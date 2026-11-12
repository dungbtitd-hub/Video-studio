// Synthetic media backend for tests: no files, no codecs.
// "Files" are named like "synthetic:<seconds>:<w>x<h>" and produce frames
// whose red channel encodes the frame index (mod 256) and a 1 kHz-ish ramp
// as audio, so tests can check exactly which source frame/sample was used.
#pragma once

#include <atomic>
#include <cstdio>
#include <string>

#include "vstudio/media.hpp"

namespace fake {

struct Spec {
    double seconds = 10;
    int w = 64, h = 36;
};

inline bool parse(const std::string& path, Spec& s) {
    const auto pos = path.find("synthetic:");
    if (pos == std::string::npos) return false;
    return std::sscanf(path.c_str() + pos, "synthetic:%lf:%dx%d", &s.seconds, &s.w, &s.h) == 3;
}

inline std::atomic<int> decodes{0};
inline std::atomic<int> seeks{0};

class Decoder final : public vstudio::IMediaDecoder {
public:
    bool open(const std::string& path, std::string* err) override {
        if (!parse(path, spec_)) {
            if (err) *err = "not synthetic";
            return false;
        }
        return true;
    }
    void setMaxOutputSize(int w, int h) override { maxW_ = w; maxH_ = h; }
    std::optional<vstudio::VideoFrame> videoFrameAt(vstudio::Time t) override {
        const vstudio::Rational fps{30, 1};
        const std::int64_t idx = t.toFrame(fps);
        if (idx < 0 || t.seconds() >= spec_.seconds) return std::nullopt;
        if (idx != last_ + 1) ++seeks;
        last_ = idx;
        ++decodes;
        int w = spec_.w, h = spec_.h;
        if (maxW_ > 0 && maxH_ > 0 && (w > maxW_ || h > maxH_)) {
            const double k = std::min(double(maxW_) / w, double(maxH_) / h);
            w = std::max(2, int(w * k));
            h = std::max(2, int(h * k));
        }
        vstudio::VideoFrame f;
        f.allocate(w, h);
        f.pts = vstudio::Time::fromFrame(idx, fps);
        for (std::size_t i = 0; i < f.rgba.size(); i += 4) {
            f.rgba[i] = static_cast<std::uint8_t>(idx % 256);
            f.rgba[i + 1] = 100;
            f.rgba[i + 2] = 50;
            f.rgba[i + 3] = 255;
        }
        return f;
    }
    vstudio::AudioBuffer readAudio(vstudio::Time t, std::int64_t frames, int sr, int ch) override {
        vstudio::AudioBuffer b;
        b.sampleRate = sr;
        b.channels = ch;
        b.samples.resize(static_cast<std::size_t>(frames) * ch);
        const std::int64_t s0 = t.toSample(sr);
        for (std::int64_t i = 0; i < frames; ++i)
            for (int c = 0; c < ch; ++c) b.samples[i * ch + c] = 0.25f * static_cast<float>(((s0 + i) % 48) / 48.0);
        return b;
    }

private:
    Spec spec_;
    int maxW_ = 0, maxH_ = 0;
    std::int64_t last_ = -10;
};

class Encoder final : public vstudio::IMediaEncoder {
public:
    bool open(const std::string&, const vstudio::ExportSettings& s, std::string*) override {
        settings = s;
        return true;
    }
    bool writeVideo(const vstudio::VideoFrame& f, std::string*) override {
        ++videoFrames;
        lastWidth = f.width;
        firstPixels.push_back(f.rgba.empty() ? 0 : f.rgba[0]);
        return true;
    }
    bool writeAudio(const vstudio::AudioBuffer& a, std::string*) override {
        audioFrames += a.frames();
        return true;
    }
    bool finish(std::string*) override {
        finished = true;
        return true;
    }
    vstudio::ExportSettings settings;
    std::int64_t videoFrames = 0, audioFrames = 0;
    int lastWidth = 0;
    bool finished = false;
    std::vector<int> firstPixels;
};

class Backend final : public vstudio::IMediaBackend {
public:
    std::string name() const override { return "fake"; }
    std::optional<vstudio::ProbeResult> probe(const std::string& path, std::string* err) override {
        Spec s;
        if (!parse(path, s)) {
            if (err) *err = "not synthetic";
            return std::nullopt;
        }
        vstudio::ProbeResult r;
        r.hasVideo = r.hasAudio = true;
        r.duration = vstudio::Time::fromSeconds(s.seconds);
        r.width = s.w;
        r.height = s.h;
        r.fps = {30, 1};
        r.videoCodec = "synthetic";
        r.audioCodec = "pcm";
        r.sampleRate = 48000;
        r.channels = 2;
        return r;
    }
    std::unique_ptr<vstudio::IMediaDecoder> createDecoder() override { return std::make_unique<Decoder>(); }
    std::unique_ptr<vstudio::IMediaEncoder> createEncoder() override { return std::make_unique<Encoder>(); }
};

}  // namespace fake
