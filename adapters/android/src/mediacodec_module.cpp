#include "vstudio/android/mediacodec_module.hpp"

#include <android/log.h>
#include <fcntl.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace vstudio::android {

namespace {

constexpr int64_t kTimeoutUs = 10'000;
constexpr int32_t kColorI420 = 19;          // COLOR_FormatYUV420Planar
constexpr int32_t kColorNV12 = 21;          // COLOR_FormatYUV420SemiPlanar
constexpr int32_t kColorFlexible = 0x7F420888;

void logE(const std::string& m) { __android_log_print(ANDROID_LOG_ERROR, "vstudio", "%s", m.c_str()); }

void setErr(std::string* out, const std::string& m) {
    if (out) *out = m;
}

// ---- RAII ----
struct ExtractorDel { void operator()(AMediaExtractor* e) const { AMediaExtractor_delete(e); } };
struct CodecDel {
    void operator()(AMediaCodec* c) const {
        AMediaCodec_stop(c);
        AMediaCodec_delete(c);
    }
};
struct FormatDel { void operator()(AMediaFormat* f) const { AMediaFormat_delete(f); } };
using ExtractorPtr = std::unique_ptr<AMediaExtractor, ExtractorDel>;
using CodecPtr = std::unique_ptr<AMediaCodec, CodecDel>;
using FormatPtr = std::unique_ptr<AMediaFormat, FormatDel>;

class Fd {
public:
    Fd() = default;
    explicit Fd(int fd, bool own) : fd_(fd), own_(own) {}
    Fd(Fd&& o) noexcept : fd_(o.fd_), own_(o.own_) { o.fd_ = -1; }
    Fd& operator=(Fd&& o) noexcept {
        reset();
        fd_ = o.fd_;
        own_ = o.own_;
        o.fd_ = -1;
        return *this;
    }
    ~Fd() { reset(); }
    void reset() {
        if (fd_ >= 0 && own_) ::close(fd_);
        fd_ = -1;
    }
    [[nodiscard]] int get() const { return fd_; }

private:
    int fd_ = -1;
    bool own_ = false;
};

// "fd:<n>" (owned by the caller, we dup it) or a path.
Fd openInput(const std::string& path) {
    if (path.rfind("fd:", 0) == 0) return Fd(::dup(std::atoi(path.c_str() + 3)), true);
    return Fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC), true);
}

ExtractorPtr openExtractor(const std::string& path, Fd& fd, std::string* error) {
    fd = openInput(path);
    if (fd.get() < 0) {
        setErr(error, "cannot open " + path);
        return nullptr;
    }
    struct stat st {};
    ::fstat(fd.get(), &st);
    ExtractorPtr ex(AMediaExtractor_new());
    if (AMediaExtractor_setDataSourceFd(ex.get(), fd.get(), 0, st.st_size) != AMEDIA_OK) {
        setErr(error, "unsupported or corrupt media: " + path);
        return nullptr;
    }
    return ex;
}

std::string mimeOf(AMediaFormat* f) {
    const char* m = nullptr;
    AMediaFormat_getString(f, AMEDIAFORMAT_KEY_MIME, &m);
    return m ? m : "";
}

int32_t getI32(AMediaFormat* f, const char* key, int32_t def = 0) {
    int32_t v = def;
    AMediaFormat_getInt32(f, key, &v);
    return v;
}

int64_t getI64(AMediaFormat* f, const char* key, int64_t def = 0) {
    int64_t v = def;
    AMediaFormat_getInt64(f, key, &v);
    return v;
}

struct Tracks {
    int video = -1, audio = -1;
    FormatPtr vfmt, afmt;
};

Tracks findTracks(AMediaExtractor* ex) {
    Tracks t;
    const size_t n = AMediaExtractor_getTrackCount(ex);
    for (size_t i = 0; i < n; ++i) {
        FormatPtr f(AMediaExtractor_getTrackFormat(ex, i));
        const std::string mime = mimeOf(f.get());
        if (t.video < 0 && mime.rfind("video/", 0) == 0) {
            t.video = static_cast<int>(i);
            t.vfmt = std::move(f);
        } else if (t.audio < 0 && mime.rfind("audio/", 0) == 0) {
            t.audio = static_cast<int>(i);
            t.afmt = std::move(f);
        }
    }
    return t;
}

