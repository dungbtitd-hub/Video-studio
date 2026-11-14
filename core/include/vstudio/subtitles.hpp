// Subtitles: a subtitle track is a video track whose `extra["subtitle"]` object holds the
// look shared by every line on it (style, position, wrapping, language). Each line ("cue")
// is a generated clip on that track with content {"kind": "subtitle", "text": "..."} and,
// when one line must look or sit differently, its own "style" / "position" overrides.
//
// Because cues are ordinary clips, the timeline moves, trims, splits and undoes them like
// any other clip, and the renderer draws them through the generator interface
// ("generate.subtitle"). Changing the track style changes every line at once (one edit of
// a few bytes, whatever the number of lines).
//
// Positions: "bottom" / "top" / "middle" keep the block's edge `margin` (fraction of the
// canvas height) from the frame edge whatever the number of lines; `x` (0..1) sets the
// horizontal centre. "custom" puts the block centre at (`x`, `y`). "above" stacks the block
// right above the line shown at the same time on another subtitle track (bilingual
// subtitles: the translation under the original, or the other way round).
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "vstudio/model.hpp"

namespace vstudio::subtitles {

struct Cue {
    Time start;
    Time end;
    std::string text;  // lines split on '\n'
};

// ---- files ----
// SubRip (.srt). Tolerates BOM, CRLF, missing indices, ',' or '.' decimals, <i>/<b>/<font> tags (stripped).
std::vector<Cue> parseSrt(const std::string& text);
// WebVTT (.vtt): cue settings and voice/class tags are dropped, NOTE/STYLE/REGION blocks skipped.
std::vector<Cue> parseVtt(const std::string& text);
// Advanced SubStation (.ass/.ssa) "Dialogue:" lines; override tags {\...} dropped, \N = new line.
std::vector<Cue> parseAss(const std::string& text);
// Picks the format from the content (WEBVTT header, [Script Info], else SRT).
std::vector<Cue> parseAny(const std::string& text);
std::string toSrt(const std::vector<Cue>& cues);
std::string toVtt(const std::vector<Cue>& cues);
// Sorts by start; joins lines that start together; trims an overlap so no line covers the
// next; drops empty and zero-length lines. Tracks never hold overlapping clips.
std::vector<Cue> normalise(std::vector<Cue> cues, Rational fps);

// ---- tracks ----
inline constexpr const char* kExtraKey = "subtitle";
[[nodiscard]] bool isSubtitleTrack(const Track& t);
// {"lang", "style": {text keys}, "position": {"anchor", "margin", "x", "y", "track", "gap"},
//  "max_width": 0.88 (fraction of canvas width before wrapping)}.
Json defaultTrackSettings(const std::string& lang = {});
// Cue content for `text`.
Json cueContent(const std::string& text);
// The lines of a subtitle track in timeline time.
std::vector<Cue> cuesOf(const Track& t);

// One-tap subtitle looks: a style patch merged over the track style (font kept unless set).
struct Style {
    std::string id;
    std::string name;
    Json style;
};
const std::vector<Style>& subtitleStyles();
// Named positions for the UI ({"anchor", "x", ...} patches).
struct Position {
    std::string id;
    std::string name;
    Json position;
};
const std::vector<Position>& subtitlePositions();

// What a cue draws: the track look with the cue's overrides, wrapped to the track width,
// as text content plus "offset": [dx, dy] (canvas px) that puts it at its anchor.
// Cheap (a few microseconds): the renderer calls it for every frame a cue is on screen.
Json resolvedContent(const Project& p, const Track& t, const Clip& c);

// Inserts line breaks so no line of `text` is wider than `maxWidthPx` with this style
// (content keys "font", "size", "letter_spacing"). Existing breaks are kept.
std::string wrapText(const std::string& text, const Json& style, double maxWidthPx);

// ---- translation (AI) ----
// The core never touches the network. A translator module (kind "translator") does the work
// in process, or the app sends the HTTP requests built here and hands back the replies.

struct TranslateItem {
    std::string id;    // short key, unique in the batch ("1", "2", ...)
    std::string text;
};
struct TranslateRequest {
    std::string from;                  // language code or name; "" = detect
    std::string to;                    // e.g. "vi", "en", "ja"
    std::vector<TranslateItem> items;  // the lines to translate, in order
    std::vector<std::string> context;  // the lines just before (already translated or not), for meaning
    std::string notes;                 // glossary, names, tone ("xưng hô anh/em", ...)
};

// In-process translator (an offline model, a company service SDK...). Registered in the
// ModuleRegistry with capability "translate.subtitle"; created only when used.
class ISubtitleTranslator {
public:
    static constexpr const char* kModuleKind = "translator";
    virtual ~ISubtitleTranslator() = default;
    // One text per item, same order. False (with *error) when it failed: nothing is applied.
    virtual bool translate(const TranslateRequest& req, std::vector<std::string>& out, std::string* error) const = 0;
};

// Splits the track's lines into requests of at most `batchSize` lines, each with the
// `contextLines` lines before it as context. Item ids are "1".."n" within a batch.
std::vector<TranslateRequest> makeBatches(const Track& t, const std::string& from, const std::string& to,
                                          const std::string& notes, int batchSize = 40, int contextLines = 4);

// HTTP connectors to AI services. `config`: {"provider": "openai" | "anthropic" | "gemini" |
// "custom", "url", "api_key", "model", "headers": {...extra}}. "openai" is any OpenAI-compatible
// Chat Completions endpoint (OpenAI, DeepSeek, Groq, OpenRouter, Ollama, LM Studio...).
// "custom" is the Video Studio open protocol (docs/subtitles.md): POST
// {"source_lang", "target_lang", "items": [{"id", "text"}], "context", "notes"} -> {"items": [{"id", "text"}]}.
struct HttpRequest {
    std::string method = "POST";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};
// The connectors the UI offers, with their default url and model: [{"id", "name", "provider", "url",
// "model", "needs_key", "hint"}].
const Json& providerPresets();
// Throws std::invalid_argument for an unusable config (no url, unknown provider).
HttpRequest buildHttpRequest(const Json& config, const TranslateRequest& req);
// Reads the reply body. Fills one text per item (in order) and returns true when every item came
// back; else false with *error (wrong JSON, refusal, missing lines: the batch can be retried).
bool parseHttpResponse(const Json& config, const TranslateRequest& req, const std::string& body,
                       std::vector<std::string>& out, std::string* error);
// The instructions sent to chat models (system prompt) and the user message for a batch.
std::string systemPrompt(const TranslateRequest& req);
std::string userMessage(const TranslateRequest& req);

// Gathers the batches' results into {clip id -> translated text} for Editor::applySubtitleTranslation.
class TranslationJob {
public:
    TranslationJob(const Track& source, Json config, const std::string& from, const std::string& to,
                   const std::string& notes = {}, int batchSize = 40);
    [[nodiscard]] std::size_t batchCount() const { return batches_.size(); }
    [[nodiscard]] const TranslateRequest& batch(std::size_t i) const { return batches_.at(i); }
    [[nodiscard]] HttpRequest request(std::size_t i) const { return buildHttpRequest(config_, batches_.at(i)); }
    // Reply for batch i; false with *error when it must be retried.
    bool accept(std::size_t i, const std::string& body, std::string* error);
    // Result of an in-process translator for batch i.
    bool acceptTexts(std::size_t i, const std::vector<std::string>& texts, std::string* error);
    [[nodiscard]] bool done(std::size_t i) const { return done_.at(i); }
    [[nodiscard]] std::size_t doneCount() const;
    [[nodiscard]] const std::map<Id, std::string>& results() const { return results_; }
    [[nodiscard]] const std::string& sourceTrack() const { return trackId_; }
    [[nodiscard]] const std::string& targetLanguage() const { return to_; }

private:
    Id trackId_;
    Json config_;
    std::string to_;
    std::vector<TranslateRequest> batches_;
    std::vector<std::vector<Id>> clipIds_;
    std::vector<bool> done_;
    std::map<Id, std::string> results_;
};

// Language list for pickers: [{"code", "name"}] (Vietnamese names).
const Json& languages();

}  // namespace vstudio::subtitles
