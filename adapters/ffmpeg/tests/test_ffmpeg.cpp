// FFmpeg module: lazy registration, 10-bit decode, plus the shared backend conformance suite.
#include <cstdio>
#include <set>

#include "backend_conformance.hpp"
#include "vstudio/export_presets.hpp"
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

TEST("ffmpeg: graded 10-bit footage exports as H.265 10-bit without losing levels") {
    auto b = ffmpeg::createBackend();
    const std::string src = makeTenBitRamp();
    std::string err;
    auto pr = b->probe(src, &err);
    CHECK(pr.has_value());
    Editor ed{Editor::newProject("Ramp", 880, 16, {25, 1}, ModuleRegistry::currentPlatform())};
    const Id asset = ed.addAsset(assetFromProbe(newId(), src, *pr, ""));
    const Id v = ed.addTrack(TrackType::Video, "V1");
    const Id c = ed.appendClip(v, asset);
    ed.setClipGrade({c}, Json{{"contrast", 1.15}, {"gamma", Json::array({0, 0, 0, 0.1})}});  // a real grade, not neutral
    MediaPool pool(b, "");
    ExportSettings s = exportSettingsFor(ed.project(), *findExportPreset("master"), "hevc10");
    s.includeAudio = false;
    auto enc = b->createEncoder();
    const std::string out = (vtest::conformance::workDir() / "graded10.mp4").string();
    const auto r = exportProject(ed.project(), pool, *enc, out, s);
    if (!r.ok) {
        // A build without any H.265 encoder must say so (CI images have x265 or a GPU encoder).
        CHECK(!r.error.empty());
        std::printf("       %s\n", r.error.c_str());
        return;
    }
    auto d = b->createDecoder();
    d->setDeepOutput(true);
    CHECK(d->open(out, &err));
    auto f = d->videoFrameAt(Time::zero());
    CHECK(f.has_value() && f->deep());
    std::set<int> levels;
    for (int x = 0; x < f->width; ++x)
        levels.insert(static_cast<int>(std::lround(halfToFloat(f->half[(static_cast<std::size_t>(f->height / 2) * f->width + x) * 4 + 1]) * 1023)));
    std::printf("       %zu levels after grade + H.265 10-bit\n", levels.size());
    CHECK(levels.size() > 400u);  // an 8-bit path keeps at most 256
}

namespace {

// A JPEG photo: left half red, right half blue.
std::string makePhoto() {
    const std::string path = (vtest::conformance::workDir() / "photo.jpg").string();
    constexpr int W = 320, H = 240;
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec) throw std::runtime_error("no mjpeg");
    AVCodecContext* c = avcodec_alloc_context3(codec);
    c->width = W;
    c->height = H;
    c->pix_fmt = AV_PIX_FMT_YUVJ444P;
    c->time_base = {1, 25};
    if (avcodec_open2(c, codec, nullptr) < 0) throw std::runtime_error("mjpeg open");
    AVFrame* f = av_frame_alloc();
    f->width = W;
    f->height = H;
    f->format = c->pix_fmt;
    av_frame_get_buffer(f, 0);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {  // full-range BT.601: red (76,85,255), blue (29,255,107)
            const bool left = x < W / 2;
            f->data[0][y * f->linesize[0] + x] = left ? 76 : 29;
            f->data[1][y * f->linesize[1] + x] = left ? 85 : 255;
            f->data[2][y * f->linesize[2] + x] = left ? 255 : 107;
        }
    f->pts = 0;
    AVPacket* pkt = av_packet_alloc();
    avcodec_send_frame(c, f);
    avcodec_send_frame(c, nullptr);
    // The bare JPEG bytes, as a camera or phone writes them (a .jpg file is one JPEG image).
    std::FILE* out = std::fopen(path.c_str(), "wb");
    while (avcodec_receive_packet(c, pkt) == 0) {
        std::fwrite(pkt->data, 1, static_cast<std::size_t>(pkt->size), out);
        av_packet_unref(pkt);
    }
    std::fclose(out);
    av_packet_free(&pkt);
    av_frame_free(&f);
    avcodec_free_context(&c);
    return path;
}

}  // namespace

TEST("ffmpeg: a JPEG photo decodes, again and again, at any time and size") {
    ModuleRegistry r;
    ffmpeg::registerModule(r);
    auto b = r.best<IMediaBackend>("decode");
    const std::string path = makePhoto();
    std::string err;
    auto pr = b->probe(path, &err);
    CHECK(pr.has_value());
    CHECK(pr->isStillImage);
    auto d = b->createDecoder();
    CHECK(d->open(path, &err));
    d->setMaxOutputSize(160, 120);
    // A photo clip asks for its picture at many times; image demuxers cannot seek back.
    for (const double s : {0.0, 2.5, 0.0, 10.0}) {
        auto f = d->videoFrameAt(Time::fromSeconds(s));
        CHECK(f.has_value());
        CHECK(f->width == 160 && f->height == 120);
        const auto* left = &f->rgba[(60 * 160 + 20) * 4];
        const auto* right = &f->rgba[(60 * 160 + 140) * 4];
        CHECK(left[0] > 200 && left[2] < 60);
        CHECK(right[2] > 200 && right[0] < 60);
    }
    d->setMaxOutputSize(80, 60);  // a new size re-converts the kept frame
    auto small = d->videoFrameAt(Time::zero());
    CHECK(small.has_value() && small->width == 80);
}
