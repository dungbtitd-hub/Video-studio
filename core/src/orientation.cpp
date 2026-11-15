#include "vstudio/orientation.hpp"
#include "vstudio/pixel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

namespace vstudio {

std::string Orientation::key() const {
    if (identity()) return {};
    std::string k = "r" + std::to_string(rotate);
    if (flipH) k += "h";
    if (flipV) k += "v";
    return k;
}

int normalizeQuarterTurn(int degrees) {
    const int q = static_cast<int>(std::lround(degrees / 90.0));
    return ((q % 4) + 4) % 4 * 90;
}

int assetRotation(const MediaAsset& a) {
    auto it = a.extra.find("rotation");
    if (it == a.extra.end() || !it->is_number()) return 0;
    return normalizeQuarterTurn(static_cast<int>(std::lround(it->get<double>())));
}

std::pair<int, int> displaySize(const MediaAsset& a) {
    const int r = assetRotation(a);
    return r == 90 || r == 270 ? std::pair{a.height, a.width} : std::pair{a.width, a.height};
}

Orientation orientationFromJson(const Json& j) {
    Orientation o;
    if (!j.is_object()) return o;
    o.rotate = normalizeQuarterTurn(static_cast<int>(std::lround(j.value("rotate", 0.0))));
    o.flipH = j.value("flip_h", false);
    o.flipV = j.value("flip_v", false);
    return o;
}

Json toJson(const Orientation& o) { return Json{{"rotate", o.rotate}, {"flip_h", o.flipH}, {"flip_v", o.flipV}}; }

Orientation clipOrientation(const Clip& c) {
    auto it = c.extra.find("orient");
    return it == c.extra.end() ? Orientation{} : orientationFromJson(*it);
}

Orientation combinedOrientation(const MediaAsset& a, const Clip* c) {
    Orientation o = c ? clipOrientation(*c) : Orientation{};
    o.rotate = (o.rotate + assetRotation(a)) % 360;
    return o;
}

std::pair<int, int> orientedSize(const MediaAsset& a, const Orientation& o) {
    return o.swapsAxes() ? std::pair{a.height, a.width} : std::pair{a.width, a.height};
}

namespace {

class OrientedDecoder final : public IMediaDecoder {
public:
    OrientedDecoder(std::unique_ptr<IMediaDecoder> inner, Orientation o) : inner_(std::move(inner)), o_(o) {}
    bool open(const std::string& path, std::string* error) override { return inner_->open(path, error); }
    void setMaxOutputSize(int w, int h) override {
        if (o_.swapsAxes()) std::swap(w, h);
        inner_->setMaxOutputSize(w, h);
    }
    void setDeepOutput(bool on) override { inner_->setDeepOutput(on); }
    std::optional<VideoFrame> videoFrameAt(Time t) override {
        auto f = inner_->videoFrameAt(t);
        if (f) orientFrame(*f, o_);
        return f;
    }
    AudioBuffer readAudio(Time t, std::int64_t frames, int sr, int ch) override { return inner_->readAudio(t, frames, sr, ch); }

private:
    std::unique_ptr<IMediaDecoder> inner_;
    Orientation o_;
};

template <class T>
void orientPlane(std::vector<T>& px, int& w, int& h, const Orientation& o) {
    const int W = w, H = h;
    const bool swap = o.swapsAxes();
    const int ow = swap ? H : W, oh = swap ? W : H;
    std::vector<T> out(px.size());
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int dx = x, dy = y;
            switch (o.rotate) {  // clockwise
                case 90: dx = H - 1 - y; dy = x; break;
                case 180: dx = W - 1 - x; dy = H - 1 - y; break;
                case 270: dx = y; dy = W - 1 - x; break;
                default: break;
            }
            if (o.flipH) dx = ow - 1 - dx;
            if (o.flipV) dy = oh - 1 - dy;
            const std::size_t s = (static_cast<std::size_t>(y) * W + x) * 4, d = (static_cast<std::size_t>(dy) * ow + dx) * 4;
            out[d] = px[s];
            out[d + 1] = px[s + 1];
            out[d + 2] = px[s + 2];
            out[d + 3] = px[s + 3];
        }
    }
    px.swap(out);
    w = ow;
    h = oh;
}

}  // namespace

void orientFrame(VideoFrame& f, const Orientation& o) {
    if (o.identity() || f.empty()) return;
    if (f.deep()) {
        orientPlane(f.half, f.width, f.height, o);
    } else {
        orientPlane(f.rgba, f.width, f.height, o);
    }
}