Time fromUs(int64_t us) { return Time::fromTimebase(us, {1, 1'000'000}); }
int64_t toUs(Time t) { return t.toTimebase({1, 1'000'000}, Rounding::Down); }

std::optional<ProbeResult> probeFile(const std::string& path, std::string* error) {
    Fd fd;
    ExtractorPtr ex = openExtractor(path, fd, error);
    if (!ex) return std::nullopt;
    Tracks t = findTracks(ex.get());
    if (t.video < 0 && t.audio < 0) {
        setErr(error, "no audio or video track");
        return std::nullopt;
    }
    ProbeResult r;
    if (t.video >= 0) {
        AMediaFormat* f = t.vfmt.get();
        r.hasVideo = true;
        r.videoCodec = mimeOf(f);
        r.width = getI32(f, AMEDIAFORMAT_KEY_WIDTH);
        r.height = getI32(f, AMEDIAFORMAT_KEY_HEIGHT);
        r.duration = fromUs(getI64(f, AMEDIAFORMAT_KEY_DURATION));
        const int32_t fps = getI32(f, AMEDIAFORMAT_KEY_FRAME_RATE, 0);
        r.fps = fps > 0 ? Rational{fps, 1} : Rational{30, 1};
        r.rotation = getI32(f, "rotation-degrees", 0);
        r.isStillImage = r.videoCodec == "image/jpeg";  // images come through the picker as stills
    }
    if (t.audio >= 0) {
        AMediaFormat* f = t.afmt.get();
        r.hasAudio = true;
        r.audioCodec = mimeOf(f);
        r.sampleRate = getI32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE);
        r.channels = getI32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT);
        if (!r.hasVideo) r.duration = fromUs(getI64(f, AMEDIAFORMAT_KEY_DURATION));
    }
    return r;
}

// ------------------------------------------------------------------ decoder

// One extractor + codec per stream: the core already keeps a separate decoder
// instance per purpose (preview video / audio / export), so each stays sequential.
class StreamReader {
public:
    bool open(const std::string& path, bool video, std::string* error) {
        ex_ = openExtractor(path, fd_, error);
        if (!ex_) return false;
        Tracks t = findTracks(ex_.get());
        track_ = video ? t.video : t.audio;
        if (track_ < 0) return false;  // stream absent: not an error for the caller
        fmt_ = std::move(video ? t.vfmt : t.afmt);
        AMediaExtractor_selectTrack(ex_.get(), static_cast<size_t>(track_));
        const std::string mime = mimeOf(fmt_.get());
        codec_.reset(AMediaCodec_createDecoderByType(mime.c_str()));
        if (!codec_) {
            setErr(error, "no decoder for " + mime);
            return false;
        }
        if (video) AMediaFormat_setInt32(fmt_.get(), AMEDIAFORMAT_KEY_COLOR_FORMAT, kColorFlexible);
        if (AMediaCodec_configure(codec_.get(), fmt_.get(), nullptr, nullptr, 0) != AMEDIA_OK ||
            AMediaCodec_start(codec_.get()) != AMEDIA_OK) {
            setErr(error, "cannot start decoder for " + mime);
            codec_.reset();
            return false;
        }
        return true;
    }

    [[nodiscard]] bool ok() const { return codec_ != nullptr; }

    void seek(int64_t us) {
        AMediaExtractor_seekTo(ex_.get(), us, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
        AMediaCodec_flush(codec_.get());
        inputEos_ = outputEos_ = false;
    }

    // Next decoded buffer. `fn(data, size, ptsUs)` is called while the buffer is held.
    template <class F>
    bool next(F&& fn) {
        for (int spins = 0; spins < 2000 && !outputEos_; ++spins) {
            feed();
            AMediaCodecBufferInfo info{};
            const ssize_t idx = AMediaCodec_dequeueOutputBuffer(codec_.get(), &info, kTimeoutUs);
            if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                outFmt_.reset(AMediaCodec_getOutputFormat(codec_.get()));
                continue;
            }
            if (idx < 0) continue;  // try again / buffers changed
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) outputEos_ = true;
            size_t cap = 0;
            uint8_t* buf = AMediaCodec_getOutputBuffer(codec_.get(), static_cast<size_t>(idx), &cap);
            if (buf && info.size > 0) fn(buf + info.offset, static_cast<size_t>(info.size), info.presentationTimeUs);
            AMediaCodec_releaseOutputBuffer(codec_.get(), static_cast<size_t>(idx), false);
            if (info.size > 0) return true;
        }
        return false;
    }

    AMediaFormat* outputFormat() {
        if (!outFmt_) outFmt_.reset(AMediaCodec_getOutputFormat(codec_.get()));
        return outFmt_.get();
    }
    AMediaFormat* inputFormat() { return fmt_.get(); }

