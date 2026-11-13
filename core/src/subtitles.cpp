#include "vstudio/subtitles.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <stdexcept>

#include "vstudio/fonts.hpp"
#include "vstudio/text.hpp"

namespace vstudio::subtitles {

namespace {

// ---------------------------------------------------------------- text helpers

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    std::size_t i = 0;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        i = 3;  // UTF-8 BOM
    for (; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            out.push_back(std::move(cur));
            cur.clear();
        } else if (ch == '\n') {
            out.push_back(std::move(cur));
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    out.push_back(std::move(cur));
    return out;
}

// Drops <tags> (HTML-ish markup in SRT/VTT) and {\override} blocks (ASS), decodes a few entities.
std::string stripMarkup(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '<') {
            const auto close = s.find('>', i);
            if (close != std::string::npos && close - i < 64) {
                i = close;
                continue;
            }
        }
        if (s[i] == '{' && i + 1 < s.size() && s[i + 1] == '\\') {
            const auto close = s.find('}', i);
            if (close != std::string::npos) {
                i = close;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    auto replaceAll = [&](const std::string& from, const std::string& to) {
        for (std::size_t p = 0; (p = out.find(from, p)) != std::string::npos; p += to.size()) out.replace(p, from.size(), to);
    };
    replaceAll("&amp;", "&");
    replaceAll("&lt;", "<");
    replaceAll("&gt;", ">");
    replaceAll("&nbsp;", " ");
    replaceAll("&quot;", "\"");
    return out;
}

// "01:02:03,450", "02:03.450", "1:02:03.45" -> time. False when it is not a timestamp.
bool parseClock(std::string s, Time& out) {
    s = trim(s);
    if (s.empty()) return false;
    for (char& ch : s)
        if (ch == ',') ch = '.';
    std::vector<std::string> parts;
    std::string cur;
    for (char ch : s) {
        if (ch == ':') {
            parts.push_back(cur);
            cur.clear();
        } else if ((ch >= '0' && ch <= '9') || ch == '.') {
            cur.push_back(ch);
        } else {
            return false;
        }
    }
    parts.push_back(cur);
    if (parts.size() < 2 || parts.size() > 3) return false;
    double secs = 0;
    try {
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            if (parts[i].empty() || parts[i].find('.') != std::string::npos) return false;
            secs = secs * 60 + std::stod(parts[i]);
        }
        if (parts.back().empty()) return false;
        secs = secs * 60 + std::stod(parts.back());
    } catch (...) {
        return false;
    }
    out = Time::fromMilliseconds(static_cast<std::int64_t>(std::llround(secs * 1000)));
    return true;
}

// "00:00:01,000 --> 00:00:02,000 align:start" -> times.
bool parseTimingLine(const std::string& line, Time& a, Time& b) {
    const auto arrow = line.find("-->");
    if (arrow == std::string::npos) return false;
    std::string right = trim(line.substr(arrow + 3));
    if (const auto sp = right.find_first_of(" \t"); sp != std::string::npos) right = right.substr(0, sp);  // VTT settings
    return parseClock(line.substr(0, arrow), a) && parseClock(right, b);
}

std::string clock(Time t, char decimal) {
    std::int64_t ms = std::max<std::int64_t>(0, std::llround(t.seconds() * 1000));
    const std::int64_t h = ms / 3'600'000;
    ms %= 3'600'000;
    const std::int64_t m = ms / 60'000;
    ms %= 60'000;
    const std::int64_t s = ms / 1000;
    ms %= 1000;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%02lld:%02lld:%02lld%c%03lld", static_cast<long long>(h), static_cast<long long>(m),
                  static_cast<long long>(s), decimal, static_cast<long long>(ms));
    return buf;
}

// Cues from blocks of "timing line + text lines" separated by blank lines (SRT and VTT).
std::vector<Cue> parseBlocks(const std::string& text, bool vtt) {
    std::vector<Cue> cues;
    const auto ls = lines(text);
    std::size_t i = 0;
    if (vtt) {  // header block
        while (i < ls.size() && !trim(ls[i]).empty()) ++i;
    }
    while (i < ls.size()) {
        if (trim(ls[i]).empty()) {
            ++i;
            continue;
        }
        // A block: optional id line(s), then the timing line, then text until a blank line.
        const std::size_t start = i;
        std::size_t timing = std::string::npos;
        for (std::size_t j = i; j < ls.size() && !trim(ls[j]).empty(); ++j)
            if (ls[j].find("-->") != std::string::npos) {
                timing = j;
                break;
            }
        if (timing == std::string::npos) {  // NOTE / STYLE / REGION / junk: skip the block
            while (i < ls.size() && !trim(ls[i]).empty()) ++i;
            continue;
        }
        Cue c;
        std::size_t j = timing + 1;
        if (!parseTimingLine(ls[timing], c.start, c.end)) {
            i = j;
            continue;
        }
        std::string body;
        for (; j < ls.size() && !trim(ls[j]).empty(); ++j) {
            // In SRT a missing blank line before the next "index / timing" pair still starts a new cue.
            if (!vtt && j + 1 < ls.size() && ls[j + 1].find("-->") != std::string::npos &&
                trim(ls[j]).find_first_not_of("0123456789") == std::string::npos)
                break;
            if (!body.empty()) body += '\n';
            body += trim(stripMarkup(ls[j]));
        }
        (void)start;
        c.text = trim(body);
        cues.push_back(std::move(c));
        i = j;
    }
    return cues;
}

// ---------------------------------------------------------------- JSON helpers

Json objectOr(const Json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_object() ? *it : Json::object();
}

// ---------------------------------------------------------------- wrapping

bool isCjk(char32_t c) {
    return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0xF900 && c <= 0xFAFF) ||
           (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0x0E00 && c <= 0x0EFF) /* Thai, Lao: no spaces between words */;
}

