// Direct3D 11 compositor. Same structure as the Metal one: one pass per output
// frame (clear to opaque black, one quad per layer, straight-alpha "over"),
// then a copy to a staging texture that is read into the caller's RGBA frame.
// Coverage and texture coordinates are computed from the pixel centre in the
// shader against the shared layerRect(), exactly like the CPU reference.
#include "vstudio/windows/d3d11_compositor.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <list>
#include <mutex>

namespace vstudio::windows {
namespace {

using Microsoft::WRL::ComPtr;

const char* kShader = R"(
cbuffer Layer : register(b0) {
    float4 quad;     // NDC x0, y0 (top), x1, y1 (bottom), one pixel larger than the layer
    float4 rect;     // layer in output pixels: left, top, right, bottom
    float opacity;
    uint exact;
    int2 offset;
};
Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

float4 vs_quad(uint vid : SV_VertexID) : SV_Position {
    float2 t = float2(float(vid & 1u), float(vid >> 1));
    return float4(lerp(quad.x, quad.z, t.x), lerp(quad.y, quad.w, t.y), 0.0, 1.0);
}

float4 ps_layer(float4 pos : SV_Position) : SV_Target {
    float2 p = pos.xy;  // pixel centre (x + 0.5, y + 0.5)
    if (p.x < rect.x || p.x >= rect.z || p.y < rect.y || p.y >= rect.w) discard;
    float4 c;
    if (exact != 0u) {
        c = tex.Load(int3(int2(floor(p)) - offset, 0));
    } else {
        c = tex.SampleLevel(smp, (p - rect.xy) / (rect.zw - rect.xy), 0);
    }
    c.a *= opacity;
    return c;
}
)";

struct LayerCB {
    float quad[4];
    float rect[4];
    float opacity;
    std::uint32_t exact;
    std::int32_t offset[2];
};
static_assert(sizeof(LayerCB) % 16 == 0, "constant buffer size");

using CompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT,
                                   UINT, ID3DBlob**, ID3DBlob**);

class D3D11Compositor final : public ICompositor {
public:
    static std::shared_ptr<D3D11Compositor> create(bool allowWarp) {
        auto c = std::shared_ptr<D3D11Compositor>(new D3D11Compositor());
        return c->init(allowWarp) ? c : nullptr;
    }

    std::string name() const override { return warp_ ? "windows.d3d11_compositor (WARP)" : "windows.d3d11_compositor"; }