private:
    void feed() {
        if (inputEos_) return;
        const ssize_t idx = AMediaCodec_dequeueInputBuffer(codec_.get(), 0);
        if (idx < 0) return;
        size_t cap = 0;
        uint8_t* buf = AMediaCodec_getInputBuffer(codec_.get(), static_cast<size_t>(idx), &cap);
        const ssize_t n = AMediaExtractor_readSampleData(ex_.get(), buf, cap);
        if (n < 0) {
            AMediaCodec_queueInputBuffer(codec_.get(), static_cast<size_t>(idx), 0, 0, 0,
                                         AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
            inputEos_ = true;
            return;
        }
        AMediaCodec_queueInputBuffer(codec_.get(), static_cast<size_t>(idx), 0, static_cast<size_t>(n),
                                     static_cast<uint64_t>(AMediaExtractor_getSampleTime(ex_.get())), 0);
        AMediaExtractor_advance(ex_.get());
    }

    Fd fd_;
    ExtractorPtr ex_;
    CodecPtr codec_;
    FormatPtr fmt_, outFmt_;
    int track_ = -1;
    bool inputEos_ = false, outputEos_ = false;
};

// YUV 4:2:0 (I420 or NV12, with stride/slice-height) -> RGBA, box-downscaled to w x h.
void yuvToRgba(const uint8_t* src, int srcW, int srcH, int stride, int sliceH, bool nv12, VideoFrame& out, int w, int h) {
    out.allocate(w, h);
    const uint8_t* yPlane = src;
    const uint8_t* uvPlane = src + static_cast<size_t>(stride) * sliceH;
    const uint8_t* vPlane = uvPlane + static_cast<size_t>(stride / 2) * (sliceH / 2);
    for (int y = 0; y < h; ++y) {
        const int sy = std::min(srcH - 1, static_cast<int>((y + 0.5) * srcH / h));
        uint8_t* d = out.rgba.data() + static_cast<size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            const int sx = std::min(srcW - 1, static_cast<int>((x + 0.5) * srcW / w));
            const int Y = yPlane[static_cast<size_t>(sy) * stride + sx];
            int U, V;
            if (nv12) {
                const uint8_t* uv = uvPlane + static_cast<size_t>(sy / 2) * stride + (sx & ~1);
                U = uv[0];
                V = uv[1];
            } else {
                U = uvPlane[static_cast<size_t>(sy / 2) * (stride / 2) + sx / 2];
                V = vPlane[static_cast<size_t>(sy / 2) * (stride / 2) + sx / 2];
            }
            // BT.601 limited range
            const int c = Y - 16, dd = U - 128, e = V - 128;
            d[0] = static_cast<uint8_t>(std::clamp((298 * c + 409 * e + 128) >> 8, 0, 255));
            d[1] = static_cast<uint8_t>(std::clamp((298 * c - 100 * dd - 208 * e + 128) >> 8, 0, 255));
            d[2] = static_cast<uint8_t>(std::clamp((298 * c + 516 * dd + 128) >> 8, 0, 255));
            d[3] = 255;
            d += 4;
        }
    }
}

class Decoder final : public IMediaDecoder {
public:
    bool open(const std::string& path, std::string* error) override {
        path_ = path;
        std::string err;
        auto pr = probeFile(path, &err);
        if (!pr) {
            setErr(error, err);
            return false;
        }
        probe_ = *pr;
        return true;
    }

    void setMaxOutputSize(int w, int h) override {
        maxW_ = w;
        maxH_ = h;
    }

