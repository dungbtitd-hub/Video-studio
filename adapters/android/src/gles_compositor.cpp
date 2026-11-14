// OpenGL ES 3 compositor. Same design as the Metal and D3D11 ones: one pass
// per output frame into an offscreen framebuffer (clear to opaque black, one
// quad per layer, straight-alpha "over"), then glReadPixels into the caller's
// frame. Coverage and texture coordinates come from the pixel centre in the
// shader against the shared layerRect().
//
// Orientation: framebuffer row 0 is the top row of the image (the quad is
// placed with y = top maps to NDC -1), so glReadPixels needs no flip.
//
// The EGL context is made current only for the duration of one call (under the
// mutex), so any thread may composite; the context never stays bound.
#include "vstudio/android/gles_compositor.hpp"

#include "vstudio/grade.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <list>
#include <mutex>

namespace vstudio::android {
namespace {

const char* kVertex = R"(#version 300 es
uniform vec4 uQuad;  // NDC x0, y0 (top), x1, y1 (bottom), one pixel larger than the layer
void main() {
    vec2 t = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    gl_Position = vec4(mix(uQuad.x, uQuad.z, t.x), mix(uQuad.y, uQuad.w, t.y), 0.0, 1.0);
}
)";

const char* kFragment = R"(#version 300 es
precision highp float;
precision highp int;
uniform sampler2D uTex;
uniform vec4 uRect;      // layer in output pixels: left, top, right, bottom
uniform float uOpacity;
uniform int uExact;
uniform ivec2 uOffset;
uniform int uGraded;
uniform highp sampler2D uGrade;  // RGBA32F, 4098 x 3 (see GpuGrade in grade.hpp); texelFetch only
uniform vec4 uG[8];
out vec4 color;

// ---- colour grade: the same steps and tables as applyColorGrade() on the CPU ----
// Channels are picked with one-hot masks, not v[ch]: some mobile drivers get dynamic component indexing wrong.
float toneRow(float v, vec4 m, int row) {
    float pos = clamp(v, 0.0, 1.0) * 4096.0;
    int i = min(int(pos), 4095);
    float t = pos - float(i);
    return mix(dot(texelFetch(uGrade, ivec2(i, row), 0), m), dot(texelFetch(uGrade, ivec2(i + 1, row), 0), m), t);
}
float tone(float v, vec4 m) { return toneRow(v, m, 0); }
vec4 hueRow(float h) {
    float pos = h - 360.0 * floor(h / 360.0);  // hue shifts may leave 0..360
    int i = clamp(int(pos), 0, 359);
    float t = pos - float(i);
    return mix(texelFetch(uGrade, ivec2(i, 1), 0), texelFetch(uGrade, ivec2(i + 1, 1), 0), t);
}
vec3 rgb2hsv(vec3 c) {
    float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b)), d = mx - mn;
    float s = mx > 1e-6 ? d / mx : 0.0;
    float h = 0.0;
    if (d > 1e-6) {
        float k = 60.0 / d;
        if (mx == c.r) { h = (c.g - c.b) * k; if (h < 0.0) h += 360.0; }
        else if (mx == c.g) h = (c.b - c.r) * k + 120.0;
        else h = (c.r - c.g) * k + 240.0;
    }
    return vec3(h, s, mx);
}
vec3 hsv2rgb(vec3 c) {
    float h = c.x, s = c.y, v = c.z;
    if (s <= 0.0) return vec3(v);
    h = h - 360.0 * floor(h / 360.0);
    float hp = h / 60.0;
    int i = min(int(hp), 5);
    float f = hp - float(i), p = v * (1.0 - s), q = v * (1.0 - s * f), t = v * (1.0 - s * (1.0 - f));
    if (i == 0) return vec3(v, t, p);
    if (i == 1) return vec3(q, v, p);
    if (i == 2) return vec3(p, v, t);
    if (i == 3) return vec3(p, q, v);
    if (i == 4) return vec3(t, p, v);
    return vec3(v, p, q);
}
float rangeMask(float v, float lo, float hi, float soft) {
    if (v >= lo && v <= hi) return 1.0;
    float d = v < lo ? lo - v : v - hi;
    if (d >= soft) return 0.0;
    float t = 1.0 - d / soft;
    return t * t * (3.0 - 2.0 * t);
}
vec3 grade(vec3 c) {
    if (uG[0].y > 0.5) c = vec3(tone(c.r, vec4(1, 0, 0, 0)), tone(c.g, vec4(0, 1, 0, 0)), tone(c.b, vec4(0, 0, 1, 0)));
    if (uG[0].x != 1.0) {
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        c = l + (c - l) * uG[0].x;
    }
    bool hsl = uG[0].z > 0.5, qual = uG[0].w > 0.5;
    if (!hsl && !qual) return c;
    c = max(c, vec3(0.0));
    vec3 hsv = rgb2hsv(c);
    if (hsl && hsv.y > 0.0) {
        vec4 a = hueRow(hsv.x);
        float w = min(1.0, hsv.y * 2.0);
        hsv.x += a.x * w;
        hsv.z *= 1.0 + a.z * 0.5 * w;
        hsv.y = clamp(hsv.y * (1.0 + (a.y - 1.0) * w), 0.0, 1.0);
    }
    if (qual) {
        float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
        float m = hueRow(hsv.x).w * rangeMask(hsv.y, uG[1].x, uG[1].y, uG[2].x) * rangeMask(luma, uG[1].z, uG[1].w, uG[2].x);
        if (uG[2].y > 0.5) m = 1.0 - m;
        if (uG[2].z > 0.5) return vec3(m);
        if (m > 0.0) {
            hsv.x += uG[2].w * m;
            hsv.y = clamp(hsv.y * (1.0 + (uG[3].x - 1.0) * m), 0.0, 1.0);
            hsv.z *= 1.0 + (uG[3].y - 1.0) * m;
        }
    }
    return hsv2rgb(hsv);
}