std::string encodeUtf8(const std::u32string& s) {
    std::string out;
    for (char32_t c : s) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

struct Token {
    std::u32string text;
    double width = 0;     // px
    bool spaceBefore = false;
};

// Greedy fill at `width`; returns the lines as token index ranges.
std::vector<std::pair<std::size_t, std::size_t>> fill(const std::vector<Token>& toks, double space, double width) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    std::size_t begin = 0;
    double w = 0;
    for (std::size_t i = 0; i < toks.size(); ++i) {
        const double add = (i > begin && toks[i].spaceBefore ? space : 0) + toks[i].width;
        if (i > begin && w + add > width) {
            out.emplace_back(begin, i);
            begin = i;
            w = toks[i].width;
        } else {
            w += add;
        }
    }
    if (begin < toks.size()) out.emplace_back(begin, toks.size());
    return out;
}

// ---------------------------------------------------------------- resolve

constexpr double kDefaultMaxWidth = 0.88;

struct Placed {
    Json content;
    std::array<double, 4> box{};  // content px, after offset
};

Placed place(const Project& p, const Track& t, const Clip& c, int depth);

double widthOf(const Track& t) {
    const Json set = objectOr(t.extra, kExtraKey);
    return std::clamp(set.value("max_width", kDefaultMaxWidth), 0.2, 1.0);
}

Placed place(const Project& p, const Track& t, const Clip& c, int depth) {
    const double W = p.canvas.width, H = p.canvas.height;
    const Json set = objectOr(t.extra, kExtraKey);
    Json style = objectOr(set, "style");
    if (style.empty()) style = objectOr(defaultTrackSettings(), "style");
    const Json cueStyle = objectOr(c.content, "style");
    if (!cueStyle.empty()) style.merge_patch(cueStyle);
    Json pos = objectOr(set, "position");
    const Json cuePos = objectOr(c.content, "position");
    if (!cuePos.empty()) pos.merge_patch(cuePos);

    const std::string h = pos.value("h", std::string("center"));
    Json out = style;
    out["kind"] = "subtitle";
    if (h == "left" || h == "right") out["align"] = h;
    double scale = 1;
    if (c.transform.is_object()) scale = std::max(0.05, c.transform.value("scale", 1.0));
    out["text"] = wrapText(c.content.value("text", std::string{}), out, widthOf(t) * W / scale);
    const auto b = text::textBounds(out);

    const double x = (std::clamp(pos.value("x", 0.5), 0.0, 1.0) - 0.5) * W;
    double dx = x - (b[0] + b[2]) / 2;
    if (h == "left") dx = x - b[0];
    else if (h == "right") dx = x - b[2];
    const double margin = std::clamp(pos.value("margin", 0.06), 0.0, 0.5) * H;
    const std::string anchor = pos.value("anchor", std::string("bottom"));
    double dy = H / 2 - margin - b[3];  // bottom
    if (anchor == "top") {
        dy = -H / 2 + margin - b[1];
    } else if (anchor == "middle") {
        dy = -(b[1] + b[3]) / 2;
    } else if (anchor == "custom") {
        dy = (std::clamp(pos.value("y", 0.85), 0.0, 1.0) - 0.5) * H - (b[1] + b[3]) / 2;
    } else if (anchor == "above") {
        // Right above the line the other track shows in the middle of this one.
        const Track* other = p.findTrack(pos.value("track", std::string{}));
        const Clip* under = nullptr;
        if (other && other != &t && depth < 3 && isSubtitleTrack(*other) && other->enabled)
            under = other->clipAt(c.timelineIn + (c.timelineOut() - c.timelineIn).scaled({1, 2}));
        if (under) {
            const Placed below = place(p, *other, *under, depth + 1);
            dy = below.box[1] - std::clamp(pos.value("gap", 0.012), 0.0, 0.2) * H - b[3];
        }
    }
    out["offset"] = Json::array({dx, dy});
    return {std::move(out), {b[0] + dx, b[1] + dy, b[2] + dx, b[3] + dy}};
}

// ---------------------------------------------------------------- translation helpers

std::string languageName(const std::string& code) {
    if (code.empty()) return {};
    for (const auto& l : languages())
        if (l.value("code", std::string{}) == code) return l.value("english", code);
    return code;  // a name typed by the user
}

// The JSON object inside a model reply (code fences, prose around it).
bool extractJson(const std::string& s, Json& out) {
    const auto a = s.find_first_of("{["), b = s.find_last_of("}]");
    if (a == std::string::npos || b == std::string::npos || b < a) return false;
    out = Json::parse(s.substr(a, b - a + 1), nullptr, false);
    return !out.is_discarded();
}