    bool composite(VideoFrame& out, const std::vector<CompositeLayer>& layers, std::string* error) override {
        if (out.empty()) return true;
        std::lock_guard lk(mu_);
        const int W = out.width, H = out.height;
        if (!ensureTarget(W, H)) return fail(error, "cannot allocate the output texture");
        const float black[4] = {0, 0, 0, 1};
        ctx_->ClearRenderTargetView(rtv_.Get(), black);
        ID3D11RenderTargetView* rtvs[] = {rtv_.Get()};
        ctx_->OMSetRenderTargets(1, rtvs, nullptr);
        ctx_->OMSetBlendState(blend_.Get(), nullptr, 0xffffffff);
        D3D11_VIEWPORT vp{0, 0, float(W), float(H), 0, 1};
        ctx_->RSSetViewports(1, &vp);
        ctx_->RSSetState(raster_.Get());
        ctx_->IASetInputLayout(nullptr);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ctx_->VSSetShader(vs_.Get(), nullptr, 0);
        ctx_->PSSetShader(ps_.Get(), nullptr, 0);
        ID3D11Buffer* cbs[] = {cb_.Get()};
        ctx_->VSSetConstantBuffers(0, 1, cbs);
        ctx_->PSSetConstantBuffers(0, 1, cbs);
        ID3D11SamplerState* smps[] = {sampler_.Get()};
        ctx_->PSSetSamplers(0, 1, smps);
        for (const auto& l : layers) {
            if (!l.frame || l.frame->empty()) continue;
            const VideoFrame& src = *l.frame;
            const Transform& tr = l.transform;
            if (tr.opacity <= 0.0 || tr.scale <= 0.0) continue;
            const LayerRect r = layerRect(src.width, src.height, W, H, tr);
            if (r.left >= W || r.top >= H || r.left + r.width <= 0 || r.top + r.height <= 0) continue;
            ID3D11ShaderResourceView* srv = texture(l.frame);
            if (!srv) return fail(error, "cannot upload a layer texture");
            const double op = std::lround(std::clamp(tr.opacity, 0.0, 1.0) * 255.0) / 255.0;
            LayerCB cb{{float((r.left - 1) / W * 2.0 - 1.0), float(1.0 - (r.top - 1) / H * 2.0),
                        float((r.left + r.width + 1) / W * 2.0 - 1.0), float(1.0 - (r.top + r.height + 1) / H * 2.0)},
                       {float(r.left), float(r.top), float(r.left + r.width), float(r.top + r.height)},
                       float(op),
                       r.unscaled ? 1u : 0u,
                       {std::int32_t(std::lround(r.left)), std::int32_t(std::lround(r.top))}};
            ctx_->UpdateSubresource(cb_.Get(), 0, nullptr, &cb, 0, 0);
            ctx_->PSSetShaderResources(0, 1, &srv);
            ctx_->Draw(4, 0);
        }
        ID3D11ShaderResourceView* none[] = {nullptr};
        ctx_->PSSetShaderResources(0, 1, none);
        ctx_->CopyResource(staging_.Get(), target_.Get());
        D3D11_MAPPED_SUBRESOURCE m{};
        const HRESULT hr = ctx_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &m);
        if (FAILED(hr)) {
            const HRESULT reason = device_->GetDeviceRemovedReason();
            return fail(error, FAILED(reason) ? "device removed" : "cannot read the output back");
        }
        const auto* sp = static_cast<const std::uint8_t*>(m.pData);
        for (int y = 0; y < H; ++y)
            std::memcpy(out.rgba.data() + static_cast<std::size_t>(y) * W * 4, sp + static_cast<std::size_t>(y) * m.RowPitch,
                        static_cast<std::size_t>(W) * 4);
        ctx_->Unmap(staging_.Get(), 0);
        return true;
    }