    std::optional<VideoFrame> videoFrameAt(Time t) override {
        if (!probe_.hasVideo) return std::nullopt;
        if (!video_.ok() && !video_.open(path_, true, nullptr)) return std::nullopt;
        const int64_t target = toUs(t);
        if (cur_ && target >= curPts_ && (!pendingValid_ || target < pendingPts_)) return *cur_;
        if (!cur_ || target < curPts_ || target - curPts_ > 2'000'000) {
            video_.seek(target);
            cur_.reset();
            pendingValid_ = false;
        }
        for (;;) {
            if (pendingValid_) {
                if (pendingPts_ <= target || !cur_) {
                    cur_ = std::make_shared<VideoFrame>(std::move(pending_));
                    curPts_ = pendingPts_;
                    pendingValid_ = false;
                } else {
                    break;
                }
            }
            bool got = video_.next([&](const uint8_t* data, size_t, int64_t pts) {
                convert(data, pending_);
                pending_.pts = fromUs(pts);
                pendingPts_ = pts;
            });
            if (!got) break;
            pendingValid_ = true;
        }
        if (!cur_) return std::nullopt;
        return *cur_;
    }

    AudioBuffer readAudio(Time t, std::int64_t frames, int sr, int ch) override {
        AudioBuffer out;
        out.sampleRate = sr;
        out.channels = ch;
        out.samples.assign(static_cast<size_t>(frames) * ch, 0.0f);
        if (!probe_.hasAudio || frames <= 0) return out;
        if (!audio_.ok() && !audio_.open(path_, false, nullptr)) return out;
        const int64_t want = t.toSample(sr);
        const int64_t have = static_cast<int64_t>(fifo_.size()) / ch;
        if (fifoStart_ < 0 || want < fifoStart_ || want > fifoStart_ + have + sr) {
            audio_.seek(toUs(t));
            fifo_.clear();
            fifoStart_ = -1;
        }
        while (fifoStart_ < 0 || fifoStart_ + static_cast<int64_t>(fifo_.size()) / ch < want + frames) {
            bool got = audio_.next([&](const uint8_t* data, size_t size, int64_t pts) { appendPcm(data, size, pts, sr, ch); });
            if (!got) break;
        }
        if (fifoStart_ < 0) return out;
        const int64_t total = static_cast<int64_t>(fifo_.size()) / ch;
        const int64_t from = std::max(want, fifoStart_), to = std::min(want + frames, fifoStart_ + total);
        for (int64_t s = from; s < to; ++s)
            std::memcpy(&out.samples[static_cast<size_t>(s - want) * ch], &fifo_[static_cast<size_t>(s - fifoStart_) * ch],
                        sizeof(float) * ch);
        const int64_t drop = std::clamp<int64_t>(want + frames - fifoStart_, 0, total);
        fifo_.erase(fifo_.begin(), fifo_.begin() + static_cast<ptrdiff_t>(drop) * ch);
        fifoStart_ += drop;
        return out;
    }

private:
    void convert(const uint8_t* data, VideoFrame& out) {
        AMediaFormat* f = video_.outputFormat();
        const int w = getI32(f, AMEDIAFORMAT_KEY_WIDTH, probe_.width);
        const int h = getI32(f, AMEDIAFORMAT_KEY_HEIGHT, probe_.height);
        const int stride = std::max(w, getI32(f, AMEDIAFORMAT_KEY_STRIDE, w));
        const int slice = std::max(h, getI32(f, "slice-height", h));
        const int color = getI32(f, AMEDIAFORMAT_KEY_COLOR_FORMAT, kColorNV12);
        int ow = w, oh = h;
        if (maxW_ > 0 && maxH_ > 0 && (w > maxW_ || h > maxH_)) {
            const double k = std::min(static_cast<double>(maxW_) / w, static_cast<double>(maxH_) / h);
            ow = std::max(2, static_cast<int>(w * k) & ~1);
            oh = std::max(2, static_cast<int>(h * k) & ~1);
        }
        // Flexible output is NV12 on almost all devices; I420 is the other common layout.
        yuvToRgba(data, w, h, stride, slice, color != kColorI420, out, ow, oh);
    }

