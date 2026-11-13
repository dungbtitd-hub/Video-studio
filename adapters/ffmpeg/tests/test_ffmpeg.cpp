// FFmpeg module: lazy registration, 10-bit decode, plus the shared backend conformance suite.
#include <set>

#include "backend_conformance.hpp"
#include "vstudio/ffmpeg/ffmpeg_module.hpp"
#include "vstudio/pixel.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

using namespace vstudio;

TEST("ffmpeg: module registers lazily") {
    ModuleRegistry r;
    ffmpeg::registerModule(r);
    CHECK(!r.loaded("ffmpeg"));
    auto b = r.best<IMediaBackend>("decode");
    CHECK(b != nullptr);
    CHECK(r.loaded("ffmpeg"));
    CHECK(b->name().rfind("ffmpeg", 0) == 0);
}

namespace {

// A 10-bit 4:2:0 FFV1 file whose luma is a horizontal ramp over every 10-bit
// video level (64..940): an 8-bit pipeline can keep at most 220 of them.
std::string makeTenBitRamp() {
    const std::string path = (vtest::conformance::workDir() / "ramp10.mkv").string();
    constexpr int W = 880, H = 16;
    AVFormatContext* fmt = nullptr;
    avformat_alloc_output_context2(&fmt, nullptr, "matroska", path.c_str());
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_FFV1);
    if (!fmt || !codec) throw std::runtime_error("no matroska/ffv1");
    AVStream* st = avformat_new_stream(fmt, nullptr);
    AVCodecContext* c = avcodec_alloc_context3(codec);
    c->width = W;
    c->height = H;
    c->pix_fmt = AV_PIX_FMT_YUV420P10LE;
    c->time_base = {1, 25};
    c->colorspace = AVCOL_SPC_BT709;
    c->color_range = AVCOL_RANGE_MPEG;
    if (fmt->oformat->flags & AVFMT_GLOBALHEADER) c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(c, codec, nullptr) < 0) throw std::runtime_error("ffv1 open");
    avcodec_parameters_from_context(st->codecpar, c);
    st->time_base = c->time_base;
    avio_open(&fmt->pb, path.c_str(), AVIO_FLAG_WRITE);
    if (avformat_write_header(fmt, nullptr) < 0) throw std::runtime_error("header");
    AVFrame* f = av_frame_alloc();
    f->width = W;
    f->height = H;
    f->format = c->pix_fmt;
    av_frame_get_buffer(f, 0);
    AVPacket* pkt = av_packet_alloc();
    auto drain = [&] {
        while (avcodec_receive_packet(c, pkt) == 0) {
            av_packet_rescale_ts(pkt, c->time_base, st->time_base);
            pkt->stream_index = st->index;
            av_interleaved_write_frame(fmt, pkt);
        }
    };
    for (int i = 0; i < 3; ++i) {
        av_frame_make_writable(f);
        for (int y = 0; y < H; ++y) {
            auto* row = reinterpret_cast<std::uint16_t*>(f->data[0] + y * f->linesize[0]);
            for (int x = 0; x < W; ++x) row[x] = static_cast<std::uint16_t>(64 + x * 876 / (W - 1));
        }
        for (int p = 1; p < 3; ++p)
            for (int y = 0; y < H / 2; ++y) {
                auto* row = reinterpret_cast<std::uint16_t*>(f->data[p] + y * f->linesize[p]);
                for (int x = 0; x < W / 2; ++x) row[x] = 512;  // neutral chroma: grey ramp
            }
        f->pts = i;
        avcodec_send_frame(c, f);
        drain();
    }
    avcodec_send_frame(c, nullptr);
    drain();
    av_write_trailer(fmt);
    avio_closep(&fmt->pb);
    av_packet_free(&pkt);
    av_frame_free(&f);
    avcodec_free_context(&c);
    avformat_free_context(fmt);
    return path;
}

}  // namespace

TEST("ffmpeg: 10-bit sources keep their precision when deep output is asked") {
    auto b = ffmpeg::createBackend();
    const std::string path = makeTenBitRamp();
    std::string err;
    auto pr = b->probe(path, &err);
    CHECK(pr.has_value());
    CHECK_EQ(pr->bitDepth, 10);

    auto levels = [&](bool deep) {
        auto d = b->createDecoder();
        d->setDeepOutput(deep);
        CHECK(d->open(path, &err));
        auto f = d->videoFrameAt(Time::zero());
        CHECK(f.has_value());
        CHECK_EQ(f->deep(), deep);
        std::set<float> seen;
        float lo = 1, hi = 0;
        for (int x = 0; x < f->width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(f->height / 2) * f->width + x) * 4;
            const float v = deep ? halfToFloat(f->half[i]) : f->rgba[i] / 255.0f;
            seen.insert(v);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            if (deep) CHECK(std::abs(halfToFloat(f->half[i]) - halfToFloat(f->half[i + 1])) < 0.002f);  // grey
        }
        CHECK(lo < 0.01f && hi > 0.99f);  // limited range expanded to full
        return seen.size();
    };
    const auto deepLevels = levels(true), flatLevels = levels(false);
    CHECK(flatLevels <= 256u);
    CHECK(deepLevels > 700u);  // ~877 levels survive
}

static vtest::BackendSuite suite("ffmpeg", [] { return ffmpeg::createBackend(); });
