// Direct3D 11 compositor. Same structure as the Metal one: one pass per output
// frame (clear to opaque black, one quad per layer, straight-alpha "over"),
// then a copy to a staging texture that is read into the caller's RGBA frame.
// Coverage and texture coordinates are computed from the pixel centre in the
// shader against the shared layerRect(), exactly like the CPU reference.
#include "vstudio/windows/d3d11_compositor.hpp"

#include "vstudio/grade.hpp"

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
    uint graded;
    uint3 pad;
    float4 g[8];     // grade and look uniforms (GpuGrade::u)
};
Texture2D<float4> tex : register(t0);
Texture2D<float4> gradeTab : register(t1);  // R32G32B32A32_FLOAT, 4098 x 3 (GpuGrade::table); Load only
SamplerState smp : register(s0);

// ---- colour grade: the same steps and tables as applyColorGrade() on the CPU ----
// Channels are picked with one-hot masks, not v[ch]: some mobile drivers get dynamic component indexing wrong.
float toneRow(float v, float4 m, int row) {
    float pos = saturate(v) * 4096.0;
    int i = min(int(pos), 4095);
    float t = pos - float(i);
    return lerp(dot(gradeTab.Load(int3(i, row, 0)), m), dot(gradeTab.Load(int3(i + 1, row, 0)), m), t);
}
float tone(float v, float4 m) { return toneRow(v, m, 0); }
float4 hueRow(float h) {
    float pos = h - 360.0 * floor(h / 360.0);
    int i = clamp(int(pos), 0, 359);
    float t = pos - float(i);
    return lerp(gradeTab.Load(int3(i, 1, 0)), gradeTab.Load(int3(i + 1, 1, 0)), t);
}
float3 rgb2hsv(float3 c) {
    float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b)), d = mx - mn;
    float s = mx > 1e-6 ? d / mx : 0.0;
    float h = 0.0;
    if (d > 1e-6) {
        float k = 60.0 / d;
        if (mx == c.r) { h = (c.g - c.b) * k; if (h < 0.0) h += 360.0; }
        else if (mx == c.g) h = (c.b - c.r) * k + 120.0;
        else h = (c.r - c.g) * k + 240.0;
    }
    return float3(h, s, mx);
}
float3 hsv2rgb(float3 c) {
    float h = c.x, s = c.y, v = c.z;
    if (s <= 0.0) return float3(v, v, v);
    h = h - 360.0 * floor(h / 360.0);
    float hp = h / 60.0;
    int i = min(int(hp), 5);
    float f = hp - float(i), p = v * (1.0 - s), q = v * (1.0 - s * f), t = v * (1.0 - s * (1.0 - f));
    if (i == 0) return float3(v, t, p);
    if (i == 1) return float3(q, v, p);
    if (i == 2) return float3(p, v, t);
    if (i == 3) return float3(p, q, v);
    if (i == 4) return float3(t, p, v);
    return float3(v, p, q);
}
float rangeMask(float v, float lo, float hi, float soft) {
    if (v >= lo && v <= hi) return 1.0;
    float d = v < lo ? lo - v : v - hi;
    if (d >= soft) return 0.0;
    float t = 1.0 - d / soft;
    return t * t * (3.0 - 2.0 * t);
}
float3 grade(float3 c) {
    if (g[0].y > 0.5) c = float3(tone(c.r, float4(1, 0, 0, 0)), tone(c.g, float4(0, 1, 0, 0)), tone(c.b, float4(0, 0, 1, 0)));
    if (g[0].x != 1.0) {
        float l = dot(c, float3(0.2126, 0.7152, 0.0722));
        c = l + (c - l) * g[0].x;
    }
    bool hsl = g[0].z > 0.5, qual = g[0].w > 0.5;
    if (!hsl && !qual) return c;
    c = max(c, float3(0.0, 0.0, 0.0));
    float3 hsv = rgb2hsv(c);
    if (hsl && hsv.y > 0.0) {
        float4 a = hueRow(hsv.x);
        float w = min(1.0, hsv.y * 2.0);
        hsv.x += a.x * w;
        hsv.z *= 1.0 + a.z * 0.5 * w;
        hsv.y = saturate(hsv.y * (1.0 + (a.y - 1.0) * w));
    }
    if (qual) {
        float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
        float m = hueRow(hsv.x).w * rangeMask(hsv.y, g[1].x, g[1].y, g[2].x) * rangeMask(luma, g[1].z, g[1].w, g[2].x);
        if (g[2].y > 0.5) m = 1.0 - m;
        if (g[2].z > 0.5) return float3(m, m, m);
        if (m > 0.0) {
            hsv.x += g[2].w * m;
            hsv.y = saturate(hsv.y * (1.0 + (g[3].x - 1.0) * m));
            hsv.z *= 1.0 + (g[3].y - 1.0) * m;
        }
    }
    return hsv2rgb(hsv);
}

// ---- one-tap look after the grade: the same steps as applyColorLook() on the CPU ----
// pos: the pixel centre in the layer texture's own pixels (the vignette's coordinates); sz: its size.
float3 look(float3 c, float2 pos, float2 sz) {
    float3 o = float3(toneRow(c.r, float4(1, 0, 0, 0), 2), toneRow(c.g, float4(0, 1, 0, 0), 2), toneRow(c.b, float4(0, 0, 1, 0), 2));
    if (g[5].x > 0.5) {
        float l = dot(o, float3(0.2126, 0.7152, 0.0722));
        float s = g[4].z;
        if (g[4].w != 0.0) s *= 1.0 + g[4].w * (1.0 - (max(o.r, max(o.g, o.b)) - min(o.r, min(o.g, o.b))));
        o = l + (o - l) * s;
        if (g[5].y > 0.5) o += (1.0 - l) * (1.0 - l) * g[6].rgb + l * l * g[7].rgb;
    }
    if (g[5].z > 0.0) {
        float2 d = (pos - sz * 0.5) / length(sz * 0.5);
        float t = saturate((sqrt(min(1.0, dot(d, d))) - 0.45) / 0.55);
        o *= 1.0 - g[5].z * t * t * (3.0 - 2.0 * t);
    }
    return c + (saturate(o) - c) * g[4].y;
}