    // 16-bit PCM from the decoder -> float, remixed to `ch`, linearly resampled to `sr`.
    void appendPcm(const uint8_t* data, size_t size, int64_t ptsUs, int sr, int ch) {
        AMediaFormat* f = audio_.outputFormat();
        const int inRate = getI32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, probe_.sampleRate);
        const int inCh = std::max(1, getI32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, probe_.channels));
        const auto* pcm = reinterpret_cast<const int16_t*>(data);
        const int64_t inFrames = static_cast<int64_t>(size / sizeof(int16_t)) / inCh;
        if (fifoStart_ < 0) fifoStart_ = fromUs(ptsUs).toSample(sr);
        const int64_t outFrames = inFrames * sr / std::max(1, inRate);
        for (int64_t i = 0; i < outFrames; ++i) {
            const double pos = static_cast<double>(i) * inRate / sr;
            const int64_t j = std::min<int64_t>(static_cast<int64_t>(pos), inFrames - 1);
            const int64_t j1 = std::min<int64_t>(j + 1, inFrames - 1);
            const float fr = static_cast<float>(pos - j);
            for (int c = 0; c < ch; ++c) {
                const int sc = std::min(c, inCh - 1);
                const float a = pcm[j * inCh + sc] / 32768.0f, b = pcm[j1 * inCh + sc] / 32768.0f;
                fifo_.push_back(a + (b - a) * fr);
            }
        }
    }

    std::string path_;
    ProbeResult probe_;
    int maxW_ = 0, maxH_ = 0;
    StreamReader video_, audio_;
    std::shared_ptr<VideoFrame> cur_;
    int64_t curPts_ = 0;
    VideoFrame pending_;
    int64_t pendingPts_ = 0;
    bool pendingValid_ = false;
    std::vector<float> fifo_;
    int64_t fifoStart_ = -1;
};

// ------------------------------------------------------------------ encoder

class Encoder final : public IMediaEncoder {
public:
    ~Encoder() override {
        if (muxer_ && !finished_) finish(nullptr);
        if (muxer_) AMediaMuxer_delete(muxer_);
        if (fd_ >= 0) ::close(fd_);
    }