// {"items": [{"id", "text"}]} or [{"id", "text"}] or ["text", ...] or {"translations": [...]} -> texts by item.
bool readItems(const Json& j, const TranslateRequest& req, std::vector<std::string>& out, std::string* error) {
    const Json* arr = nullptr;
    if (j.is_array()) arr = &j;
    else if (j.is_object()) {
        for (const char* k : {"items", "translations", "subtitles", "lines", "data"})
            if (auto it = j.find(k); it != j.end() && it->is_array()) {
                arr = &*it;
                break;
            }
    }
    if (!arr) {
        if (error) *error = "Câu trả lời không có danh sách \"items\".";
        return false;
    }
    std::map<std::string, std::string> byId;
    std::vector<std::string> inOrder;
    for (const auto& e : *arr) {
        if (e.is_string()) {
            inOrder.push_back(e.get<std::string>());
        } else if (e.is_object()) {
            std::string text;
            if (auto it = e.find("text"); it != e.end() && it->is_string()) text = it->get<std::string>();
            else if (auto it2 = e.find("translation"); it2 != e.end() && it2->is_string()) text = it2->get<std::string>();
            else continue;
            inOrder.push_back(text);
            if (auto id = e.find("id"); id != e.end()) byId[id->is_string() ? id->get<std::string>() : id->dump()] = text;
        }
    }
    out.assign(req.items.size(), {});
    std::size_t missing = 0;
    const bool useIds = !byId.empty();
    for (std::size_t i = 0; i < req.items.size(); ++i) {
        if (useIds) {
            auto it = byId.find(req.items[i].id);
            if (it == byId.end()) {
                ++missing;
                continue;
            }
            out[i] = trim(it->second);
        } else if (i < inOrder.size()) {
            out[i] = trim(inOrder[i]);
        } else {
            ++missing;
        }
    }
    if (missing) {
        if (error) *error = "Thiếu " + std::to_string(missing) + " dòng trong câu trả lời.";
        return false;
    }
    return true;
}

std::string serviceError(const Json& j) {
    if (!j.is_object()) return {};
    auto it = j.find("error");
    if (it == j.end()) return {};
    if (it->is_string()) return it->get<std::string>();
    if (it->is_object()) return it->value("message", it->dump());
    return it->dump();
}

}  // namespace

// ================================================================= files

std::vector<Cue> parseSrt(const std::string& text) { return parseBlocks(text, false); }
std::vector<Cue> parseVtt(const std::string& text) { return parseBlocks(text, true); }

std::vector<Cue> parseAss(const std::string& text) {
    std::vector<Cue> cues;
    std::vector<std::string> format{"layer", "start", "end", "style", "name", "marginl", "marginr", "marginv", "effect", "text"};
    bool events = false;
    for (const auto& raw : lines(text)) {
        const std::string l = trim(raw);
        if (!l.empty() && l.front() == '[') {
            std::string sec = l;
            std::transform(sec.begin(), sec.end(), sec.begin(), [](unsigned char ch) { return std::tolower(ch); });
            events = sec == "[events]";
            continue;
        }
        if (!events) continue;
        auto colon = l.find(':');
        if (colon == std::string::npos) continue;
        std::string key = l.substr(0, colon);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) { return std::tolower(ch); });
        const std::string rest = l.substr(colon + 1);
        if (key == "format") {
            format.clear();
            std::stringstream ss(rest);
            std::string f;
            while (std::getline(ss, f, ',')) {
                f = trim(f);
                std::transform(f.begin(), f.end(), f.begin(), [](unsigned char ch) { return std::tolower(ch); });
                format.push_back(f);
            }
            continue;
        }
        if (key != "dialogue") continue;
        // Split into format.size() fields; the last (Text) keeps its commas.
        std::vector<std::string> fields;
        std::size_t p = 0;
        for (std::size_t k = 0; k + 1 < format.size(); ++k) {
            const auto comma = rest.find(',', p);
            if (comma == std::string::npos) break;
            fields.push_back(rest.substr(p, comma - p));
            p = comma + 1;
        }
        fields.push_back(p <= rest.size() ? rest.substr(p) : std::string{});
        if (fields.size() != format.size()) continue;
        Cue c;
        bool ok = true;
        for (std::size_t k = 0; k < format.size(); ++k) {
            if (format[k] == "start") ok = ok && parseClock(fields[k], c.start);
            else if (format[k] == "end") ok = ok && parseClock(fields[k], c.end);
            else if (format[k] == "text") {
                std::string t = stripMarkup(fields[k]);
                for (std::size_t q = 0; (q = t.find("\\N", q)) != std::string::npos;) t.replace(q, 2, "\n");
                for (std::size_t q = 0; (q = t.find("\\n", q)) != std::string::npos;) t.replace(q, 2, "\n");
                for (std::size_t q = 0; (q = t.find("\\h", q)) != std::string::npos;) t.replace(q, 2, " ");
                c.text = trim(t);
            }
        }
        if (ok) cues.push_back(std::move(c));
    }
    return cues;
}