float4 vs_quad(uint vid : SV_VertexID) : SV_Position {
    float2 t = float2(float(vid & 1u), float(vid >> 1));
    return float4(lerp(quad.x, quad.z, t.x), lerp(quad.y, quad.w, t.y), 0.0, 1.0);
}

float4 ps_layer(float4 pos : SV_Position) : SV_Target {
    float2 p = pos.xy;  // pixel centre (x + 0.5, y + 0.5)
    if (p.x < rect.x || p.x >= rect.z || p.y < rect.y || p.y >= rect.w) discard;
    float4 c;
    uint tw, th;
    tex.GetDimensions(tw, th);
    float2 sz = float2(tw, th);
    float2 xyPos;
    if (exact != 0u) {
        int2 xy = int2(floor(p)) - offset;
        c = tex.Load(int3(xy, 0));
        xyPos = float2(xy) + 0.5;
    } else {
        float2 uv = (p - rect.xy) / (rect.zw - rect.xy);
        c = tex.SampleLevel(smp, uv, 0);
        xyPos = uv * sz;
    }
    if (graded != 0u) {
        if (g[3].z > 0.5) c.rgb = saturate(grade(c.rgb));
        if (g[4].x > 0.5) c.rgb = look(c.rgb, xyPos, sz);
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
    std::uint32_t graded;
    std::uint32_t pad[3];
    float g[32];
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
    bool supportsGrade() const override { return true; }

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
                       {std::int32_t(std::lround(r.left)), std::int32_t(std::lround(r.top))},
                       l.grade ? 1u : 0u,
                       {0, 0, 0},
                       {}};
            ID3D11ShaderResourceView* gsrv = nullptr;
            if (l.grade) {
                std::copy(l.grade->u, l.grade->u + 32, cb.g);
                gsrv = gradeTexture(l.grade);
                if (!gsrv) return fail(error, "cannot upload a grade table");
            }
            ctx_->UpdateSubresource(cb_.Get(), 0, nullptr, &cb, 0, 0);
            ctx_->PSSetShaderResources(0, 1, &srv);
            ctx_->PSSetShaderResources(1, 1, &gsrv);
            ctx_->Draw(4, 0);
        }
        ID3D11ShaderResourceView* none[] = {nullptr, nullptr};
        ctx_->PSSetShaderResources(0, 2, none);
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
        Cached c{f.get(), f, nullptr, nullptr, f->width, f->height, f->deep()};
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->frame.expired() && it->w == f->width && it->h == f->height && it->deep == f->deep()) {
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
            d.Format = f->deep() ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(device_->CreateTexture2D(&d, nullptr, &c.tex)) ||
                FAILED(device_->CreateShaderResourceView(c.tex.Get(), nullptr, &c.srv)))
                return nullptr;
        }
        if (f->deep())  // 10-bit sources graded in float: half floats straight from the decoder
            ctx_->UpdateSubresource(c.tex.Get(), 0, nullptr, f->half.data(), UINT(f->width) * 8, 0);
        else
            ctx_->UpdateSubresource(c.tex.Get(), 0, nullptr, f->rgba.data(), UINT(f->width) * 4, 0);
        cache_.push_front(std::move(c));
        while (cache_.size() > kMaxTextures) cache_.pop_back();
        return cache_.front().srv.Get();
    }

    // Grade tables by grade identity (gpuGrade() reuses one object per parameter set).
    ID3D11ShaderResourceView* gradeTexture(const std::shared_ptr<const GpuGrade>& g) {
        for (auto it = grades_.begin(); it != grades_.end(); ++it)
            if (it->key == g.get() && it->grade.lock() == g) {
                grades_.splice(grades_.begin(), grades_, it);
                return grades_.front().srv.Get();
            }
        GradeTex t{g.get(), g, nullptr, nullptr};
        D3D11_TEXTURE2D_DESC d{};
        d.Width = UINT(GpuGrade::kTableWidth);
        d.Height = UINT(GpuGrade::kTableRows);
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{g->table.data(), UINT(GpuGrade::kTableWidth) * 16, 0};
        if (FAILED(device_->CreateTexture2D(&d, &init, &t.tex)) ||
            FAILED(device_->CreateShaderResourceView(t.tex.Get(), nullptr, &t.srv)))
            return nullptr;
        grades_.push_front(std::move(t));
        while (grades_.size() > 8) grades_.pop_back();
        return grades_.front().srv.Get();
    }

    struct GradeTex {
        const GpuGrade* key;
        std::weak_ptr<const GpuGrade> grade;
        ComPtr<ID3D11Texture2D> tex;
        ComPtr<ID3D11ShaderResourceView> srv;
    };

    struct Cached {
        const VideoFrame* key;
        std::weak_ptr<const VideoFrame> frame;
        ComPtr<ID3D11Texture2D> tex;
        ComPtr<ID3D11ShaderResourceView> srv;
        int w = 0, h = 0;
        bool deep = false;
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
    std::list<GradeTex> grades_;
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
