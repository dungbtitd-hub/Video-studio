#include "vstudio/export_presets.hpp"

#include <algorithm>
#include <cmath>

namespace vstudio {

const std::vector<ExportPreset>& exportPresets() {
    static const std::vector<ExportPreset> presets{
        {"social", "Mạng xã hội 1080p", "TikTok, Reels, Shorts, Facebook: 1080p, tối đa 60 khung/giây", 1080, 60, 0.10},
        {"hd720", "Chia sẻ nhanh 720p", "File nhỏ để gửi qua tin nhắn", 720, 30, 0.08},
        {"fhd1080", "Full HD 1080p", "Chất lượng tiêu chuẩn cho YouTube và máy tính", 1080, 0, 0.12},
        {"qhd1440", "2K 1440p", "Màn hình lớn, giữ chi tiết khi phóng to", 1440, 0, 0.12},
        {"uhd2160", "4K 2160p", "Chất lượng cao nhất cho YouTube và TV 4K", 2160, 0, 0.10},
        {"master", "Bản gốc chất lượng cao", "Đúng kích thước dự án, bitrate cao để lưu trữ hoặc dựng tiếp", 0, 0, 0.25},
    };
    return presets;
}

const ExportPreset* findExportPreset(const std::string& id) {
    for (const auto& p : exportPresets())
        if (p.id == id) return &p;
    return nullptr;
}

ExportSettings exportSettingsFor(const Project& p, const ExportPreset& preset) {
    ExportSettings s;
    const int cw = std::max(2, p.canvas.width), ch = std::max(2, p.canvas.height);
    if (preset.shortEdge <= 0) {
        s.width = cw;
        s.height = ch;
    } else {
        const double k = static_cast<double>(preset.shortEdge) / std::min(cw, ch);
        auto even = [](double v) { return std::max(2, static_cast<int>(std::lround(v / 2.0)) * 2); };
        s.width = even(cw * k);
        s.height = even(ch * k);
    }
    s.fps = p.fps.valid() && p.fps.num > 0 ? p.fps : Rational{30, 1};
    if (preset.maxFps > 0 && static_cast<double>(s.fps.num) / s.fps.den > preset.maxFps + 0.01) s.fps = {preset.maxFps, 1};
    const double fps = static_cast<double>(s.fps.num) / s.fps.den;
    const double bps = static_cast<double>(s.width) * s.height * fps * preset.bitsPerPixel;
    s.videoBitrate = static_cast<std::int64_t>(std::clamp(bps, 1e6, 100e6));
    return s;
}

const std::vector<ExportFormat>& exportFormats() {
    static const std::vector<ExportFormat> formats{
        {"h264", "H.264", "Phát được ở mọi nơi"},
        {"hevc", "H.265 (HEVC)", "File nhỏ hơn khoảng 1/3, cùng chất lượng; máy và trình phát đời mới"},
        {"hevc10", "H.265 10-bit", "Giữ dải màu 10-bit cho video Log/10-bit đã chỉnh màu: không vệt bậc màu"},
    };
    return formats;
}

const ExportFormat* findExportFormat(const std::string& id) {
    for (const auto& f : exportFormats())
        if (f.id == id) return &f;
    return nullptr;
}

bool applyExportFormat(ExportSettings& s, const std::string& formatId) {
    double k = 1.0;
    if (formatId == "h264") {
        s.videoCodec = "h264";
        s.bitDepth = 8;
    } else if (formatId == "hevc") {
        s.videoCodec = "hevc";
        s.bitDepth = 8;
        k = 0.65;
    } else if (formatId == "hevc10") {
        s.videoCodec = "hevc";
        s.bitDepth = 10;
        k = 0.75;
    } else {
        return false;
    }
    s.videoBitrate = static_cast<std::int64_t>(std::clamp(static_cast<double>(s.videoBitrate) * k, 1e6, 100e6));
    return true;
}

ExportSettings exportSettingsFor(const Project& p, const ExportPreset& preset, const std::string& formatId) {
    ExportSettings s = exportSettingsFor(p, preset);
    applyExportFormat(s, formatId);
    return s;
}

std::int64_t estimateExportBytes(const ExportSettings& s, double seconds) {
    const double bits = (static_cast<double>(s.videoBitrate) + (s.includeAudio ? s.audioBitrate : 0)) * seconds;
    return static_cast<std::int64_t>(bits / 8 * 1.02);  // + container overhead
}

}  // namespace vstudio