std::vector<Cue> parseAny(const std::string& text) {
    const std::string head = trim(text.substr(0, std::min<std::size_t>(text.size(), 400)));
    auto starts = [&](const char* s) { return head.find(s) != std::string::npos && head.find(s) < 8; };
    if (starts("WEBVTT")) return parseVtt(text);
    if (head.find("[Script Info]") != std::string::npos || head.find("[Events]") != std::string::npos) return parseAss(text);
    return parseSrt(text);
}

std::string toSrt(const std::vector<Cue>& cues) {
    std::string out;
    int n = 0;
    for (const auto& c : cues) {
        out += std::to_string(++n) + "\n" + clock(c.start, ',') + " --> " + clock(c.end, ',') + "\n" + c.text + "\n\n";
    }
    return out;
}

std::string toVtt(const std::vector<Cue>& cues) {
    std::string out = "WEBVTT\n\n";
    for (const auto& c : cues) out += clock(c.start, '.') + " --> " + clock(c.end, '.') + "\n" + c.text + "\n\n";
    return out;
}

std::vector<Cue> normalise(std::vector<Cue> cues, Rational fps) {
    for (auto& c : cues) {
        c.start = std::max(Time::zero(), c.start).snapToFrame(fps);
        c.end = c.end.snapToFrame(fps);
        c.text = trim(c.text);
    }
    std::stable_sort(cues.begin(), cues.end(), [](const Cue& a, const Cue& b) { return a.start < b.start; });
    std::vector<Cue> out;
    for (auto& c : cues) {
        if (c.text.empty() || c.end <= c.start) continue;
        if (!out.empty() && out.back().start == c.start) {  // two speakers at once: one block of lines
            out.back().text += "\n" + c.text;
            out.back().end = std::max(out.back().end, c.end);
            continue;
        }
        if (!out.empty() && out.back().end > c.start) out.back().end = c.start;
        out.push_back(std::move(c));
    }
    std::erase_if(out, [](const Cue& c) { return c.end <= c.start; });
    return out;
}

// ================================================================= tracks

bool isSubtitleTrack(const Track& t) {
    if (t.type != TrackType::Video) return false;
    auto it = t.extra.find(kExtraKey);
    return it != t.extra.end() && it->is_object();
}

Json defaultTrackSettings(const std::string& lang) {
    Json style = subtitleStyles().front().style;
    style["font"] = "be-vietnam-pro-bold";
    style["size"] = 56.0;
    style["align"] = "center";
    style["line_height"] = 1.22;
    style["letter_spacing"] = 0.0;
    style["italic"] = false;
    style["rotation"] = 0.0;
    // Lines fade in and out quickly: nothing pops, and the next line is readable at once.
    style["in"] = {{"type", "fade"}, {"duration", 0.12}};
    style["out"] = {{"type", "fade"}, {"duration", 0.12}};
    style["loop"] = {{"type", ""}, {"speed", 1.0}};
    return {{"lang", lang},
            {"style", style},
            {"position", {{"anchor", "bottom"}, {"margin", 0.06}, {"x", 0.5}, {"y", 0.85}, {"h", "center"}}},
            {"max_width", kDefaultMaxWidth}};
}

Json cueContent(const std::string& text) { return {{"kind", "subtitle"}, {"text", text}}; }

std::vector<Cue> cuesOf(const Track& t) {
    std::vector<Cue> out;
    for (const auto& c : t.clips) {
        if (!c.generated() || c.content.value("kind", std::string{}) != "subtitle") continue;
        out.push_back({c.timelineIn, c.timelineOut(), c.content.value("text", std::string{})});
    }
    return out;
}

const std::vector<Style>& subtitleStyles() {
    static const std::vector<Style> kStyles = [] {
        auto base = [] {
            return Json{{"color", "#ffffff"},
                        {"color2", nullptr},
                        {"stroke", {{"color", "#000000"}, {"width", 0.0}}},
                        {"shadow", {{"color", "#000000"}, {"opacity", 0.0}, {"blur", 0.06}, {"x", 0.02}, {"y", 0.04}}},
                        {"glow", {{"color", "#ffffff"}, {"opacity", 0.0}, {"radius", 0.25}}},
                        {"box", {{"color", "#000000"}, {"opacity", 0.0}, {"padding", 0.22}, {"radius", 0.1}}}};
        };
        auto with = [&](std::initializer_list<std::pair<const char*, Json>> kv) {
            Json j = base();
            for (const auto& [k, v] : kv) {
                if (j.contains(k) && j[k].is_object() && v.is_object()) j[k].update(v);
                else j[k] = v;
            }
            return j;
        };
        return std::vector<Style>{
            {"classic", "Cổ điển",
             with({{"stroke", {{"width", 0.07}}}, {"shadow", {{"opacity", 0.45}, {"blur", 0.05}}}})},
            {"soft_box", "Nền mờ", with({{"box", {{"opacity", 0.55}}}})},
            {"solid_box", "Nền đen", with({{"box", {{"opacity", 0.88}, {"radius", 0.04}, {"padding", 0.18}}}})},
            {"cinema_yellow", "Vàng điện ảnh",
             with({{"color", "#ffe14d"}, {"stroke", {{"color", "#111111"}, {"width", 0.07}}}})},
            {"soft_shadow", "Bóng mềm", with({{"shadow", {{"opacity", 0.85}, {"blur", 0.12}, {"x", 0.0}, {"y", 0.03}}}})},
            {"thick_outline", "Viền dày", with({{"stroke", {{"width", 0.13}}}})},
            {"light_box", "Nền trắng",
             with({{"color", "#161618"}, {"box", {{"color", "#ffffff"}, {"opacity", 0.92}}}})},
            {"marker", "Nhãn vàng",
             with({{"color", "#141414"}, {"box", {{"color", "#ffd53d"}, {"opacity", 1.0}, {"radius", 0.06}}}})},
            {"minimal", "Tối giản", with({{"shadow", {{"opacity", 0.35}, {"blur", 0.04}}}})},
            {"neon", "Neon",
             with({{"color", "#effbff"}, {"glow", {{"color", "#2fc6ff"}, {"opacity", 0.9}, {"radius", 0.22}}}})},
            {"pink_outline", "Hồng viền",
             with({{"color", "#ffffff"}, {"stroke", {{"color", "#e0367f"}, {"width", 0.09}}}})},
            {"handwritten", "Viết tay",
             with({{"font", "patrick-hand"}, {"shadow", {{"opacity", 0.7}, {"blur", 0.06}}}})},
            {"karaoke_blue", "Xanh sân khấu",
             with({{"color", "#7ef9ff"}, {"color2", "#2a6dff"}, {"stroke", {{"color", "#ffffff"}, {"width", 0.05}}}})},
            {"retro", "Hoài cổ",
             with({{"font", "lobster"}, {"color", "#fff4dc"}, {"stroke", {{"color", "#7a2a1d"}, {"width", 0.07}}}})},
        };
    }();
    return kStyles;
}

