#include "vstudio/audio_fx.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

#include "vstudio/modules.hpp"

namespace vstudio::audiofx {

namespace {

const Json& emptyObject() {
    static const Json e = Json::object();
    return e;
}

// ---- equaliser: low shelf 80 Hz, peaks at 250 / 1k / 4k, high shelf 10 kHz (RBJ biquads) ----

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

Biquad design(int band, double gainDb, double sr) {
    const double f = std::min(kEqFreqs[static_cast<std::size_t>(band)], sr * 0.45);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2.0 * std::numbers::pi * f / sr;
    const double cw = std::cos(w), sw = std::sin(w);
    Biquad q;
    double a0 = 1;
    if (band == 0 || band == kEqBands - 1) {
        const double alpha = sw / 2.0 * std::sqrt(2.0);  // shelf slope 1
        const double s = 2.0 * std::sqrt(A) * alpha;
        if (band == 0) {
            q.b0 = A * ((A + 1) - (A - 1) * cw + s);
            q.b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            q.b2 = A * ((A + 1) - (A - 1) * cw - s);
            a0 = (A + 1) + (A - 1) * cw + s;
            q.a1 = -2 * ((A - 1) + (A + 1) * cw);
            q.a2 = (A + 1) + (A - 1) * cw - s;
        } else {
            q.b0 = A * ((A + 1) + (A - 1) * cw + s);
            q.b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            q.b2 = A * ((A + 1) + (A - 1) * cw - s);
            a0 = (A + 1) - (A - 1) * cw + s;
            q.a1 = 2 * ((A - 1) - (A + 1) * cw);
            q.a2 = (A + 1) - (A - 1) * cw - s;
        }
    } else {
        const double alpha = sw / (2.0 * 1.0);  // Q = 1 (about 1.4 octaves)
        q.b0 = 1 + alpha * A;
        q.b1 = -2 * cw;
        q.b2 = 1 - alpha * A;
        a0 = 1 + alpha / A;
        q.a1 = -2 * cw;
        q.a2 = 1 - alpha / A;
    }
    q.b0 /= a0, q.b1 /= a0, q.b2 /= a0, q.a1 /= a0, q.a2 /= a0;
    return q;
}

std::array<double, kEqBands> eqGains(const Json& params) {
    std::array<double, kEqBands> g{};
    if (params.contains("gains") && params["gains"].is_array()) {
        for (std::size_t i = 0; i < g.size() && i < params["gains"].size(); ++i)
            if (params["gains"][i].is_number()) g[i] = std::clamp(params["gains"][i].get<double>(), -12.0, 12.0);
    } else if (const EqPreset* p = findEqPreset(params.value("preset", std::string{}))) {
        g = p->gains;
    }
    return g;
}

class Equalizer final : public IAudioEffect {
public:
    std::int64_t margin(const Json&, int sampleRate) const override { return sampleRate / 10; }  // 100 ms settles 80 Hz
    void apply(AudioBuffer& buf, const Json& params) const override {
        const auto gains = eqGains(params);
        const int ch = buf.channels;
        const std::int64_t n = buf.frames();
        for (int band = 0; band < kEqBands; ++band) {
            if (std::abs(gains[static_cast<std::size_t>(band)]) < 0.05) continue;
            const Biquad q = design(band, gains[static_cast<std::size_t>(band)], buf.sampleRate);
            for (int k = 0; k < ch; ++k) {
                double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
                float* s = buf.samples.data() + k;
                for (std::int64_t i = 0; i < n; ++i) {
                    const double x = s[i * ch];
                    const double y = q.b0 * x + q.b1 * x1 + q.b2 * x2 - q.a1 * y1 - q.a2 * y2;
                    x2 = x1, x1 = x, y2 = y1, y1 = y;
                    s[i * ch] = static_cast<float>(y);
                }
            }
        }
    }
};

// ---- FFT (radix 2, in place) ----

void fft(std::vector<std::complex<float>>& a, bool inverse) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * std::numbers::pi / static_cast<double>(len) * (inverse ? 1 : -1);
        const std::complex<float> wl(static_cast<float>(std::cos(ang)), static_cast<float>(std::sin(ang)));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<float> w(1);
            for (std::size_t j = 0; j < len / 2; ++j) {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= static_cast<float>(n);
}

const std::vector<float>& sqrtHann() {
    static const std::vector<float> w = [] {
        std::vector<float> v(kDenoiseFft);
        for (int i = 0; i < kDenoiseFft; ++i)
            v[static_cast<std::size_t>(i)] =
                static_cast<float>(std::sqrt(0.5 - 0.5 * std::cos(2 * std::numbers::pi * i / kDenoiseFft)));
        return v;
    }();
    return w;
}

constexpr int kBins = kDenoiseFft / 2 + 1;

// Magnitude spectra of the mono mix, one per hop.
std::vector<std::vector<float>> spectra(const AudioBuffer& a) {
    std::vector<std::vector<float>> out;
    const int ch = std::max(1, a.channels);
    const std::int64_t n = a.frames();
    const auto& w = sqrtHann();
    std::vector<std::complex<float>> buf(kDenoiseFft);
    for (std::int64_t start = 0; start + kDenoiseFft <= n; start += kAudioFxAlign) {
        for (int i = 0; i < kDenoiseFft; ++i) {
            float m = 0;
            for (int k = 0; k < ch; ++k) m += a.samples[static_cast<std::size_t>((start + i) * ch + k)];
            buf[static_cast<std::size_t>(i)] = m / static_cast<float>(ch) * w[static_cast<std::size_t>(i)];
        }
        fft(buf, false);
        std::vector<float> mag(kBins);
        for (int b = 0; b < kBins; ++b) mag[static_cast<std::size_t>(b)] = std::abs(buf[static_cast<std::size_t>(b)]);
        out.push_back(std::move(mag));
    }
    return out;
}

// Spectral noise gate: each bin is turned down by how close it is to the noise floor
// (Wiener-like gain with a floor, so speech keeps its body and hiss drops ~20 dB).
class Denoise final : public IAudioEffect {
public:
    std::int64_t margin(const Json&, int) const override { return kDenoiseFft; }
    void apply(AudioBuffer& buf, const Json& params) const override {
        const double amount = std::clamp(params.value("amount", 0.6), 0.0, 1.0);
        if (amount <= 0.001) return;
        std::vector<float> noise;
        if (params.contains("profile") && params["profile"].is_array() && params["profile"].size() == kBins) {
            noise = params["profile"].get<std::vector<float>>();
        } else {
            noise = noiseProfile(buf);  // not analysed yet: estimate from this block
        }
        if (noise.size() != kBins) return;
        const int ch = buf.channels;
        const std::int64_t n = buf.frames();
        const auto& w = sqrtHann();
        const float floorGain = static_cast<float>(1.0 - 0.9 * amount);  // up to -20 dB
        const float over = static_cast<float>(1.0 + amount);             // oversubtraction
        std::vector<float> out(buf.samples.size(), 0.0f);
        std::vector<std::complex<float>> f(kDenoiseFft);
        std::vector<float> gain(kBins), smooth(kBins);
        for (int k = 0; k < ch; ++k) {
            // Frames start one window before the block so every sample is covered by 4 frames.
            for (std::int64_t start = -(kDenoiseFft - kAudioFxAlign); start < n; start += kAudioFxAlign) {
                for (int i = 0; i < kDenoiseFft; ++i) {
                    const std::int64_t at = start + i;
                    const float x = (at >= 0 && at < n) ? buf.samples[static_cast<std::size_t>(at * ch + k)] : 0.0f;
                    f[static_cast<std::size_t>(i)] = x * w[static_cast<std::size_t>(i)];
                }
                fft(f, false);
                for (int b = 0; b < kBins; ++b) {
                    const float mag = std::abs(f[static_cast<std::size_t>(b)]) + 1e-9f;
                    const float r = over * noise[static_cast<std::size_t>(b)] / mag;
                    gain[static_cast<std::size_t>(b)] = std::max(floorGain, 1.0f - r * r);
                }
                // Light smoothing across neighbouring bins (less "musical noise").
                for (int b = 0; b < kBins; ++b) {
                    const float l = gain[static_cast<std::size_t>(std::max(0, b - 1))];
                    const float r = gain[static_cast<std::size_t>(std::min(kBins - 1, b + 1))];
                    smooth[static_cast<std::size_t>(b)] = 0.25f * l + 0.5f * gain[static_cast<std::size_t>(b)] + 0.25f * r;
                }
                for (int b = 0; b < kBins; ++b) {
                    f[static_cast<std::size_t>(b)] *= smooth[static_cast<std::size_t>(b)];
                    if (b > 0 && b < kDenoiseFft / 2) f[static_cast<std::size_t>(kDenoiseFft - b)] = std::conj(f[static_cast<std::size_t>(b)]);
                }
                fft(f, true);
                for (int i = 0; i < kDenoiseFft; ++i) {
                    const std::int64_t at = start + i;
                    if (at < 0 || at >= n) continue;
                    // sqrt-Hann in and out = Hann; Hann at hop N/4 sums to 2.
                    out[static_cast<std::size_t>(at * ch + k)] +=
                        f[static_cast<std::size_t>(i)].real() * w[static_cast<std::size_t>(i)] * 0.5f;
                }
            }
        }
        buf.samples.swap(out);
    }
};

}  // namespace

const std::vector<EqPreset>& eqPresets() {
    static const std::vector<EqPreset> p = {
        {"flat", "Gốc", {0, 0, 0, 0, 0}},
        {"voice", "Giọng nói rõ", {-6, -2, 2, 4, 2}},
        {"podcast", "Podcast", {-4, 1, 2, 3, 0}},
        {"bass", "Tăng bass", {6, 3, 0, 0, 0}},
        {"treble", "Tăng treble", {0, 0, 0, 3, 6}},
        {"warm", "Ấm", {3, 2, 0, -2, -3}},
        {"bright", "Sáng", {-2, 0, 1, 3, 4}},
        {"music", "Nhạc", {4, 1, -1, 2, 4}},
        {"phone", "Điện thoại", {-12, -6, 4, 0, -12}},
        {"radio", "Radio cũ", {-12, -3, 5, 3, -12}},
    };
    return p;
}

const EqPreset* findEqPreset(const std::string& id) {
    for (const auto& p : eqPresets())
        if (p.id == id) return &p;
    return nullptr;
}

const Json& clipAudio(const Clip& c) {
    auto it = c.extra.find("audio");
    return it != c.extra.end() && it->is_object() ? *it : emptyObject();
}

bool detached(const Clip& c) { return clipAudio(c).value("detached", false); }

bool hasFades(const Clip& c) {
    const Json& a = clipAudio(c);
    return a.value("fade_in", 0.0) > 0 || a.value("fade_out", 0.0) > 0;
}

double fadeGain(const Clip& c, Time t) {
    const Json& a = clipAudio(c);
    const double in = a.value("fade_in", 0.0), out = a.value("fade_out", 0.0);
    double g = 1.0;
    const double fromStart = (t - c.timelineIn).seconds(), toEnd = (c.timelineOut() - t).seconds();
    if (in > 0 && fromStart < in) g *= std::sin(std::clamp(fromStart / in, 0.0, 1.0) * std::numbers::pi / 2);
    if (out > 0 && toEnd < out) g *= std::sin(std::clamp(toEnd / out, 0.0, 1.0) * std::numbers::pi / 2);
    return g * g;  // sin^2: smooth start and end
}

std::vector<std::pair<std::string, Json>> clipEffects(const Clip& c) {
    std::vector<std::pair<std::string, Json>> fx;
    const Json& a = clipAudio(c);
    if (a.contains("denoise") && a["denoise"].is_object() && a["denoise"].value("amount", 0.0) > 0)
        fx.emplace_back(kDenoiseEffect, a["denoise"]);
    if (a.contains("eq") && a["eq"].is_object()) {
        const auto g = eqGains(a["eq"]);
        if (std::any_of(g.begin(), g.end(), [](double x) { return std::abs(x) >= 0.05; })) fx.emplace_back(kEqEffect, a["eq"]);
    }
    if (a.contains("effects") && a["effects"].is_array())  // third-party IAudioEffect modules
        for (const auto& e : a["effects"])
            if (e.is_object() && e.contains("type")) fx.emplace_back(e["type"].get<std::string>(), e.value("params", Json::object()));
    return fx;
}

double trackPan(const Track& t) {
    auto it = t.extra.find("pan");
    return it != t.extra.end() && it->is_number() ? std::clamp(it->get<double>(), -1.0, 1.0) : 0.0;
}

bool trackSolo(const Track& t) {
    auto it = t.extra.find("solo");
    return it != t.extra.end() && it->is_boolean() && it->get<bool>();
}

bool trackAudible(const Project& p, const Track& t) {
    if (!t.enabled || t.muted) return false;
    const bool anySolo = std::any_of(p.tracks.begin(), p.tracks.end(), [](const Track& x) { return trackSolo(x); });
    return !anySolo || trackSolo(t);
}

void applyClipEffects(ModuleRegistry* modules, const std::vector<std::pair<std::string, Json>>& fx, AudioBuffer& buf) {
    if (!modules) return;
    for (const auto& [type, params] : fx)
        if (auto e = modules->get<IAudioEffect>(type)) e->apply(buf, params);
}

std::int64_t clipMargin(ModuleRegistry* modules, const std::vector<std::pair<std::string, Json>>& fx, int sampleRate) {
    std::int64_t m = 0;
    if (!modules) return 0;
    for (const auto& [type, params] : fx)
        if (auto e = modules->get<IAudioEffect>(type)) m = std::max(m, e->margin(params, sampleRate));
    return m;
}

std::vector<float> noiseProfile(const AudioBuffer& audio) {
    const auto s = spectra(audio);
    if (s.empty()) return {};
    std::vector<float> prof(kBins);
    std::vector<float> col(s.size());
    for (int b = 0; b < kBins; ++b) {
        for (std::size_t i = 0; i < s.size(); ++i) col[i] = s[i][static_cast<std::size_t>(b)];
        const std::size_t k = col.size() * 15 / 100;
        std::nth_element(col.begin(), col.begin() + static_cast<std::ptrdiff_t>(k), col.end());
        // The 15th percentile of a noise bin is about half its mean magnitude.
        prof[static_cast<std::size_t>(b)] = std::round(col[k] * 2.0f * 1e5f) / 1e5f;
    }
    return prof;
}

std::vector<float> analyzeClipNoise(IMediaDecoder& decoder, const Clip& c, int sampleRate, double maxSeconds,
                                    const std::atomic<bool>* cancel, std::string* error) {
    const Time len = std::min(c.sourceDuration(), Time::fromSeconds(maxSeconds));
    const std::int64_t total = len.toSample(sampleRate, Rounding::Down);
    AudioBuffer all;
    all.sampleRate = sampleRate;
    all.channels = 1;
    constexpr std::int64_t kChunk = 48000;
    for (std::int64_t done = 0; done < total; done += kChunk) {
        if (cancel && cancel->load()) {
            if (error) *error = "cancelled";
            return {};
        }
        const std::int64_t n = std::min(kChunk, total - done);
        AudioBuffer b = decoder.readAudio(c.sourceIn + Time::fromSample(done, sampleRate), n, sampleRate, 1);
        if (b.samples.empty()) break;
        all.samples.insert(all.samples.end(), b.samples.begin(), b.samples.end());
    }
    if (all.frames() < kDenoiseFft * 4) {
        if (error) *error = "not enough audio to analyse";
        return {};
    }
    return noiseProfile(all);
}

void registerAudioModules(ModuleRegistry& r) {
    r.add<IAudioEffect>({.id = kEqEffect, .displayName = "Equaliser", .capabilities = {"effect.audio.eq"}},
                        [] { return std::make_shared<Equalizer>(); });
    r.add<IAudioEffect>({.id = kDenoiseEffect, .displayName = "Noise reduction", .capabilities = {"effect.audio.denoise"}},
                        [] { return std::make_shared<Denoise>(); });
}

}  // namespace vstudio::audiofx
