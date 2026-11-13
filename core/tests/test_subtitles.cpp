#include <chrono>

#include "fake_media.hpp"
#include "vstudio/editor.hpp"
#include "vstudio/frame_cache.hpp"
#include "vstudio/generators.hpp"
#include "vstudio/modules.hpp"
#include "vstudio/project_io.hpp"
#include "vstudio/render.hpp"
#include "vstudio/subtitles.hpp"
#include "vstudio/text.hpp"
#include "vtest.hpp"

using namespace vstudio;
namespace sub = vstudio::subtitles;

namespace {

Time secs(double s) { return Time::fromSeconds(s); }

Editor newEditor() {
    Editor ed{Editor::newProject("Sub", 1280, 720, {25, 1}, "test")};
    ed.addTrack(TrackType::Video, "V1");
    return ed;
}

// Rows of the frame that hold drawn (non-black) pixels: the project has no media, the picture is black.
std::pair<int, int> inkRows(const VideoFrame& f) {
    int first = -1, last = -1;
    for (int y = 0; y < f.height; ++y) {
        int sum = 0;
        for (int x = 0; x < f.width; ++x)
            for (int ch = 0; ch < 3; ++ch) sum += f.rgba[(static_cast<std::size_t>(y) * f.width + x) * 4 + ch];
        if (sum > 255 * 3) {
            if (first < 0) first = y;
            last = y;
        }
    }
    return {first, last};
}

}  // namespace

TEST("subtitles: SRT, VTT and ASS files read and write back") {
    const std::string srt =
        "\xEF\xBB\xBF" "1\r\n00:00:01,000 --> 00:00:02,500\r\n<i>Xin chào</i>\r\nViệt Nam\r\n\r\n"
        "2\r\n00:00:03,000 --> 00:00:04,000\r\nDòng hai\r\n"
        "3\r\n00:01:02.250 --> 00:01:03,000\r\nThiếu dòng trống & vẫn đọc được\r\n";
    const auto a = sub::parseSrt(srt);
    CHECK_EQ(a.size(), 3u);
    CHECK(a[0].start == Time::fromMilliseconds(1000) && a[0].end == Time::fromMilliseconds(2500));
    CHECK_EQ(a[0].text, std::string("Xin chào\nViệt Nam"));
    CHECK_EQ(a[1].text, std::string("Dòng hai"));
    CHECK(a[2].start == Time::fromMilliseconds(62250));
    CHECK_EQ(sub::parseSrt(sub::toSrt(a)).size(), 3u);
    CHECK_EQ(sub::parseSrt(sub::toSrt(a))[0].text, a[0].text);

    const std::string vtt =
        "WEBVTT - phim\n\nNOTE ghi chú\nbỏ qua\n\nSTYLE\n::cue { color: red }\n\n"
        "intro\n00:01.000 --> 00:02.000 align:start position:10%\n<v Lan>Chào <b>bạn</b>\n\n"
        "01:00:00.000 --> 01:00:01.500\nMột giờ\n";
    const auto v = sub::parseVtt(vtt);
    CHECK_EQ(v.size(), 2u);
    CHECK_EQ(v[0].text, std::string("Chào bạn"));
    CHECK(v[1].start == secs(3600));
    CHECK_EQ(sub::parseAny(vtt).size(), 2u);
    CHECK_EQ(sub::parseVtt(sub::toVtt(v)).size(), 2u);

    const std::string ass =
        "[Script Info]\nTitle: x\n\n[V4+ Styles]\nFormat: Name, Fontname\nStyle: Default,Arial\n\n[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:01.50,0:00:03.00,Default,,0,0,0,,{\\an8}Trên cùng\\Ndòng hai, có dấu phẩy\n"
        "Comment: 0,0:00:04.00,0:00:05.00,Default,,0,0,0,,không lấy\n";
    const auto s = sub::parseAny(ass);
    CHECK_EQ(s.size(), 1u);
    CHECK(s[0].start == Time::fromMilliseconds(1500));
    CHECK_EQ(s[0].text, std::string("Trên cùng\ndòng hai, có dấu phẩy"));

    // Overlaps and lines starting together never reach the track as overlapping clips.
    const auto n = sub::normalise({{secs(2), secs(5), "b"}, {secs(0), secs(3), "a"}, {secs(2), secs(4), "c"},
                                   {secs(6), secs(6), "rỗng"}, {secs(7), secs(8), "  "}},
                                  {25, 1});
    CHECK_EQ(n.size(), 2u);
    CHECK(n[0].end == secs(2));
    CHECK_EQ(n[1].text, std::string("b\nc"));
}