// ---- one-tap look after the grade: the same steps as applyColorLook() on the CPU ----
// pos: the pixel centre in the layer texture's own pixels (the vignette's coordinates).
vec3 look(vec3 c, vec2 pos) {
    vec3 o = vec3(toneRow(c.r, vec4(1, 0, 0, 0), 2), toneRow(c.g, vec4(0, 1, 0, 0), 2), toneRow(c.b, vec4(0, 0, 1, 0), 2));
    if (uG[5].x > 0.5) {
        float l = dot(o, vec3(0.2126, 0.7152, 0.0722));
        float s = uG[4].z;
        if (uG[4].w != 0.0) s *= 1.0 + uG[4].w * (1.0 - (max(o.r, max(o.g, o.b)) - min(o.r, min(o.g, o.b))));
        o = l + (o - l) * s;
        if (uG[5].y > 0.5) o += (1.0 - l) * (1.0 - l) * uG[6].rgb + l * l * uG[7].rgb;
    }
    if (uG[5].z > 0.0) {
        vec2 sz = vec2(textureSize(uTex, 0));
        vec2 d = (pos - sz * 0.5) / length(sz * 0.5);
        float t = clamp((sqrt(min(1.0, dot(d, d))) - 0.45) / 0.55, 0.0, 1.0);
        o *= 1.0 - uG[5].z * t * t * (3.0 - 2.0 * t);
    }
    return c + (clamp(o, 0.0, 1.0) - c) * uG[4].y;
}

void main() {
    vec2 p = gl_FragCoord.xy;  // pixel centre (x + 0.5, y + 0.5); y counts from the top row
    if (p.x < uRect.x || p.x >= uRect.z || p.y < uRect.y || p.y >= uRect.w) discard;
    vec4 c;
    vec2 pos;
    if (uExact != 0) {
        ivec2 xy = ivec2(floor(p)) - uOffset;
        c = texelFetch(uTex, xy, 0);
        pos = vec2(xy) + 0.5;
    } else {
        vec2 uv = (p - uRect.xy) / (uRect.zw - uRect.xy);
        c = texture(uTex, uv);
        pos = uv * vec2(textureSize(uTex, 0));
    }
    if (uGraded != 0) {
        if (uG[3].z > 0.5) c.rgb = clamp(grade(c.rgb), 0.0, 1.0);
        if (uG[4].x > 0.5) c.rgb = look(c.rgb, pos);
    }
    c.a *= uOpacity;
    color = c;
}
)";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(s);
        return 0;
    }
    return s;
}