const std::vector<Position>& subtitlePositions() {
    static const std::vector<Position> kPositions{
        {"bottom", "Dưới giữa", {{"anchor", "bottom"}, {"x", 0.5}, {"h", "center"}}},
        {"bottom_left", "Dưới trái", {{"anchor", "bottom"}, {"x", 0.06}, {"h", "left"}}},
        {"bottom_right", "Dưới phải", {{"anchor", "bottom"}, {"x", 0.94}, {"h", "right"}}},
        {"middle", "Giữa", {{"anchor", "middle"}, {"x", 0.5}, {"h", "center"}}},
        {"top", "Trên giữa", {{"anchor", "top"}, {"x", 0.5}, {"h", "center"}}},
        {"top_left", "Trên trái", {{"anchor", "top"}, {"x", 0.06}, {"h", "left"}}},
        {"top_right", "Trên phải", {{"anchor", "top"}, {"x", 0.94}, {"h", "right"}}},
    };
    return kPositions;
}

std::string wrapText(const std::string& textIn, const Json& style, double maxWidthPx) {
    const auto font = text::Font::get(style.value("font", std::string(text::defaultFontId())));
    const auto fallback = text::Font::get(text::defaultFontId());
    const double size = std::max(1.0, style.value("size", 56.0));
    const double spacing = style.value("letter_spacing", 0.0);
    auto advance = [&](char32_t ch) {
        int g = font->glyph(ch);
        if (g == 0 && ch > U' ') {
            if (const int fg = fallback->glyph(ch)) return (fallback->advance(fg) + spacing) * size;
        }
        return (font->advance(g) + spacing) * size;
    };
    const double space = advance(U' ');
    std::string out;
    bool firstPara = true;
    for (const auto& para : lines(textIn)) {
        if (!firstPara) out += '\n';
        firstPara = false;
        const std::u32string s = text::decodeUtf8(para);
        // Tokens: words split on spaces; CJK/Thai letters each stand alone (no spaces between words).
        std::vector<Token> toks;
        bool pendingSpace = false;
        for (char32_t ch : s) {
            if (ch == U' ' || ch == U'\t') {
                pendingSpace = true;
                continue;
            }
            const bool cjk = isCjk(ch);
            if (toks.empty() || pendingSpace || cjk || isCjk(toks.back().text.back())) {
                toks.push_back({{}, 0, pendingSpace && !toks.empty()});
            }
            toks.back().text.push_back(ch);
            toks.back().width += advance(ch);
            pendingSpace = false;
        }
        // A single word wider than a line is broken between letters.
        std::vector<Token> fit;
        for (auto& t : toks) {
            if (t.width <= maxWidthPx || t.text.size() < 2) {
                fit.push_back(std::move(t));
                continue;
            }
            Token cur{{}, 0, t.spaceBefore};
            for (char32_t ch : t.text) {
                const double a = advance(ch);
                if (!cur.text.empty() && cur.width + a > maxWidthPx) {
                    fit.push_back(std::move(cur));
                    cur = Token{};
                }
                cur.text.push_back(ch);
                cur.width += a;
            }
            fit.push_back(std::move(cur));
        }
        auto rows = fill(fit, space, maxWidthPx);
        if (rows.size() > 1) {
            // Balance: the narrowest width giving the same number of lines (no long line over a short one).
            double lo = 0, hi = maxWidthPx;
            for (int it = 0; it < 14; ++it) {
                const double mid = (lo + hi) / 2;
                if (fill(fit, space, mid).size() <= rows.size()) hi = mid;
                else lo = mid;
            }
            rows = fill(fit, space, hi);
        }
        for (std::size_t r = 0; r < rows.size(); ++r) {
            if (r) out += '\n';
            std::u32string line;
            for (std::size_t i = rows[r].first; i < rows[r].second; ++i) {
                if (i > rows[r].first && fit[i].spaceBefore) line.push_back(U' ');
                line += fit[i].text;
            }
            out += encodeUtf8(line);
        }
    }
    return out;
}