std::unique_ptr<IMediaDecoder> orientedDecoder(std::unique_ptr<IMediaDecoder> inner, const Orientation& o) {
    if (!inner || o.identity()) return inner;
    return std::make_unique<OrientedDecoder>(std::move(inner), o);
}

VideoFrame fitFrame(const VideoFrame& f, int maxW, int maxH) {
    if (f.empty() || maxW <= 0 || maxH <= 0 || (f.width <= maxW && f.height <= maxH)) return f;
    const VideoFrame src = f.deep() ? convertedFrame(f, PixelFormat::RGBA8) : f;
    const double k = std::min(static_cast<double>(maxW) / src.width, static_cast<double>(maxH) / src.height);
    const int w = std::max(1, static_cast<int>(src.width * k)), h = std::max(1, static_cast<int>(src.height * k));
    VideoFrame out;
    out.pts = src.pts;
    out.allocate(w, h);
    for (int y = 0; y < h; ++y) {
        const int y0 = y * src.height / h, y1 = std::max(y0 + 1, (y + 1) * src.height / h);
        for (int x = 0; x < w; ++x) {
            const int x0 = x * src.width / w, x1 = std::max(x0 + 1, (x + 1) * src.width / w);
            unsigned sum[4] = {0, 0, 0, 0};
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx) {
                    const std::uint8_t* p = &src.rgba[(static_cast<std::size_t>(sy) * src.width + sx) * 4];
                    for (int c = 0; c < 4; ++c) sum[c] += p[c];
                }
            const unsigned n = static_cast<unsigned>((y1 - y0) * (x1 - x0));
            std::uint8_t* d = &out.rgba[(static_cast<std::size_t>(y) * w + x) * 4];
            for (int c = 0; c < 4; ++c) d[c] = static_cast<std::uint8_t>((sum[c] + n / 2) / n);
        }
    }
    return out;
}

std::optional<int> jpegExifOrientation(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<unsigned char> buf(128 * 1024);  // EXIF sits in the first segments
    in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    buf.resize(static_cast<std::size_t>(in.gcount()));
    if (buf.size() < 4 || buf[0] != 0xFF || buf[1] != 0xD8) return std::nullopt;
    std::size_t i = 2;
    while (i + 4 <= buf.size()) {
        if (buf[i] != 0xFF) return std::nullopt;
        const unsigned marker = buf[i + 1];
        const std::size_t len = (static_cast<std::size_t>(buf[i + 2]) << 8) | buf[i + 3];
        if (marker == 0xDA || len < 2) return std::nullopt;  // start of scan: no EXIF before it
        const std::size_t seg = i + 4, end = std::min(buf.size(), i + 2 + len);
        if (marker == 0xE1 && end > seg + 14 && std::equal(buf.begin() + seg, buf.begin() + seg + 6, "Exif\0\0")) {
            const std::size_t tiff = seg + 6;
            const bool le = buf[tiff] == 'I';
            auto u16 = [&](std::size_t p) -> unsigned {
                if (p + 2 > end) return 0;
                return le ? buf[p] | (buf[p + 1] << 8) : (buf[p] << 8) | buf[p + 1];
            };
            auto u32 = [&](std::size_t p) -> std::size_t {
                if (p + 4 > end) return 0;
                return le ? buf[p] | (buf[p + 1] << 8) | (buf[p + 2] << 16) | (static_cast<std::size_t>(buf[p + 3]) << 24)
                          : (static_cast<std::size_t>(buf[p]) << 24) | (buf[p + 1] << 16) | (buf[p + 2] << 8) | buf[p + 3];
            };
            const std::size_t ifd = tiff + u32(tiff + 4);
            const unsigned n = u16(ifd);
            for (unsigned k = 0; k < n; ++k) {
                const std::size_t e = ifd + 2 + k * 12;
                if (e + 12 > end) break;
                if (u16(e) == 0x0112) {
                    const unsigned v = u16(e + 8);
                    return v >= 1 && v <= 8 ? std::optional<int>(static_cast<int>(v)) : std::nullopt;
                }
            }
            return std::nullopt;
        }
        i += 2 + len;
    }
    return std::nullopt;
}

Orientation fromExif(int e) {
    switch (e) {
        case 2: return {0, true, false};
        case 3: return {180, false, false};
        case 4: return {0, false, true};
        case 5: return {90, true, false};
        case 6: return {90, false, false};
        case 7: return {270, true, false};
        case 8: return {270, false, false};
        default: return {};
    }
}

}  // namespace vstudio