class GlesCompositor final : public ICompositor {
public:
    static std::shared_ptr<GlesCompositor> create() {
        auto c = std::shared_ptr<GlesCompositor>(new GlesCompositor());
        return c->init() ? c : nullptr;
    }
    ~GlesCompositor() override {
        if (dpy_ == EGL_NO_DISPLAY) return;
        if (ctx_ != EGL_NO_CONTEXT && eglMakeCurrent(dpy_, surf_, surf_, ctx_)) {
            cache_.clear();  // deletes textures while the context is current
            grades_.clear();
            if (fbo_) glDeleteFramebuffers(1, &fbo_);
            if (target_) glDeleteTextures(1, &target_);
            if (prog_) glDeleteProgram(prog_);
            if (vao_) glDeleteVertexArrays(1, &vao_);
            eglMakeCurrent(dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        if (ctx_ != EGL_NO_CONTEXT) eglDestroyContext(dpy_, ctx_);
        if (surf_ != EGL_NO_SURFACE) eglDestroySurface(dpy_, surf_);
    }

    std::string name() const override { return "android.gles_compositor"; }
    bool supportsGrade() const override { return true; }

    bool composite(VideoFrame& out, const std::vector<CompositeLayer>& layers, std::string* error) override {
        if (out.empty()) return true;
        std::lock_guard lk(mu_);
        if (!eglMakeCurrent(dpy_, surf_, surf_, ctx_)) return fail(error, "cannot make the EGL context current");
        const bool ok = draw(out, layers, error);
        eglMakeCurrent(dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        return ok;
    }

private:
    GlesCompositor() = default;

    static bool fail(std::string* error, const char* what) {
        if (error) *error = what;
        return false;
    }

    bool draw(VideoFrame& out, const std::vector<CompositeLayer>& layers, std::string* error) {
        const int W = out.width, H = out.height;
        if (W > maxTex_ || H > maxTex_) return fail(error, "output larger than GL_MAX_TEXTURE_SIZE");
        if (!ensureTarget(W, H)) return fail(error, "cannot allocate the output framebuffer");
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, W, H);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(prog_);
        glBindVertexArray(vao_);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(uTex_, 0);
        for (const auto& l : layers) {
            if (!l.frame || l.frame->empty()) continue;
            const VideoFrame& src = *l.frame;
            const Transform& tr = l.transform;
            if (tr.opacity <= 0.0 || tr.scale <= 0.0) continue;
            const LayerRect r = layerRect(src.width, src.height, W, H, tr);
            if (r.left >= W || r.top >= H || r.left + r.width <= 0 || r.top + r.height <= 0) continue;
            if (src.width > maxTex_ || src.height > maxTex_) return fail(error, "layer larger than GL_MAX_TEXTURE_SIZE");
            const GLuint tex = texture(l.frame);
            if (!tex) return fail(error, "cannot upload a layer texture");
            glBindTexture(GL_TEXTURE_2D, tex);
            // y = top maps to NDC -1: framebuffer row 0 is the image's top row.
            glUniform4f(uQuad_, float((r.left - 1) / W * 2.0 - 1.0), float((r.top - 1) / H * 2.0 - 1.0),
                        float((r.left + r.width + 1) / W * 2.0 - 1.0), float((r.top + r.height + 1) / H * 2.0 - 1.0));
            glUniform4f(uRect_, float(r.left), float(r.top), float(r.left + r.width), float(r.top + r.height));
            glUniform1f(uOpacity_, float(std::lround(std::clamp(tr.opacity, 0.0, 1.0) * 255.0) / 255.0));
            glUniform1i(uExact_, r.unscaled ? 1 : 0);
            if (l.grade) {
                glActiveTexture(GL_TEXTURE1);  // the upload binds: keep it off the layer's unit
                const GLuint g = gradeTexture(l.grade);
                if (g) glBindTexture(GL_TEXTURE_2D, g);
                glActiveTexture(GL_TEXTURE0);
                if (!g) return fail(error, "cannot upload a grade table");
                glUniform4fv(uG_, 8, l.grade->u);
            }
            glUniform1i(uGraded_, l.grade ? 1 : 0);
            glUniform2i(uOffset_, GLint(std::lround(r.left)), GLint(std::lround(r.top)));
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out.rgba.data());
        const GLenum err = glGetError();
        if (err != GL_NO_ERROR) return fail(error, err == GL_OUT_OF_MEMORY ? "GL out of memory" : "GL error");
        return true;
    }

    bool init() {
        dpy_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (dpy_ == EGL_NO_DISPLAY || !eglInitialize(dpy_, nullptr, nullptr)) return false;
        const EGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLConfig cfg = nullptr;
        EGLint n = 0;
        if (!eglChooseConfig(dpy_, attrs, &cfg, 1, &n) || n < 1) return false;
        const EGLint pb[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        surf_ = eglCreatePbufferSurface(dpy_, cfg, pb);
        const EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        ctx_ = eglCreateContext(dpy_, cfg, EGL_NO_CONTEXT, ca);
        if (surf_ == EGL_NO_SURFACE || ctx_ == EGL_NO_CONTEXT) return false;
        if (!eglMakeCurrent(dpy_, surf_, surf_, ctx_)) return false;
        bool ok = false;
        GLuint vs = compile(GL_VERTEX_SHADER, kVertex), fs = compile(GL_FRAGMENT_SHADER, kFragment);
        if (vs && fs) {
            prog_ = glCreateProgram();
            glAttachShader(prog_, vs);
            glAttachShader(prog_, fs);
            glLinkProgram(prog_);
            GLint linked = 0;
            glGetProgramiv(prog_, GL_LINK_STATUS, &linked);
            ok = linked != 0;
        }
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        if (ok) {
            uTex_ = glGetUniformLocation(prog_, "uTex");
            uQuad_ = glGetUniformLocation(prog_, "uQuad");
            uRect_ = glGetUniformLocation(prog_, "uRect");
            uOpacity_ = glGetUniformLocation(prog_, "uOpacity");
            uExact_ = glGetUniformLocation(prog_, "uExact");
            uOffset_ = glGetUniformLocation(prog_, "uOffset");
            uGraded_ = glGetUniformLocation(prog_, "uGraded");
            uG_ = glGetUniformLocation(prog_, "uG");
            glUseProgram(prog_);
            glUniform1i(glGetUniformLocation(prog_, "uGrade"), 1);
            glUseProgram(0);
            glGenVertexArrays(1, &vao_);
            glGenFramebuffers(1, &fbo_);
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex_);
            ok = vao_ && fbo_ && glGetError() == GL_NO_ERROR;
        }
        eglMakeCurrent(dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        return ok;
    }

    bool ensureTarget(int W, int H) {
        if (target_ && W == targetW_ && H == targetH_) return true;
        if (!target_) glGenTextures(1, &target_);
        glBindTexture(GL_TEXTURE_2D, target_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target_, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
        targetW_ = W;
        targetH_ = H;
        return true;
    }

    // Source textures by frame identity (see the Metal compositor): a frame that
    // stays on screen is uploaded once; textures of gone frames are reused.
    GLuint texture(const std::shared_ptr<const VideoFrame>& f) {
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->key == f.get() && it->frame.lock() == f) {
                cache_.splice(cache_.begin(), cache_, it);
                return cache_.front().tex.id;
            }
        }
        Cached c{f.get(), f, {}, f->width, f->height, f->deep()};
        bool reuse = false;
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->frame.expired() && it->w == f->width && it->h == f->height && it->deep == f->deep()) {
                std::swap(c.tex.id, it->tex.id);
                cache_.erase(it);
                reuse = true;
                break;
            }
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (!reuse) {
            glGenTextures(1, &c.tex.id);
            glBindTexture(GL_TEXTURE_2D, c.tex.id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexStorage2D(GL_TEXTURE_2D, 1, f->deep() ? GL_RGBA16F : GL_RGBA8, f->width, f->height);
        } else {
            glBindTexture(GL_TEXTURE_2D, c.tex.id);
        }
        if (f->deep())  // 10-bit sources graded in float: half floats straight from the decoder
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f->width, f->height, GL_RGBA, GL_HALF_FLOAT, f->half.data());
        else
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f->width, f->height, GL_RGBA, GL_UNSIGNED_BYTE, f->rgba.data());
        if (glGetError() != GL_NO_ERROR) return 0;
        cache_.push_front(std::move(c));
        while (cache_.size() > kMaxTextures) cache_.pop_back();
        return cache_.front().tex.id;
    }