Json resolvedContent(const Project& p, const Track& t, const Clip& c) { return place(p, t, c, 0).content; }

// ================================================================= translation

const Json& languages() {
    static const Json kLangs = Json::array({
        {{"code", "vi"}, {"name", "Tiếng Việt"}, {"english", "Vietnamese"}},
        {{"code", "en"}, {"name", "Tiếng Anh"}, {"english", "English"}},
        {{"code", "zh-Hans"}, {"name", "Tiếng Trung (giản thể)"}, {"english", "Simplified Chinese"}},
        {{"code", "zh-Hant"}, {"name", "Tiếng Trung (phồn thể)"}, {"english", "Traditional Chinese"}},
        {{"code", "ja"}, {"name", "Tiếng Nhật"}, {"english", "Japanese"}},
        {{"code", "ko"}, {"name", "Tiếng Hàn"}, {"english", "Korean"}},
        {{"code", "th"}, {"name", "Tiếng Thái"}, {"english", "Thai"}},
        {{"code", "lo"}, {"name", "Tiếng Lào"}, {"english", "Lao"}},
        {{"code", "km"}, {"name", "Tiếng Khmer"}, {"english", "Khmer"}},
        {{"code", "id"}, {"name", "Tiếng Indonesia"}, {"english", "Indonesian"}},
        {{"code", "ms"}, {"name", "Tiếng Mã Lai"}, {"english", "Malay"}},
        {{"code", "fr"}, {"name", "Tiếng Pháp"}, {"english", "French"}},
        {{"code", "de"}, {"name", "Tiếng Đức"}, {"english", "German"}},
        {{"code", "es"}, {"name", "Tiếng Tây Ban Nha"}, {"english", "Spanish"}},
        {{"code", "pt"}, {"name", "Tiếng Bồ Đào Nha"}, {"english", "Portuguese"}},
        {{"code", "it"}, {"name", "Tiếng Ý"}, {"english", "Italian"}},
        {{"code", "ru"}, {"name", "Tiếng Nga"}, {"english", "Russian"}},
        {{"code", "hi"}, {"name", "Tiếng Hindi"}, {"english", "Hindi"}},
        {{"code", "ar"}, {"name", "Tiếng Ả Rập"}, {"english", "Arabic"}},
    });
    return kLangs;
}

std::vector<TranslateRequest> makeBatches(const Track& t, const std::string& from, const std::string& to,
                                          const std::string& notes, int batchSize, int contextLines) {
    batchSize = std::clamp(batchSize, 1, 500);
    contextLines = std::clamp(contextLines, 0, 20);
    std::vector<std::string> texts;
    for (const auto& c : cuesOf(t)) texts.push_back(c.text);
    std::vector<TranslateRequest> out;
    for (std::size_t i = 0; i < texts.size(); i += static_cast<std::size_t>(batchSize)) {
        TranslateRequest r;
        r.from = from;
        r.to = to;
        r.notes = notes;
        const std::size_t ctx0 = i >= static_cast<std::size_t>(contextLines) ? i - contextLines : 0;
        for (std::size_t k = ctx0; k < i; ++k) r.context.push_back(texts[k]);
        const std::size_t end = std::min(texts.size(), i + static_cast<std::size_t>(batchSize));
        for (std::size_t k = i; k < end; ++k) r.items.push_back({std::to_string(k - i + 1), texts[k]});
        out.push_back(std::move(r));
    }
    return out;
}

const Json& providerPresets() {
    static const Json kPresets = Json::array({
        {{"id", "anthropic"}, {"name", "Claude (Anthropic)"}, {"provider", "anthropic"},
         {"url", "https://api.anthropic.com/v1/messages"}, {"model", "claude-opus-5-5"}, {"needs_key", true},
         {"hint", "Khóa API tạo ở console.anthropic.com"}},
        {{"id", "openai"}, {"name", "OpenAI (ChatGPT)"}, {"provider", "openai"},
         {"url", "https://api.openai.com/v1/chat/completions"}, {"model", ""}, {"needs_key", true},
         {"hint", "Nhập tên model, ví dụ model GPT bạn đang dùng"}},
        {{"id", "gemini"}, {"name", "Google Gemini"}, {"provider", "gemini"},
         {"url", "https://generativelanguage.googleapis.com/v1beta"}, {"model", ""}, {"needs_key", true},
         {"hint", "Khóa API tạo ở aistudio.google.com; nhập tên model Gemini"}},
        {{"id", "deepseek"}, {"name", "DeepSeek"}, {"provider", "openai"},
         {"url", "https://api.deepseek.com/v1/chat/completions"}, {"model", "deepseek-chat"}, {"needs_key", true},
         {"hint", "Dịch vụ tương thích OpenAI"}},
        {{"id", "ollama"}, {"name", "Ollama (máy cục bộ, không cần mạng)"}, {"provider", "openai"},
         {"url", "http://localhost:11434/v1/chat/completions"}, {"model", "qwen2.5:7b"}, {"needs_key", false},
         {"hint", "Chạy model trên máy này: cài Ollama rồi tải model"}},
        {{"id", "lmstudio"}, {"name", "LM Studio (máy cục bộ)"}, {"provider", "openai"},
         {"url", "http://localhost:1234/v1/chat/completions"}, {"model", ""}, {"needs_key", false},
         {"hint", "Bật Local Server trong LM Studio"}},
        {{"id", "openai_compatible"}, {"name", "Dịch vụ tương thích OpenAI khác"}, {"provider", "openai"},
         {"url", ""}, {"model", ""}, {"needs_key", true},
         {"hint", "OpenRouter, Groq, Together, Azure, máy chủ nội bộ... (đường dẫn .../chat/completions)"}},
        {{"id", "custom"}, {"name", "API mở Video Studio (ứng dụng của bạn)"}, {"provider", "custom"},
         {"url", ""}, {"model", ""}, {"needs_key", false},
         {"hint", "Máy chủ của bạn nhận/trả JSON theo docs/subtitles.md"}},
    });
    return kPresets;
}