private:
    D3D11Compositor() = default;

    static bool fail(std::string* error, const char* what) {
        if (error) *error = what;
        return false;
    }

    bool init(bool allowWarp) {
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                            D3D_FEATURE_LEVEL_10_0};
        auto make = [&](D3D_DRIVER_TYPE type) {
            return SUCCEEDED(D3D11CreateDevice(nullptr, type, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED, levels,
                                               UINT(std::size(levels)), D3D11_SDK_VERSION, &device_, nullptr, &ctx_));
        };
        if (!make(D3D_DRIVER_TYPE_HARDWARE)) {
            if (!allowWarp || !make(D3D_DRIVER_TYPE_WARP)) return false;
            warp_ = true;
        }
        // d3dcompiler_47.dll ships with Windows 8.1+; loaded at run time so a
        // missing DLL only disables this module.
        HMODULE dll = LoadLibraryW(L"d3dcompiler_47.dll");
        if (!dll) return false;
        auto compile = reinterpret_cast<CompileFn>(reinterpret_cast<void*>(GetProcAddress(dll, "D3DCompile")));
        if (!compile) return false;
        ComPtr<ID3DBlob> vsb, psb, errs;
        const SIZE_T n = std::strlen(kShader);
        if (FAILED(compile(kShader, n, "compositor", nullptr, nullptr, "vs_quad", "vs_4_0", 0, 0, &vsb, &errs)) ||
            FAILED(compile(kShader, n, "compositor", nullptr, nullptr, "ps_layer", "ps_4_0", 0, 0, &psb, &errs)))
            return false;
        if (FAILED(device_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_)) ||
            FAILED(device_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_)))
            return false;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(LayerCB);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device_->CreateBuffer(&bd, nullptr, &cb_))) return false;
        D3D11_BLEND_DESC bl{};
        auto& rt = bl.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ONE;
        rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device_->CreateBlendState(&bl, &blend_))) return false;
        D3D11_RASTERIZER_DESC rs{};
        rs.FillMode = D3D11_FILL_SOLID;
        rs.CullMode = D3D11_CULL_NONE;
        rs.DepthClipEnable = TRUE;
        if (FAILED(device_->CreateRasterizerState(&rs, &raster_))) return false;
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        return SUCCEEDED(device_->CreateSamplerState(&sd, &sampler_));
    }

    bool ensureTarget(int W, int H) {
        if (target_ && W == targetW_ && H == targetH_) return true;
        target_.Reset();
        rtv_.Reset();
        staging_.Reset();
        D3D11_TEXTURE2D_DESC d{};
        d.Width = UINT(W);
        d.Height = UINT(H);
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(device_->CreateTexture2D(&d, nullptr, &target_)) ||
            FAILED(device_->CreateRenderTargetView(target_.Get(), nullptr, &rtv_)))
            return false;
        d.Usage = D3D11_USAGE_STAGING;
        d.BindFlags = 0;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device_->CreateTexture2D(&d, nullptr, &staging_))) return false;
        targetW_ = W;
        targetH_ = H;
        return true;
    }

    // Source textures by frame identity (see the Metal compositor): a frame that
    // stays on screen is uploaded once; textures of gone frames are reused.
    ID3D11ShaderResourceView* texture(const std::shared_ptr<const VideoFrame>& f) {
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->key == f.get() && it->frame.lock() == f) {
                cache_.splice(cache_.begin(), cache_, it);
                return cache_.front().srv.Get();
            }
        }
        Cached c{f.get(), f, nullptr, nullptr, f->width, f->height};
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->frame.expired() && it->w == f->width && it->h == f->height) {
                c.tex = it->tex;
                c.srv = it->srv;
                cache_.erase(it);
                break;
            }
        }
        if (!c.tex) {
            D3D11_TEXTURE2D_DESC d{};
            d.Width = UINT(f->width);
            d.Height = UINT(f->height);
            d.MipLevels = 1;
            d.ArraySize = 1;
            d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(device_->CreateTexture2D(&d, nullptr, &c.tex)) ||
                FAILED(device_->CreateShaderResourceView(c.tex.Get(), nullptr, &c.srv)))
                return nullptr;
        }
        ctx_->UpdateSubresource(c.tex.Get(), 0, nullptr, f->rgba.data(), UINT(f->width) * 4, 0);
        cache_.push_front(std::move(c));
        while (cache_.size() > kMaxTextures) cache_.pop_back();
        return cache_.front().srv.Get();
    }

    struct Cached {
        const VideoFrame* key;
        std::weak_ptr<const VideoFrame> frame;
        ComPtr<ID3D11Texture2D> tex;
        ComPtr<ID3D11ShaderResourceView> srv;
        int w = 0, h = 0;
    };
    static constexpr std::size_t kMaxTextures = 24;

    std::mutex mu_;  // the immediate context is single-threaded
    bool warp_ = false;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11Buffer> cb_;
    ComPtr<ID3D11BlendState> blend_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11Texture2D> target_, staging_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    int targetW_ = 0, targetH_ = 0;
    std::list<Cached> cache_;
};

}  // namespace

std::shared_ptr<ICompositor> createD3D11Compositor(bool allowWarp) {
    // Probe the hardware once: the registry may ask again on a machine without it.
    if (!allowWarp) {
        static std::once_flag once;
        static bool available = false;
        std::call_once(once, [] {
            available = SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                                    D3D11_SDK_VERSION, nullptr, nullptr, nullptr));
        });
        if (!available) return nullptr;
    }
    return D3D11Compositor::create(allowWarp);
}

void registerD3D11Compositor(ModuleRegistry& r, int priority) {
    r.add<ICompositor>({.id = "windows.d3d11_compositor",
                        .displayName = "Direct3D 11 compositor",
                        .capabilities = {"composite", "gpu"},
                        .platforms = {"windows"},
                        .priority = priority},
                       [] { return createD3D11Compositor(); });
}

}  // namespace vstudio::windows