TEST("subtitles: inserted, imported, shifted, styled and undone on a subtitle track") {
    Editor ed = newEditor();
    const Id c1 = ed.addSubtitle({}, secs(1), "Dòng một");
    const Track* t = ed.project().trackOfClip(c1);
    CHECK(t && sub::isSubtitleTrack(*t));
    CHECK(&ed.project().tracks.back() == t);  // on top of the picture
    const Id track = t->id;
    // At a time already covered: placed after that line; cut short before the next one.
    const Id c3 = ed.addSubtitle(track, secs(5), "Dòng ba");
    const Id c2 = ed.addSubtitle(track, secs(2), "Dòng hai", secs(10));
    CHECK(ed.project().findClip(c2)->timelineIn == secs(3));
    CHECK(ed.project().findClip(c2)->timelineOut() == secs(5));
    CHECK_THROWS(ed.addSubtitle(track, secs(4), "không còn chỗ"));
    // Text edits go through the content patch like any text.
    ed.updateClipContent(c1, {{"text", "Xin chào"}});
    CHECK_EQ(sub::cuesOf(*ed.project().findTrack(track))[0].text, std::string("Xin chào"));
    // One edit restyles every line.
    ed.setSubtitleSettings(track, {{"style", {{"color", "#ffe14d"}}}, {"position", {{"anchor", "top"}}}});
    CHECK_EQ(ed.project().findTrack(track)->extra["subtitle"]["style"]["color"], "#ffe14d");
    ed.shiftSubtitles(track, secs(0.4));
    CHECK(ed.project().findClip(c3)->timelineIn == secs(5.4));
    CHECK_THROWS(ed.shiftSubtitles(track, secs(-10)));
    ed.undo();
    CHECK(ed.project().findClip(c3)->timelineIn == secs(5));

    // Import: a new track, one undo step, saved and loaded intact.
    std::vector<sub::Cue> cues;
    for (int i = 0; i < 3000; ++i) cues.push_back({secs(i * 2.0), secs(i * 2.0 + 1.5), "Dòng số " + std::to_string(i)});
    const auto t0 = std::chrono::steady_clock::now();
    const Id imported = ed.importSubtitles(cues, "Tiếng Anh", "en");
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  import 3000 lines: %.1f ms\n", ms);
    CHECK(ms < 2000);
    CHECK_EQ(ed.project().findTrack(imported)->clips.size(), 3000u);
    CHECK_EQ(ed.project().findTrack(imported)->extra["subtitle"]["lang"], "en");
    const Project back = projectFromJson(toJson(ed.project()));
    CHECK(sub::isSubtitleTrack(*back.findTrack(imported)));
    CHECK_EQ(back.findTrack(imported)->clips.size(), 3000u);
    CHECK_EQ(sub::toSrt(sub::cuesOf(*back.findTrack(imported))), sub::toSrt(sub::cuesOf(*ed.project().findTrack(imported))));
    ed.undo();
    CHECK(!ed.project().findTrack(imported));
    CHECK_THROWS(ed.importSubtitles({}, "rỗng"));
}