    // Grade tables by grade identity (gpuGrade() reuses one object per parameter set).
    GLuint gradeTexture(const std::shared_ptr<const GpuGrade>& g) {
        for (auto it = grades_.begin(); it != grades_.end(); ++it)
            if (it->key == g.get() && it->grade.lock() == g) {
                grades_.splice(grades_.begin(), grades_, it);
                return grades_.front().tex.id;
            }
        GradeTex t{g.get(), g, {}};
        glGenTextures(1, &t.tex.id);
        glBindTexture(GL_TEXTURE_2D, t.tex.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, GpuGrade::kTableWidth, GpuGrade::kTableRows);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GpuGrade::kTableWidth, GpuGrade::kTableRows, GL_RGBA, GL_FLOAT, g->table.data());
        if (glGetError() != GL_NO_ERROR) return 0;
        grades_.push_front(std::move(t));
        while (grades_.size() > 8) grades_.pop_back();
        return grades_.front().tex.id;
    }

    struct Tex {  // owns one GL texture; destroyed only while the context is current
        GLuint id = 0;
        Tex() = default;
        Tex(Tex&& o) noexcept : id(o.id) { o.id = 0; }
        Tex& operator=(Tex&& o) noexcept {
            std::swap(id, o.id);
            return *this;
        }
        ~Tex() {
            if (id) glDeleteTextures(1, &id);
        }
    };
    struct Cached {
        const VideoFrame* key;
        std::weak_ptr<const VideoFrame> frame;
        Tex tex;
        int w = 0, h = 0;
        bool deep = false;
    };
    struct GradeTex {
        const GpuGrade* key;
        std::weak_ptr<const GpuGrade> grade;
        Tex tex;
    };
    static constexpr std::size_t kMaxTextures = 24;

    std::mutex mu_;
    EGLDisplay dpy_ = EGL_NO_DISPLAY;
    EGLSurface surf_ = EGL_NO_SURFACE;
    EGLContext ctx_ = EGL_NO_CONTEXT;
    GLuint prog_ = 0, vao_ = 0, fbo_ = 0, target_ = 0;
    GLint uTex_ = -1, uQuad_ = -1, uRect_ = -1, uOpacity_ = -1, uExact_ = -1, uOffset_ = -1, uGraded_ = -1, uG_ = -1;
    GLint maxTex_ = 2048;
    int targetW_ = 0, targetH_ = 0;
    std::list<Cached> cache_;
    std::list<GradeTex> grades_;
};

}  // namespace

std::shared_ptr<ICompositor> createGlesCompositor() {
    // Probe once: the registry may ask again on a device where EGL fails.
    static std::mutex m;
    static int state = 0;  // 0 unknown, 1 ok, -1 unavailable
    std::lock_guard lk(m);
    if (state < 0) return nullptr;
    auto c = GlesCompositor::create();
    state = c ? 1 : -1;
    return c;
}

void registerGlesCompositor(ModuleRegistry& r, int priority) {
    r.add<ICompositor>({.id = "android.gles_compositor",
                        .displayName = "OpenGL ES compositor",
                        .capabilities = {"composite", "gpu"},
                        .platforms = {"android"},
                        .priority = priority},
                       [] { return createGlesCompositor(); });
}

}  // namespace vstudio::android
