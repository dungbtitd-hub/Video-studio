#include "vstudio/ffmpeg/ffmpeg_module.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace vstudio::ffmpeg {

namespace {

std::string avErr(int e) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(e, buf, sizeof buf);
    return buf;
}

void setErr(std::string* out, const std::string& msg) {
    if (out) *out = msg;
}

struct FormatCloser {
    void operator()(AVFormatContext* c) const { avformat_close_input(&c); }
};
struct CodecFreer {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct FrameFreer {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct PacketFreer {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct SwsFreer {
    void operator()(SwsContext* s) const { sws_freeContext(s); }
};
struct SwrFreer {
    void operator()(SwrContext* s) const { swr_free(&s); }
};
using FormatPtr = std::unique_ptr<AVFormatContext, FormatCloser>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecFreer>;
using FramePtr = std::unique_ptr<AVFrame, FrameFreer>;
using PacketPtr = std::unique_ptr<AVPacket, PacketFreer>;
using SwsPtr = std::unique_ptr<SwsContext, SwsFreer>;
using SwrPtr = std::unique_ptr<SwrContext, SwrFreer>;

Rational toRational(AVRational r) { return {r.num, r.den}; }

bool isImageCodec(AVCodecID id) {
    switch (id) {
        case AV_CODEC_ID_MJPEG:
        case AV_CODEC_ID_PNG:
        case AV_CODEC_ID_BMP:
        case AV_CODEC_ID_TIFF:
        case AV_CODEC_ID_WEBP:
        case AV_CODEC_ID_GIF:
            return true;
        default:
            return false;
    }
}

FormatPtr openInput(const std::string& path, std::string* error) {
    AVFormatContext* raw = nullptr;
    int e = avformat_open_input(&raw, path.c_str(), nullptr, nullptr);
    if (e < 0) {
        setErr(error, "cannot open " + path + ": " + avErr(e));
        return nullptr;
    }
    FormatPtr fmt(raw);
    if ((e = avformat_find_stream_info(fmt.get(), nullptr)) < 0) {
        setErr(error, "cannot read stream info: " + avErr(e));
        return nullptr;
    }
    return fmt;
}

CodecPtr openDecoder(AVStream* st, bool video, std::string* error) {
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        setErr(error, std::string("no decoder for ") + avcodec_get_name(st->codecpar->codec_id));
        return nullptr;
    }
    CodecPtr ctx(avcodec_alloc_context3(codec));
    avcodec_parameters_to_context(ctx.get(), st->codecpar);
    ctx->pkt_timebase = st->time_base;
    if (video) {
        ctx->thread_count = 0;  // auto: decode on all cores
        ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }
    const int e = avcodec_open2(ctx.get(), codec, nullptr);
    if (e < 0) {
        setErr(error, "cannot open decoder: " + avErr(e));
        return nullptr;
    }
    return ctx;
}

// ------------------------------------------------------------------ probe

std::optional<ProbeResult> probeFile(const std::string& path, std::string* error) {
    FormatPtr fmt = openInput(path, error);
    if (!fmt) return std::nullopt;
    ProbeResult r;
    const int vi = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int ai = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (vi < 0 && ai < 0) {
        setErr(error, "no audio or video stream");
        return std::nullopt;
    }
    if (fmt->duration > 0) r.duration = Time::fromTimebase(fmt->duration, {1, AV_TIME_BASE});
    if (vi >= 0) {
        AVStream* st = fmt->streams[vi];
        r.hasVideo = true;
        r.videoCodec = avcodec_get_name(st->codecpar->codec_id);
        r.width = st->codecpar->width;
        r.height = st->codecpar->height;
        const AVRational avg = st->avg_frame_rate, real = st->r_frame_rate;
        r.fps = avg.num > 0 && avg.den > 0 ? toRational(avg) : toRational(real);
        if (avg.num > 0 && real.num > 0) {
            const double a = av_q2d(avg), b = av_q2d(real);
            r.vfr = std::abs(a - b) / std::max(a, b) > 0.01;  // phones often record VFR
        }
        const bool singleFrame = st->nb_frames <= 1 && fmt->duration <= 0;
        r.isStillImage = isImageCodec(st->codecpar->codec_id) &&
                         (singleFrame || std::strstr(fmt->iformat->name, "image2") || std::strstr(fmt->iformat->name, "_pipe"));
        // Prefer the video stream's own length: the container duration also
        // counts audio priming/padding and would leave a black tail frame.
        if (st->duration > 0 && st->duration != AV_NOPTS_VALUE)
            r.duration = Time::fromTimebase(st->duration, toRational(st->time_base));
        if (r.isStillImage) r.duration = Time::zero();
        if (r.fps.num <= 0) r.fps = {30, 1};
    }
    if (ai >= 0) {
        AVStream* st = fmt->streams[ai];
        r.hasAudio = true;
        r.audioCodec = avcodec_get_name(st->codecpar->codec_id);
        r.sampleRate = st->codecpar->sample_rate;
        r.channels = st->codecpar->ch_layout.nb_channels;
    }
    return r;
}

// ------------------------------------------------------------------ decoder

class Decoder final : public IMediaDecoder {
public:
    bool open(const std::string& path, std::string* error) override {
        fmt_ = openInput(path, error);
        if (!fmt_) return false;
        vi_ = av_find_best_stream(fmt_.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        ai_ = av_find_best_stream(fmt_.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (vi_ >= 0 && !(vctx_ = openDecoder(fmt_->streams[vi_], true, error))) return false;
        if (ai_ >= 0 && !(actx_ = openDecoder(fmt_->streams[ai_], false, error))) return false;
        pkt_.reset(av_packet_alloc());
        // Discard everything until we know what this instance is used for.
        for (unsigned i = 0; i < fmt_->nb_streams; ++i) fmt_->streams[i]->discard = AVDISCARD_ALL;
        return vi_ >= 0 || ai_ >= 0;
    }

    void setMaxOutputSize(int w, int h) override {
        maxW_ = w;
        maxH_ = h;
    }

    std::optional<VideoFrame> videoFrameAt(Time t) override {
        if (vi_ < 0) return std::nullopt;
        useStream(vi_);
        AVStream* st = fmt_->streams[vi_];
        const Rational tb = toRational(st->time_base);
        const std::int64_t start = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
        const std::int64_t target = start + t.toTimebase(tb, Rounding::Down);

        // Same frame as last time (e.g. 24 fps source on a 30 fps timeline): reuse the conversion.
        const bool haveCur = cur_ && curPts_ != AV_NOPTS_VALUE;
        const bool inCur = haveCur && target >= curPts_ && (!pending_ || target < pendingPts_);
        if (inCur && converted_) return *converted_;

        // Seek only when going backwards or jumping far ahead; sequential reads just keep decoding.
        const std::int64_t farAhead = Time::fromSeconds(2).toTimebase(tb);
        if (!haveCur || target < curPts_ || target - curPts_ > farAhead) {
            av_seek_frame(fmt_.get(), vi_, target, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(vctx_.get());
            cur_.reset();
            pending_.reset();
            curPts_ = pendingPts_ = AV_NOPTS_VALUE;
            veof_ = false;
        }
        converted_.reset();
        for (;;) {
            if (pending_) {
                if (pendingPts_ <= target || !cur_) {
                    cur_ = std::move(pending_);
                    curPts_ = pendingPts_;
                    pending_.reset();
                } else {
                    break;  // pending is in the future: cur is the frame on screen
                }
            }
            if (veof_) break;
            FramePtr f = decodeNext(vctx_.get(), vi_, veof_);
            if (!f) continue;
            pendingPts_ = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
            if (pendingPts_ == AV_NOPTS_VALUE) pendingPts_ = curPts_ == AV_NOPTS_VALUE ? start : curPts_ + 1;
            pending_ = std::move(f);
        }
        if (!cur_) return std::nullopt;
        converted_ = convert(*cur_, Time::fromTimebase(curPts_ - start, tb));
        return *converted_;
    }

    AudioBuffer readAudio(Time t, std::int64_t frames, int sr, int ch) override {
        AudioBuffer out;
        out.sampleRate = sr;
        out.channels = ch;
        out.samples.assign(static_cast<std::size_t>(frames) * ch, 0.0f);
        if (ai_ < 0 || frames <= 0) return out;
        useStream(ai_);
        AVStream* st = fmt_->streams[ai_];
        const std::int64_t want = t.toSample(sr);
        if (!swr_ || sr != outRate_ || ch != outCh_) {
            if (!setupSwr(sr, ch)) return out;
            fifoStart_ = -1;
        }
        // Continuous playback: the FIFO already starts at `want`. Otherwise seek.
        const std::int64_t fifoFrames = static_cast<std::int64_t>(fifo_.size()) / ch;
        if (fifoStart_ < 0 || want < fifoStart_ || want > fifoStart_ + fifoFrames + sr) {
            const Rational tb = toRational(st->time_base);
            const std::int64_t start = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
            av_seek_frame(fmt_.get(), ai_, start + t.toTimebase(tb, Rounding::Down), AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(actx_.get());
            setupSwr(sr, ch);
            fifo_.clear();
            fifoStart_ = -1;
            aeof_ = false;
        }
        while (!aeof_ && (fifoStart_ < 0 || fifoStart_ + static_cast<std::int64_t>(fifo_.size()) / ch < want + frames)) {
            FramePtr f = decodeNext(actx_.get(), ai_, aeof_);
            if (!f) continue;
            const Rational tb = toRational(st->time_base);
            const std::int64_t start = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
            const std::int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
            if (fifoStart_ < 0) fifoStart_ = pts == AV_NOPTS_VALUE ? want : Time::fromTimebase(pts - start, tb).toSample(sr);
            const int maxOut = swr_get_out_samples(swr_.get(), f->nb_samples);
            std::vector<float> tmp(static_cast<std::size_t>(std::max(0, maxOut)) * ch);
            auto* dst = reinterpret_cast<std::uint8_t*>(tmp.data());
            const int got = swr_convert(swr_.get(), &dst, maxOut, const_cast<const std::uint8_t**>(f->extended_data), f->nb_samples);
            if (got > 0) fifo_.insert(fifo_.end(), tmp.begin(), tmp.begin() + static_cast<std::ptrdiff_t>(got) * ch);
        }
        if (fifoStart_ < 0) return out;
        // Copy the overlapping part, then drop what is behind `want + frames`.
        const std::int64_t have = static_cast<std::int64_t>(fifo_.size()) / ch;
        const std::int64_t from = std::max<std::int64_t>(want, fifoStart_);
        const std::int64_t to = std::min<std::int64_t>(want + frames, fifoStart_ + have);
        for (std::int64_t s = from; s < to; ++s)
            std::memcpy(&out.samples[static_cast<std::size_t>(s - want) * ch],
                        &fifo_[static_cast<std::size_t>(s - fifoStart_) * ch], sizeof(float) * ch);
        const std::int64_t drop = std::clamp<std::int64_t>(want + frames - fifoStart_, 0, have);
        fifo_.erase(fifo_.begin(), fifo_.begin() + static_cast<std::ptrdiff_t>(drop) * ch);
        fifoStart_ += drop;
        return out;
    }

private:
    void useStream(int idx) {
        if (active_ == idx) return;
        active_ = idx;
        for (unsigned i = 0; i < fmt_->nb_streams; ++i)
            fmt_->streams[i]->discard = static_cast<int>(i) == idx ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    }

    // Next decoded frame of `stream`, or nullptr (check `eof`).
    FramePtr decodeNext(AVCodecContext* ctx, int stream, bool& eof) {
        FramePtr f(av_frame_alloc());
        for (;;) {
            int e = avcodec_receive_frame(ctx, f.get());
            if (e == 0) return f;
            if (e == AVERROR_EOF) {
                eof = true;
                return nullptr;
            }
            if (e != AVERROR(EAGAIN)) {
                eof = true;
                return nullptr;
            }
            e = av_read_frame(fmt_.get(), pkt_.get());
            if (e < 0) {
                avcodec_send_packet(ctx, nullptr);  // drain
                continue;
            }
            if (pkt_->stream_index == stream) avcodec_send_packet(ctx, pkt_.get());
            av_packet_unref(pkt_.get());
        }
    }

    std::shared_ptr<VideoFrame> convert(const AVFrame& f, Time pts) {
        int w = f.width, h = f.height;
        if (maxW_ > 0 && maxH_ > 0 && (w > maxW_ || h > maxH_)) {
            const double k = std::min(static_cast<double>(maxW_) / w, static_cast<double>(maxH_) / h);
            w = std::max(2, static_cast<int>(w * k) & ~1);
            h = std::max(2, static_cast<int>(h * k) & ~1);
        }
        if (!sws_ || swsSrcW_ != f.width || swsSrcH_ != f.height || swsSrcFmt_ != f.format || swsW_ != w || swsH_ != h) {
            const int flags = (w != f.width) ? SWS_FAST_BILINEAR : SWS_POINT;
            sws_.reset(sws_getContext(f.width, f.height, static_cast<AVPixelFormat>(f.format), w, h, AV_PIX_FMT_RGBA, flags,
                                      nullptr, nullptr, nullptr));
            swsSrcW_ = f.width;
            swsSrcH_ = f.height;
            swsSrcFmt_ = f.format;
            swsW_ = w;
            swsH_ = h;
        }
        auto out = std::make_shared<VideoFrame>();
        out->allocate(w, h);
        out->pts = pts;
        if (!sws_) return out;
        std::uint8_t* dst[1] = {out->rgba.data()};
        const int stride[1] = {w * 4};
        sws_scale(sws_.get(), f.data, f.linesize, 0, f.height, dst, stride);
        return out;
    }

    bool setupSwr(int sr, int ch) {
        SwrContext* s = nullptr;
        AVChannelLayout outLayout;
        av_channel_layout_default(&outLayout, ch);
        AVChannelLayout inLayout = actx_->ch_layout;
        AVChannelLayout fallback;
        if (inLayout.order == AV_CHANNEL_ORDER_UNSPEC) {
            av_channel_layout_default(&fallback, inLayout.nb_channels);
            inLayout = fallback;
        }
        if (swr_alloc_set_opts2(&s, &outLayout, AV_SAMPLE_FMT_FLT, sr, &inLayout, actx_->sample_fmt, actx_->sample_rate, 0,
                                nullptr) < 0 ||
            swr_init(s) < 0) {
            swr_free(&s);
            return false;
        }
        swr_.reset(s);
        outRate_ = sr;
        outCh_ = ch;
        return true;
    }

    FormatPtr fmt_;
    CodecPtr vctx_, actx_;
    PacketPtr pkt_;
    int vi_ = -1, ai_ = -1, active_ = -2;
    int maxW_ = 0, maxH_ = 0;
    // video state
    FramePtr cur_, pending_;
    std::int64_t curPts_ = AV_NOPTS_VALUE, pendingPts_ = AV_NOPTS_VALUE;
    bool veof_ = false;
    std::shared_ptr<VideoFrame> converted_;
    SwsPtr sws_;
    int swsSrcW_ = 0, swsSrcH_ = 0, swsSrcFmt_ = -1, swsW_ = 0, swsH_ = 0;
    // audio state
    SwrPtr swr_;
    int outRate_ = 0, outCh_ = 0;
    std::vector<float> fifo_;
    std::int64_t fifoStart_ = -1;
    bool aeof_ = false;
};

// ------------------------------------------------------------------ encoder

class Encoder final : public IMediaEncoder {
public:
    ~Encoder() override {
        if (fmt_ && !finished_) finish(nullptr);
        if (fmt_) {
            if (!(fmt_->oformat->flags & AVFMT_NOFILE) && fmt_->pb) avio_closep(&fmt_->pb);
            avformat_free_context(fmt_);
        }
    }

    bool open(const std::string& path, const ExportSettings& s, std::string* error) override {
        s_ = s;
        int e = avformat_alloc_output_context2(&fmt_, nullptr, s.container.c_str(), path.c_str());
        if (e < 0 || !fmt_) return fail(error, "cannot create output: " + avErr(e));

        // Video
        const AVCodec* vc = nullptr;
        if (s.videoCodec == "h264") {
            vc = avcodec_find_encoder_by_name("libx264");
            if (!vc) vc = avcodec_find_encoder(AV_CODEC_ID_H264);
        }
        if (!vc) vc = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
        if (!vc) return fail(error, "no video encoder available");
        vst_ = avformat_new_stream(fmt_, nullptr);
        vctx_.reset(avcodec_alloc_context3(vc));
        vctx_->width = s.width;
        vctx_->height = s.height;
        vctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        vctx_->time_base = AVRational{static_cast<int>(s.fps.den), static_cast<int>(s.fps.num)};
        vctx_->framerate = AVRational{static_cast<int>(s.fps.num), static_cast<int>(s.fps.den)};
        vctx_->bit_rate = s.videoBitrate;
        vctx_->thread_count = 0;
        if (s.gopSize > 0) vctx_->gop_size = s.gopSize;
        if (std::strcmp(vc->name, "libx264") == 0) {
            av_opt_set(vctx_->priv_data, "preset", s.fastPreset ? "ultrafast" : "veryfast", 0);
        }
        if (fmt_->oformat->flags & AVFMT_GLOBALHEADER) vctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if ((e = avcodec_open2(vctx_.get(), vc, nullptr)) < 0) return fail(error, "cannot open video encoder: " + avErr(e));
        avcodec_parameters_from_context(vst_->codecpar, vctx_.get());
        vst_->time_base = vctx_->time_base;
        vst_->avg_frame_rate = vctx_->framerate;

        // Audio
        if (s.includeAudio) {
            const AVCodec* ac = avcodec_find_encoder(AV_CODEC_ID_AAC);
            if (!ac) return fail(error, "no AAC encoder available");
            ast_ = avformat_new_stream(fmt_, nullptr);
            actx_.reset(avcodec_alloc_context3(ac));
            actx_->sample_fmt = AV_SAMPLE_FMT_FLTP;
            actx_->sample_rate = s.sampleRate;
            av_channel_layout_default(&actx_->ch_layout, s.channels);
            actx_->bit_rate = s.audioBitrate;
            actx_->time_base = AVRational{1, s.sampleRate};
            if (fmt_->oformat->flags & AVFMT_GLOBALHEADER) actx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            if ((e = avcodec_open2(actx_.get(), ac, nullptr)) < 0) return fail(error, "cannot open audio encoder: " + avErr(e));
            avcodec_parameters_from_context(ast_->codecpar, actx_.get());
            ast_->time_base = actx_->time_base;
        }

        if (!(fmt_->oformat->flags & AVFMT_NOFILE) && (e = avio_open(&fmt_->pb, path.c_str(), AVIO_FLAG_WRITE)) < 0)
            return fail(error, "cannot write " + path + ": " + avErr(e));
        if ((e = avformat_write_header(fmt_, nullptr)) < 0) return fail(error, "cannot write header: " + avErr(e));
        pkt_.reset(av_packet_alloc());
        yuv_.reset(av_frame_alloc());
        yuv_->format = AV_PIX_FMT_YUV420P;
        yuv_->width = s.width;
        yuv_->height = s.height;
        av_frame_get_buffer(yuv_.get(), 0);
        return true;
    }

    bool writeVideo(const VideoFrame& f, std::string* error) override {
        if (f.width != s_.width || f.height != s_.height) return fail(error, "frame size mismatch");
        av_frame_make_writable(yuv_.get());
        if (!sws_) {
            sws_.reset(sws_getContext(f.width, f.height, AV_PIX_FMT_RGBA, s_.width, s_.height, AV_PIX_FMT_YUV420P, SWS_BILINEAR,
                                      nullptr, nullptr, nullptr));
        }
        const std::uint8_t* src[1] = {f.rgba.data()};
        const int stride[1] = {f.width * 4};
        sws_scale(sws_.get(), src, stride, 0, f.height, yuv_->data, yuv_->linesize);
        yuv_->pts = videoIndex_++;
        return encode(vctx_.get(), vst_, yuv_.get(), error);
    }

    bool writeAudio(const AudioBuffer& a, std::string* error) override {
        if (!actx_) return true;
        fifo_.insert(fifo_.end(), a.samples.begin(), a.samples.end());
        return drainAudio(false, error);
    }

    bool finish(std::string* error) override {
        if (finished_) return true;
        finished_ = true;
        bool ok = true;
        if (actx_) ok = drainAudio(true, error) && encode(actx_.get(), ast_, nullptr, error);
        ok = encode(vctx_.get(), vst_, nullptr, error) && ok;
        const int e = av_write_trailer(fmt_);
        if (e < 0) return fail(error, "cannot finalize file: " + avErr(e));
        return ok;
    }

private:
    bool fail(std::string* error, const std::string& msg) {
        setErr(error, msg);
        return false;
    }

    bool drainAudio(bool flushPartial, std::string* error) {
        const int ch = s_.channels;
        const int frameSize = actx_->frame_size > 0 ? actx_->frame_size : 1024;
        while (static_cast<int>(fifo_.size()) / ch >= frameSize || (flushPartial && !fifo_.empty())) {
            const int n = std::min<int>(frameSize, static_cast<int>(fifo_.size()) / ch);
            FramePtr fr(av_frame_alloc());
            fr->format = AV_SAMPLE_FMT_FLTP;
            fr->sample_rate = s_.sampleRate;
            av_channel_layout_copy(&fr->ch_layout, &actx_->ch_layout);
            fr->nb_samples = frameSize;  // last frame is zero-padded
            av_frame_get_buffer(fr.get(), 0);
            for (int c = 0; c < ch; ++c) {
                float* plane = reinterpret_cast<float*>(fr->data[c]);
                for (int i = 0; i < frameSize; ++i) plane[i] = i < n ? fifo_[static_cast<std::size_t>(i) * ch + c] : 0.0f;
            }
            fifo_.erase(fifo_.begin(), fifo_.begin() + static_cast<std::ptrdiff_t>(n) * ch);
            fr->pts = audioSamples_;
            audioSamples_ += frameSize;
            if (!encode(actx_.get(), ast_, fr.get(), error)) return false;
        }
        return true;
    }

    bool encode(AVCodecContext* ctx, AVStream* st, AVFrame* frame, std::string* error) {
        int e = avcodec_send_frame(ctx, frame);
        if (e < 0 && e != AVERROR_EOF) return fail(error, "encode failed: " + avErr(e));
        for (;;) {
            e = avcodec_receive_packet(ctx, pkt_.get());
            if (e == AVERROR(EAGAIN) || e == AVERROR_EOF) return true;
            if (e < 0) return fail(error, "encode failed: " + avErr(e));
            av_packet_rescale_ts(pkt_.get(), ctx->time_base, st->time_base);
            pkt_->stream_index = st->index;
            e = av_interleaved_write_frame(fmt_, pkt_.get());
            if (e < 0) return fail(error, "write failed: " + avErr(e));
        }
    }

    ExportSettings s_;
    AVFormatContext* fmt_ = nullptr;
    AVStream* vst_ = nullptr;
    AVStream* ast_ = nullptr;
    CodecPtr vctx_, actx_;
    PacketPtr pkt_;
    FramePtr yuv_;
    SwsPtr sws_;
    std::vector<float> fifo_;
    std::int64_t videoIndex_ = 0, audioSamples_ = 0;
    bool finished_ = false;
};

class Backend final : public IMediaBackend {
public:
    std::string name() const override { return std::string("ffmpeg ") + av_version_info(); }
    std::optional<ProbeResult> probe(const std::string& path, std::string* error) override { return probeFile(path, error); }
    std::unique_ptr<IMediaDecoder> createDecoder() override { return std::make_unique<Decoder>(); }
    std::unique_ptr<IMediaEncoder> createEncoder() override { return std::make_unique<Encoder>(); }
};

}  // namespace

std::shared_ptr<IMediaBackend> createBackend() {
    av_log_set_level(AV_LOG_ERROR);
    return std::make_shared<Backend>();
}

void registerModule(ModuleRegistry& r, int priority) {
    r.add<IMediaBackend>({.id = "ffmpeg",
                          .displayName = "FFmpeg",
                          .capabilities = {"probe", "decode", "encode.h264", "encode.aac"},
                          .priority = priority},
                         [] { return createBackend(); });
}

}  // namespace vstudio::ffmpeg