    bool open(const std::string& path, const ExportSettings& s, std::string* error) override {
        s_ = s;
        fd_ = ::open(path.c_str(), O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0644);
        if (fd_ < 0) return fail(error, "cannot write " + path);
        muxer_ = AMediaMuxer_new(fd_, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
        if (!muxer_) return fail(error, "cannot create MP4 muxer");

        FormatPtr vf(AMediaFormat_new());
        AMediaFormat_setString(vf.get(), AMEDIAFORMAT_KEY_MIME, "video/avc");
        AMediaFormat_setInt32(vf.get(), AMEDIAFORMAT_KEY_WIDTH, s.width);
        AMediaFormat_setInt32(vf.get(), AMEDIAFORMAT_KEY_HEIGHT, s.height);
        AMediaFormat_setInt32(vf.get(), AMEDIAFORMAT_KEY_COLOR_FORMAT, kColorNV12);
        AMediaFormat_setInt32(vf.get(), AMEDIAFORMAT_KEY_BIT_RATE, static_cast<int32_t>(s.videoBitrate));
        AMediaFormat_setInt32(vf.get(), AMEDIAFORMAT_KEY_FRAME_RATE,
                              static_cast<int32_t>(std::lround(s.fps.toDouble())));
        AMediaFormat_setInt32(vf.get(), AMEDIAFORMAT_KEY_I_FRAME_INTERVAL,
                              s.gopSize > 0 ? std::max(1, static_cast<int>(s.gopSize / std::max(1.0, s.fps.toDouble()))) : 2);
        venc_.reset(AMediaCodec_createEncoderByType("video/avc"));
        if (!venc_ || AMediaCodec_configure(venc_.get(), vf.get(), nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) != AMEDIA_OK ||
            AMediaCodec_start(venc_.get()) != AMEDIA_OK)
            return fail(error, "this device has no usable H.264 encoder for " + std::to_string(s.width) + "x" +
                                   std::to_string(s.height));

        if (s.includeAudio) {
            FormatPtr af(AMediaFormat_new());
            AMediaFormat_setString(af.get(), AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
            AMediaFormat_setInt32(af.get(), AMEDIAFORMAT_KEY_SAMPLE_RATE, s.sampleRate);
            AMediaFormat_setInt32(af.get(), AMEDIAFORMAT_KEY_CHANNEL_COUNT, s.channels);
            AMediaFormat_setInt32(af.get(), AMEDIAFORMAT_KEY_BIT_RATE, static_cast<int32_t>(s.audioBitrate));
            AMediaFormat_setInt32(af.get(), AMEDIAFORMAT_KEY_AAC_PROFILE, 2);  // AAC-LC
            aenc_.reset(AMediaCodec_createEncoderByType("audio/mp4a-latm"));
            if (!aenc_ || AMediaCodec_configure(aenc_.get(), af.get(), nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) != AMEDIA_OK ||
                AMediaCodec_start(aenc_.get()) != AMEDIA_OK)
                return fail(error, "this device has no AAC encoder");
        }
        nv12_.resize(static_cast<size_t>(s.width) * s.height * 3 / 2);
        return true;
    }

    bool writeVideo(const VideoFrame& f, std::string* error) override {
        if (f.width != s_.width || f.height != s_.height) return fail(error, "frame size mismatch");
        rgbaToNv12(f);
        const int64_t pts = toUs(Time::fromFrame(videoIndex_++, s_.fps));
        return queue(venc_.get(), vTrack_, nv12_.data(), nv12_.size(), pts, false, error);
    }

    bool writeAudio(const AudioBuffer& a, std::string* error) override {
        if (!aenc_) return true;
        pcm_.reserve(pcm_.size() + a.samples.size());
        for (float v : a.samples) pcm_.push_back(static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
        // Feed in 1024-frame chunks.
        const size_t chunk = 1024 * static_cast<size_t>(s_.channels);
        size_t off = 0;
        while (pcm_.size() - off >= chunk) {
            const int64_t pts = toUs(Time::fromSample(audioFrames_, s_.sampleRate));
            if (!queue(aenc_.get(), aTrack_, reinterpret_cast<const uint8_t*>(pcm_.data() + off), chunk * sizeof(int16_t), pts,
                       false, error))
                return false;
            audioFrames_ += 1024;
            off += chunk;
        }
        pcm_.erase(pcm_.begin(), pcm_.begin() + static_cast<ptrdiff_t>(off));
        return true;
    }

    bool finish(std::string* error) override {
        if (finished_) return true;
        finished_ = true;
        bool ok = true;
        if (aenc_) {
            const int64_t pts = toUs(Time::fromSample(audioFrames_, s_.sampleRate));
            ok = queue(aenc_.get(), aTrack_, reinterpret_cast<const uint8_t*>(pcm_.data()), pcm_.size() * sizeof(int16_t), pts,
                       true, error);
        }
        ok = queue(venc_.get(), vTrack_, nullptr, 0, toUs(Time::fromFrame(videoIndex_, s_.fps)), true, error) && ok;
        if (started_) AMediaMuxer_stop(muxer_);
        else return fail(error, "nothing was encoded");
        return ok;
    }

private:
    bool fail(std::string* e, const std::string& m) {
        setErr(e, m);
        logE(m);
        return false;
    }

    void rgbaToNv12(const VideoFrame& f) {
        const int w = f.width, h = f.height;
        uint8_t* Y = nv12_.data();
        uint8_t* UV = Y + static_cast<size_t>(w) * h;
        for (int y = 0; y < h; ++y) {
            const uint8_t* p = f.rgba.data() + static_cast<size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x, p += 4)
                Y[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(((66 * p[0] + 129 * p[1] + 25 * p[2] + 128) >> 8) + 16);
        }
        for (int y = 0; y < h; y += 2) {
            for (int x = 0; x < w; x += 2) {
                const uint8_t* p = f.rgba.data() + (static_cast<size_t>(y) * w + x) * 4;
                const int r = p[0], g = p[1], b = p[2];
                UV[static_cast<size_t>(y / 2) * w + x] = static_cast<uint8_t>(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
                UV[static_cast<size_t>(y / 2) * w + x + 1] = static_cast<uint8_t>(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
            }
        }
    }

    // Queues input (possibly larger than one codec buffer) and drains output to the muxer.
    bool queue(AMediaCodec* c, ssize_t& track, const uint8_t* data, size_t size, int64_t ptsUs, bool eos, std::string* error) {
        size_t off = 0;
        int guard = 0;
        do {
            const ssize_t idx = AMediaCodec_dequeueInputBuffer(c, kTimeoutUs);
            if (idx >= 0) {
                size_t cap = 0;
                uint8_t* buf = AMediaCodec_getInputBuffer(c, static_cast<size_t>(idx), &cap);
                const size_t n = std::min(cap, size - off);
                if (n) std::memcpy(buf, data + off, n);
                off += n;
                const bool last = off >= size;
                AMediaCodec_queueInputBuffer(c, static_cast<size_t>(idx), 0, n, static_cast<uint64_t>(ptsUs),
                                             eos && last ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0);
                if (last) break;
            }
            if (!drain(c, track, false, error)) return false;
        } while (++guard < 10000);
        return drain(c, track, eos, error);
    }

    bool drain(AMediaCodec* c, ssize_t& track, bool untilEos, std::string* error) {
        for (int spins = 0; spins < 5000; ++spins) {
            AMediaCodecBufferInfo info{};
            const ssize_t idx = AMediaCodec_dequeueOutputBuffer(c, &info, untilEos ? kTimeoutUs : 0);
            if (idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                if (!untilEos) return true;
                continue;
            }
            if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                FormatPtr f(AMediaCodec_getOutputFormat(c));
                track = AMediaMuxer_addTrack(muxer_, f.get());
                ++tracksAdded_;
                const int needed = aenc_ ? 2 : 1;
                if (tracksAdded_ == needed && !started_) {
                    if (AMediaMuxer_start(muxer_) != AMEDIA_OK) return fail(error, "cannot start MP4 muxer");
                    started_ = true;
                    for (auto& p : held_) AMediaMuxer_writeSampleData(muxer_, static_cast<size_t>(p.track), p.data.data(), &p.info);
                    held_.clear();
                }
                continue;
            }
            if (idx < 0) continue;
            size_t cap = 0;
            uint8_t* buf = AMediaCodec_getOutputBuffer(c, static_cast<size_t>(idx), &cap);
            const bool config = info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG;
            if (!config && info.size > 0 && buf) {
                if (started_) {
                    AMediaMuxer_writeSampleData(muxer_, static_cast<size_t>(track), buf + info.offset, &info);
                } else {
                    // The other track's format is not known yet: keep the packet until the muxer starts.
                    Held h{track, std::vector<uint8_t>(buf + info.offset, buf + info.offset + info.size), info};
                    h.info.offset = 0;
                    held_.push_back(std::move(h));
                }
            }
            AMediaCodec_releaseOutputBuffer(c, static_cast<size_t>(idx), false);
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) return true;
        }
        return !untilEos || fail(error, "encoder did not finish");
    }

    struct Held {
        ssize_t track;
        std::vector<uint8_t> data;
        AMediaCodecBufferInfo info;
    };

    ExportSettings s_;
    int fd_ = -1;
    AMediaMuxer* muxer_ = nullptr;
    CodecPtr venc_, aenc_;
    ssize_t vTrack_ = -1, aTrack_ = -1;
    int tracksAdded_ = 0;
    bool started_ = false, finished_ = false;
    std::vector<Held> held_;
    std::vector<uint8_t> nv12_;
    std::vector<int16_t> pcm_;
    int64_t videoIndex_ = 0, audioFrames_ = 0;
};

class Backend final : public IMediaBackend {
public:
    std::string name() const override { return "android.mediacodec"; }
    std::optional<ProbeResult> probe(const std::string& path, std::string* error) override { return probeFile(path, error); }
    std::unique_ptr<IMediaDecoder> createDecoder() override { return std::make_unique<Decoder>(); }
    std::unique_ptr<IMediaEncoder> createEncoder() override { return std::make_unique<Encoder>(); }
};

}  // namespace

std::shared_ptr<IMediaBackend> createBackend() { return std::make_shared<Backend>(); }

void registerModule(ModuleRegistry& r, int priority) {
    r.add<IMediaBackend>({.id = "android.mediacodec",
                          .displayName = "Android MediaCodec",
                          .capabilities = {"probe", "decode", "encode.h264", "encode.aac"},
                          .platforms = {"android"},
                          .priority = priority},
                         [] { return createBackend(); });
}

}  // namespace vstudio::android