std::string systemPrompt(const TranslateRequest& req) {
    const std::string from = languageName(req.from), to = languageName(req.to);
    std::string s = "You translate video subtitles" + (from.empty() ? std::string() : " from " + from) + " into " + to +
                    ".\n"
                    "- Translate the text of every item naturally and concisely, as on-screen subtitles a viewer reads "
                    "in a moment. Keep the meaning, tone, names and numbers.\n"
                    "- Keep the items separate: never merge, split, drop, add or reorder items, and keep each id.\n"
                    "- Use a line break inside an item only where the source has one.\n"
                    "- The \"context\" lines come just before these items. They help you understand; do not translate "
                    "or return them.\n"
                    "- Reply with only this JSON and nothing else: {\"items\": [{\"id\": \"<id>\", \"text\": "
                    "\"<translation>\"}]}";
    if (!trim(req.notes).empty()) s += "\n\nNotes from the video editor (glossary, names, tone):\n" + trim(req.notes);
    return s;
}

std::string userMessage(const TranslateRequest& req) {
    Json items = Json::array();
    for (const auto& it : req.items) items.push_back({{"id", it.id}, {"text", it.text}});
    Json j{{"target_language", languageName(req.to)}, {"context", req.context}, {"items", items}};
    if (!req.from.empty()) j["source_language"] = languageName(req.from);
    return j.dump(-1, ' ', false, Json::error_handler_t::replace);
}

HttpRequest buildHttpRequest(const Json& config, const TranslateRequest& req) {
    const std::string provider = config.value("provider", std::string("openai"));
    std::string url = trim(config.value("url", std::string{}));
    const std::string key = trim(config.value("api_key", std::string{}));
    std::string model = trim(config.value("model", std::string{}));
    if (url.empty()) throw std::invalid_argument("Chưa nhập địa chỉ (URL) của dịch vụ.");
    HttpRequest r;
    r.headers.emplace_back("Content-Type", "application/json");
    const auto dump = [](const Json& j) { return j.dump(-1, ' ', false, Json::error_handler_t::replace); };
    if (provider == "openai") {
        if (model.empty()) throw std::invalid_argument("Chưa nhập tên model.");
        r.url = url;
        if (!key.empty()) r.headers.emplace_back("Authorization", "Bearer " + key);
        r.body = dump({{"model", model},
                       {"messages", Json::array({{{"role", "system"}, {"content", systemPrompt(req)}},
                                                 {{"role", "user"}, {"content", userMessage(req)}}})}});
    } else if (provider == "anthropic") {
        if (model.empty()) model = "claude-opus-5-5";
        r.url = url;
        if (!key.empty()) r.headers.emplace_back("x-api-key", key);
        r.headers.emplace_back("anthropic-version", "2026-10-21");
        Json body{{"model", model},
                  {"max_tokens", 16000},
                  {"system", systemPrompt(req)},
                  {"messages", Json::array({{{"role", "user"}, {"content", userMessage(req)}}})},
                  // Translation is a simple task: low effort keeps it fast and cheap.
                  {"output_config", {{"effort", "low"}}}};
        if (url.find("api.anthropic.com") != std::string::npos) {
            // If the model declines a line, the API retries on a fallback model by itself.
            r.headers.emplace_back("anthropic-beta", "server-side-fallback-2026-07-01");
            body["fallbacks"] = "default";
        }
        r.body = dump(body);
    } else if (provider == "gemini") {
        if (model.empty()) throw std::invalid_argument("Chưa nhập tên model.");
        while (!url.empty() && url.back() == '/') url.pop_back();
        r.url = url.find(":generateContent") != std::string::npos ? url : url + "/models/" + model + ":generateContent";
        if (!key.empty()) r.headers.emplace_back("x-goog-api-key", key);
        r.body = dump({{"systemInstruction", {{"parts", Json::array({{{"text", systemPrompt(req)}}})}}},
                       {"contents", Json::array({{{"role", "user"}, {"parts", Json::array({{{"text", userMessage(req)}}})}}})},
                       {"generationConfig", {{"responseMimeType", "application/json"}}}});
    } else if (provider == "custom") {
        r.url = url;
        if (!key.empty()) r.headers.emplace_back("Authorization", "Bearer " + key);
        Json items = Json::array();
        for (const auto& it : req.items) items.push_back({{"id", it.id}, {"text", it.text}});
        r.body = dump({{"source_lang", req.from},
                       {"target_lang", req.to},
                       {"items", items},
                       {"context", req.context},
                       {"notes", req.notes},
                       {"model", model}});
    } else {
        throw std::invalid_argument("Không biết dịch vụ: " + provider);
    }
    if (auto h = config.find("headers"); h != config.end() && h->is_object())
        for (auto it = h->begin(); it != h->end(); ++it)
            if (it.value().is_string()) r.headers.emplace_back(it.key(), it.value().get<std::string>());
    return r;
}