TEST("subtitles: wrapping keeps lines inside the frame and balanced") {
    const Json style = sub::defaultTrackSettings()["style"];
    const std::string longLine =
        "Đây là một câu phụ đề rất dài để kiểm tra việc tự xuống dòng khi chữ vượt quá chiều rộng khung hình";
    const std::string w = sub::wrapText(longLine, style, 1800);
    CHECK(w.find('\n') != std::string::npos);
    // Every line fits.
    Json c = style;
    std::size_t start = 0;
    int lines = 0;
    while (start <= w.size()) {
        const auto nl = w.find('\n', start);
        c["text"] = w.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        const auto b = text::textBounds(c);
        CHECK(b[2] - b[0] <= 1800 + 2 * 56 * 0.2);
        ++lines;
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    CHECK_EQ(lines, 2);
    // Words are not cut; existing breaks stay.
    CHECK_EQ(sub::wrapText("Ngắn\ngọn", style, 900), std::string("Ngắn\ngọn"));
    // Text without spaces (Japanese) still wraps.
    const std::string ja = sub::wrapText("これは字幕の自動改行をテストするためのとても長い日本語の文章です。", style, 600);
    CHECK(ja.find('\n') != std::string::npos);
}

TEST("subtitles: positions place the line at the bottom, top, custom, and above another track") {
    Editor ed = newEditor();
    const Id track = ed.addSubtitleTrack();
    const Id c = ed.addSubtitle(track, secs(1), "Một dòng phụ đề");
    auto rect = [&](const Id& id) {
        return generatedRectOnCanvas(ed.project(), *ed.project().findClip(id), secs(0.5));
    };
    const double H = 720;
    auto r = rect(c);
    CHECK_NEAR(r[1] + r[3], H - 0.06 * H, 1.0);  // bottom edge at the margin
    CHECK_NEAR(r[0] + r[2] / 2, 640, 1.0);
    // Two lines: still ends at the same place (grows upwards).
    ed.updateClipContent(c, {{"text", "Một dòng phụ đề\nvà dòng thứ hai"}});
    auto r2 = rect(c);
    CHECK_NEAR(r2[1] + r2[3], H - 0.06 * H, 1.0);
    CHECK(r2[3] > r[3] * 1.6);
    ed.setSubtitleSettings(track, {{"position", {{"anchor", "top"}, {"margin", 0.1}}}});
    CHECK_NEAR(rect(c)[1], 0.1 * H, 1.0);
    ed.setSubtitleSettings(track, {{"position", {{"anchor", "custom"}, {"x", 0.25}, {"y", 0.5}}}});
    CHECK_NEAR(rect(c)[0] + rect(c)[2] / 2, 320, 1.0);
    CHECK_NEAR(rect(c)[1] + rect(c)[3] / 2, 360, 1.0);
    ed.setSubtitleSettings(track, {{"position", {{"anchor", "bottom"}, {"x", 0.06}, {"h", "left"}}}});
    CHECK_NEAR(rect(c)[0], 0.06 * 1280, 1.0);
    // One line may sit elsewhere (over a caption burned in the video).
    ed.updateClipContent(c, {{"position", {{"anchor", "top"}, {"h", "center"}, {"x", 0.5}}}});
    CHECK(rect(c)[1] < H / 2);
    ed.updateClipContent(c, {{"position", nullptr}});
    ed.setSubtitleSettings(track, {{"position", {{"anchor", "bottom"}, {"x", 0.5}, {"h", "center"}, {"margin", 0.06}}}});

    // Bilingual: the translation stacks above the source line.
    std::map<Id, std::string> tr{{c, "One subtitle line\nand a second line"}};
    const Id en = ed.applySubtitleTranslation(track, tr, "en", Editor::TranslationMode::Bilingual);
    const Track* et = ed.project().findTrack(en);
    CHECK(et && et->clips.size() == 1u && ed.project().findTrack(track)->enabled);
    const Id ec = et->clips[0].id;
    const auto src = rect(c), top = rect(ec);
    CHECK(top[1] + top[3] <= src[1] + 0.5);
    CHECK(top[1] + top[3] > src[1] - 0.03 * H);
    // Hiding the source track drops the translation back to the bottom.
    ed.setTrackFlags(track, false, false, false);
    CHECK_NEAR(rect(ec)[1] + rect(ec)[3], H - 0.06 * H, 1.0);
    ed.undo();
    // New track mode hides the source; Replace changes the texts in place.
    const Id only = ed.applySubtitleTranslation(track, tr, "en", Editor::TranslationMode::NewTrack);
    CHECK(!ed.project().findTrack(track)->enabled && ed.project().findTrack(only)->enabled);
    ed.undo();
    CHECK(ed.project().findTrack(track)->enabled);
    ed.applySubtitleTranslation(track, tr, "en", Editor::TranslationMode::Replace);
    CHECK_EQ(ed.project().findClip(c)->content["text"], "One subtitle line\nand a second line");
}

TEST("subtitles: rendered at the bottom of the picture, fading in, every style drawing") {
    Editor ed = newEditor();
    const Id c = ed.addSubtitle({}, secs(1), "Phụ đề tiếng Việt");
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    FrameCache cache(32 << 20);
    Renderer r(ed.project(), pool, &cache);
    const VideoFrame shown = r.renderVideo(secs(2), 640, 360);
    const auto rows = inkRows(shown);
    std::printf("  ink rows %d..%d\n", rows.first, rows.second);
    CHECK(rows.first > 360 * 0.7 && rows.second < 360 * 0.96);
    // Fades in: almost nothing on the first frame, nothing before.
    const VideoFrame first = r.renderVideo(secs(1), 640, 360), before = r.renderVideo(secs(0.9), 640, 360);
    CHECK(inkRows(before).first < 0);
    long inkFirst = 0, inkShown = 0;
    for (std::size_t i = 0; i < shown.rgba.size(); i += 4) {
        inkFirst += first.rgba[i];
        inkShown += shown.rgba[i];
    }
    CHECK(inkFirst < inkShown / 3);
    // Cached still frame is identical.
    CHECK(r.renderVideo(secs(2), 640, 360).rgba == shown.rgba);
    // Moving the track to the top moves the line.
    ed.setSubtitleSettings(ed.project().trackOfClip(c)->id, {{"position", {{"anchor", "top"}}}});
    Renderer r2(ed.project(), pool, &cache);
    CHECK(inkRows(r2.renderVideo(secs(2), 640, 360)).second < 360 / 2);
    // Every style draws.
    CHECK(sub::subtitleStyles().size() >= 12u);
    CHECK(sub::subtitlePositions().size() >= 7u);
    for (const auto& s : sub::subtitleStyles()) {
        ed.setSubtitleSettings(ed.project().trackOfClip(c)->id, {{"style", s.style}});
        Renderer rs(ed.project(), pool);
        CHECK(inkRows(rs.renderVideo(secs(2), 320, 180)).first >= 0);
    }
}

TEST("subtitles: AI translation requests and replies for every connector") {
    Editor ed = newEditor();
    std::vector<sub::Cue> cues;
    for (int i = 0; i < 7; ++i) cues.push_back({secs(i), secs(i + 0.8), "Câu " + std::to_string(i)});
    const Id track = ed.importSubtitles(cues, "Gốc", "vi");
    const Track& t = *ed.project().findTrack(track);
    const auto batches = sub::makeBatches(t, "vi", "en", "Giữ nguyên tên riêng", 3, 2);
    CHECK_EQ(batches.size(), 3u);
    CHECK_EQ(batches[1].items.size(), 3u);
    CHECK_EQ(batches[1].items[0].id, std::string("1"));
    CHECK_EQ(batches[1].context.size(), 2u);
    CHECK_EQ(batches[1].context[1], std::string("Câu 2"));
    CHECK(sub::systemPrompt(batches[0]).find("English") != std::string::npos);
    CHECK(sub::systemPrompt(batches[0]).find("Giữ nguyên tên riêng") != std::string::npos);

    // Request shapes.
    const Json anthropic{{"provider", "anthropic"}, {"url", "https://api.anthropic.com/v1/messages"}, {"api_key", "k"}};
    auto req = sub::buildHttpRequest(anthropic, batches[0]);
    Json body = Json::parse(req.body);
    CHECK_EQ(body["model"], "claude-opus-5-5");
    CHECK(body["messages"][0]["role"] == "user" && body.contains("system"));
    bool hasKey = false, hasVersion = false;
    for (const auto& [k, v] : req.headers) {
        hasKey |= k == "x-api-key" && v == "k";
        hasVersion |= k == "anthropic-version";
    }
    CHECK(hasKey && hasVersion);
    const Json openai{{"provider", "openai"}, {"url", "http://localhost:11434/v1/chat/completions"}, {"model", "qwen"}};
    body = Json::parse(sub::buildHttpRequest(openai, batches[0]).body);
    CHECK(body["messages"].size() == 2u && body["model"] == "qwen");
    CHECK_THROWS(sub::buildHttpRequest({{"provider", "openai"}, {"url", "http://x"}}, batches[0]));  // no model
    CHECK_THROWS(sub::buildHttpRequest({{"provider", "openai"}}, batches[0]));                      // no url
    const Json gemini{{"provider", "gemini"}, {"url", "https://g/v1beta/"}, {"model", "m"}, {"api_key", "g"}};
    CHECK_EQ(sub::buildHttpRequest(gemini, batches[0]).url, std::string("https://g/v1beta/models/m:generateContent"));
    const Json custom{{"provider", "custom"}, {"url", "https://my.app/translate"}};
    body = Json::parse(sub::buildHttpRequest(custom, batches[0]).body);
    CHECK(body["items"].size() == 3u && body["target_lang"] == "en" && body["source_lang"] == "vi");

    // Replies, as each service sends them.
    const std::string items = R"({"items":[{"id":"1","text":"Line 0"},{"id":"3","text":"Line 2"},{"id":"2","text":"Line 1"}]})";
    std::vector<std::string> out;
    std::string err;
    Json reply{{"content", Json::array({{{"type", "thinking"}, {"thinking", ""}}, {{"type", "text"}, {"text", "```json\n" + items + "\n```"}}})},
               {"stop_reason", "end_turn"}};
    CHECK(sub::parseHttpResponse(anthropic, batches[0], reply.dump(), out, &err));
    CHECK(out.size() == 3u && out[0] == "Line 0" && out[1] == "Line 1" && out[2] == "Line 2");
    reply = {{"content", Json::array()}, {"stop_reason", "refusal"}};
    CHECK(!sub::parseHttpResponse(anthropic, batches[0], reply.dump(), out, &err));
    reply = {{"choices", Json::array({{{"message", {{"content", "Here you go: " + items}}}}})}};
    CHECK(sub::parseHttpResponse(openai, batches[0], reply.dump(), out, &err));
    reply = {{"candidates", Json::array({{{"content", {{"parts", Json::array({{{"text", items}}})}}}}})}};
    CHECK(sub::parseHttpResponse(gemini, batches[0], reply.dump(), out, &err));
    CHECK(sub::parseHttpResponse(custom, batches[0], items, out, &err));
    CHECK(sub::parseHttpResponse(custom, batches[0], R"({"translations":["a","b","c"]})", out, &err) && out[2] == "c");
    CHECK(!sub::parseHttpResponse(custom, batches[0], R"({"items":[{"id":"1","text":"x"}]})", out, &err));
    CHECK(err.find("Thiếu 2") != std::string::npos);
    CHECK(!sub::parseHttpResponse(openai, batches[0], R"({"error":{"message":"invalid key"}})", out, &err));
    CHECK(err.find("invalid key") != std::string::npos);
    CHECK(!sub::parseHttpResponse(openai, batches[0], "<html>", out, &err));

    // A job gathers the batches by clip and applies them as one step.
    sub::TranslationJob job(t, custom, "vi", "en", {}, 3);
    CHECK_EQ(job.batchCount(), 3u);
    for (std::size_t i = 0; i < job.batchCount(); ++i) {
        Json r{{"items", Json::array()}};
        for (const auto& it : job.batch(i).items) r["items"].push_back({{"id", it.id}, {"text", "EN " + it.text}});
        CHECK(job.accept(i, r.dump(), &err));
    }
    CHECK_EQ(job.doneCount(), 3u);
    CHECK_EQ(job.results().size(), 7u);
    const auto sourceCues = sub::cuesOf(t);  // `t` dangles once a track is added
    const Id en = ed.applySubtitleTranslation(track, job.results(), "en", Editor::TranslationMode::NewTrack);
    CHECK_EQ(sub::cuesOf(*ed.project().findTrack(en))[6].text, std::string("EN Câu 6"));
    CHECK(sub::cuesOf(*ed.project().findTrack(en))[6].start == sourceCues[6].start);
    CHECK(sub::providerPresets().size() >= 6u);
    CHECK(sub::languages().size() >= 15u);
}

TEST("subtitles: a translator module plugs in through the registry") {
    class Upper final : public sub::ISubtitleTranslator {
    public:
        bool translate(const sub::TranslateRequest& req, std::vector<std::string>& out, std::string*) const override {
            out.clear();
            for (const auto& it : req.items) out.push_back("[" + req.to + "] " + it.text);
            return true;
        }
    };
    ModuleRegistry reg;
    int created = 0;
    reg.add<sub::ISubtitleTranslator>({.id = "test.upper", .capabilities = {"translate.subtitle"}}, [&] {
        ++created;
        return std::make_shared<Upper>();
    });
    CHECK_EQ(created, 0);  // created only when used
    auto tr = reg.best<sub::ISubtitleTranslator>("translate.subtitle");
    CHECK(tr && created == 1);
    Editor ed = newEditor();
    const Id track = ed.importSubtitles({{secs(0), secs(1), "Một"}, {secs(1), secs(2), "Hai"}}, "Gốc");
    sub::TranslationJob job(*ed.project().findTrack(track), Json::object(), "vi", "ja");
    std::vector<std::string> out;
    CHECK(tr->translate(job.batch(0), out, nullptr));
    CHECK(job.acceptTexts(0, out, nullptr));
    CHECK_EQ(job.results().begin()->second.substr(0, 4), std::string("[ja]"));
}