bool parseHttpResponse(const Json& config, const TranslateRequest& req, const std::string& body,
                       std::vector<std::string>& out, std::string* error) {
    const std::string provider = config.value("provider", std::string("openai"));
    const Json j = Json::parse(body, nullptr, false);
    if (j.is_discarded()) {
        if (error) *error = "Dịch vụ trả về dữ liệu không phải JSON.";
        return false;
    }
    if (auto e = serviceError(j); !e.empty()) {
        if (error) *error = "Dịch vụ báo lỗi: " + e;
        return false;
    }
    if (provider == "custom") return readItems(j, req, out, error);
    std::string text;
    if (provider == "anthropic") {
        const std::string stop = j.value("stop_reason", std::string{});
        if (stop == "refusal") {
            if (error) *error = "Model từ chối dịch đoạn này.";
            return false;
        }
        if (auto c = j.find("content"); c != j.end() && c->is_array())
            for (const auto& b : *c)
                if (b.value("type", std::string{}) == "text") text += b.value("text", std::string{});
        if (stop == "max_tokens" && text.empty()) {
            if (error) *error = "Câu trả lời bị cắt: hãy giảm số dòng mỗi lần gửi.";
            return false;
        }
    } else if (provider == "gemini") {
        if (auto c = j.find("candidates"); c != j.end() && c->is_array() && !c->empty()) {
            const Json parts = (*c)[0].value("content", Json::object()).value("parts", Json::array());
            for (const auto& p : parts)
                if (!p.value("thought", false)) text += p.value("text", std::string{});
        }
    } else {  // openai-compatible
        if (auto c = j.find("choices"); c != j.end() && c->is_array() && !c->empty()) {
            const Json msg = (*c)[0].value("message", Json::object());
            if (auto ct = msg.find("content"); ct != msg.end()) {
                if (ct->is_string()) text = ct->get<std::string>();
                else if (ct->is_array())
                    for (const auto& p : *ct) text += p.value("text", std::string{});
            }
            if (text.empty() && msg.value("refusal", Json()).is_string()) {
                if (error) *error = "Model từ chối dịch đoạn này.";
                return false;
            }
        }
    }
    if (trim(text).empty()) {
        if (error) *error = "Câu trả lời rỗng.";
        return false;
    }
    Json inner;
    if (!extractJson(text, inner)) {
        if (error) *error = "Model không trả về JSON như yêu cầu.";
        return false;
    }
    return readItems(inner, req, out, error);
}

// ================================================================= TranslationJob

TranslationJob::TranslationJob(const Track& source, Json config, const std::string& from, const std::string& to,
                               const std::string& notes, int batchSize)
    : trackId_(source.id), config_(std::move(config)), to_(to) {
    batches_ = makeBatches(source, from, to, notes, batchSize);
    std::vector<Id> ids;
    for (const auto& c : source.clips)
        if (c.generated() && c.content.value("kind", std::string{}) == "subtitle") ids.push_back(c.id);
    std::size_t k = 0;
    for (const auto& b : batches_) {
        clipIds_.emplace_back(ids.begin() + static_cast<std::ptrdiff_t>(k),
                              ids.begin() + static_cast<std::ptrdiff_t>(k + b.items.size()));
        k += b.items.size();
    }
    done_.assign(batches_.size(), false);
}

bool TranslationJob::acceptTexts(std::size_t i, const std::vector<std::string>& texts, std::string* error) {
    if (i >= batches_.size()) {
        if (error) *error = "batch out of range";
        return false;
    }
    if (texts.size() != clipIds_[i].size()) {
        if (error) *error = "Số dòng dịch không khớp.";
        return false;
    }
    for (std::size_t k = 0; k < texts.size(); ++k) results_[clipIds_[i][k]] = texts[k];
    done_[i] = true;
    return true;
}

bool TranslationJob::accept(std::size_t i, const std::string& body, std::string* error) {
    if (i >= batches_.size()) {
        if (error) *error = "batch out of range";
        return false;
    }
    std::vector<std::string> texts;
    if (!parseHttpResponse(config_, batches_[i], body, texts, error)) return false;
    return acceptTexts(i, texts, error);
}

std::size_t TranslationJob::doneCount() const {
    return static_cast<std::size_t>(std::count(done_.begin(), done_.end(), true));
}

}  // namespace vstudio::subtitles
