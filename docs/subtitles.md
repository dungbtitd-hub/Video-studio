# Subtitles and AI translation

Subtitles are typed, imported or translated right in the app, styled with the bundled fonts or
the user's own, and placed anywhere on the picture. They are drawn by the core like text, so the
preview, the export and every OS show the same thing (burned into the video on export; the
lines can also be saved as .srt / .vtt).

## Model

A **subtitle track** is a video track with a `subtitle` object; each **line** is a clip on it with
content `{"kind": "subtitle", "text": "..."}`.

```json
{"track_id": "…", "type": "video", "name": "Phụ đề", "subtitle": {
   "lang": "vi",
   "style": {"font": "be-vietnam-pro-bold", "size": 56, "color": "#ffffff",
             "stroke": {"color": "#000000", "width": 0.07}, "box": {"opacity": 0}, "in": {"type": "fade", "duration": 0.12}, …},
   "position": {"anchor": "bottom", "margin": 0.06, "x": 0.5, "h": "center"},
   "max_width": 0.88}}
```

* The look lives on the track: one edit restyles every line, whatever their number.
* A line can carry its own `style` / `position` (merged over the track's): "Chỉ dòng đang chọn".
* Lines are ordinary clips: move, trim, split, delete and undo work as for any clip.
* The style keys are the text keys (docs/text-stickers.md): font, size, colour, gradient, outline,
  shadow, glow, label box, in/out animations (a short fade by default, so lines never pop).
* Long lines wrap to `max_width` of the frame (balanced lines; Chinese, Japanese, Korean and Thai
  break between letters). The renderer resolves the look and the place for each frame
  (`subtitles::resolvedContent`, a few microseconds) and caches still frames like text.

### Positions

| `anchor` | Where |
|----------|-------|
| `bottom` / `top` | The block's lower / upper edge `margin` (fraction of the frame height) from the frame edge, however many lines it has |
| `middle` | Centred vertically |
| `custom` | Block centre at (`x`, `y`), 0..1 of the frame: dragging a line on the preview sets this |
| `above` | Right above the line shown at the same time on track `track` (`gap` between): bilingual subtitles |

`x` is the horizontal place, and `h` says which edge of the block it is (`left`, `center`,
`right`). The UI offers 7 presets (bottom left/centre/right, middle, top left/centre/right),
the margin slider, and free placement by dragging. Pulling a corner on the preview changes the
text size.

### Files

Import: SubRip (.srt), WebVTT (.vtt), Advanced SubStation (.ass/.ssa). Tags are dropped, overlapping
lines are trimmed and lines starting together are joined, so a track never holds overlapping clips.
Export: .srt and .vtt. Importing 3000 lines takes about 5 ms.

### Fonts

The 7 bundled OFL fonts, plus any TrueType/OpenType font the user adds ("Thêm phông…" on desktop;
`registerFont` in the C API). Desktop keeps added fonts in the app data folder and registers them
at start-up; the project stores the font id.

## AI translation

The core never opens a network connection. For each translation it splits the track into
batches (40 lines by default, with the 4 lines before each batch as context), builds the HTTP
request for the chosen connector, and reads the reply. The app only sends the bytes:
`TranslationClient` (Qt Network) on desktop, `TranslationRunner` (dart:io) on the phone. Three
batches go at once; a failed batch is retried twice (not after 401/403/404). Lines whose batch
failed keep their source text, and the result is one undo step:

* **Song ngữ** (bilingual): a new track stacked right above the source lines (`anchor: above`).
* **Track mới**: a new track with the same timing and look; the source track is hidden.
* **Thay thế**: the texts of the track are replaced.

Settings (address, model, key) are stored per connector on the device (QSettings / the app
support folder), never in the project.

### Connectors

| Connector | `provider` | Default address | Notes |
|-----------|-----------|-----------------|-------|
| Claude (Anthropic) | `anthropic` | https://api.anthropic.com/v1/messages | Messages API, model `claude-opus-5-5`, low effort; on api.anthropic.com a declined batch falls back to another model server-side |
| OpenAI (ChatGPT) | `openai` | https://api.openai.com/v1/chat/completions | Any model name |
| Google Gemini | `gemini` | https://generativelanguage.googleapis.com/v1beta | `models/<model>:generateContent`, JSON reply |
| DeepSeek, OpenRouter, Groq, Azure, … | `openai` | the service's `/chat/completions` | Any OpenAI-compatible service |
| Ollama / LM Studio (on the computer) | `openai` | http://localhost:11434/v1/chat/completions, http://localhost:1234/v1/chat/completions | Offline, no key |
| **Open protocol** (your app or server) | `custom` | your URL | Below |
| **Program on this computer** (desktop) | `command` | — | Open protocol over stdin/stdout |

Chat models get one system prompt (subtitle translator rules, the editor's notes such as names or
how people address each other) and the batch as JSON; they must answer
`{"items": [{"id", "text"}]}`. Code fences or prose around the JSON are tolerated.

### The open protocol

Any app can translate for Video Studio by answering one POST:

```http
POST <your URL>
Content-Type: application/json
Authorization: Bearer <key>          (only when a key is set)

{"source_lang": "vi", "target_lang": "en",
 "items": [{"id": "1", "text": "Xin chào"}, {"id": "2", "text": "Tạm biệt"}],
 "context": ["the lines just before, for meaning"],
 "notes": "glossary, names, tone (may be empty)",
 "model": "(what the user typed in Model, may be empty)"}
```

```json
{"items": [{"id": "1", "text": "Hello"}, {"id": "2", "text": "Goodbye"}]}
```

Also accepted: `{"translations": ["Hello", "Goodbye"]}` (same order). On failure, answer
`{"error": {"message": "..."}}`; the message is shown to the user. Every item must come back.

The **command** connector sends the same request on stdin and reads the reply from stdout (exit
code 0). `tools/subtitle-translate/echo_translator.py` is a working example of both forms
(`--serve 8765` for HTTP); replace its `translate()` with a call to any AI.

### In-process translators

A module can translate without the network (an offline model): implement
`subtitles::ISubtitleTranslator` (module kind `translator`, capability `translate.subtitle`) and
register it in the `ModuleRegistry`; it is created only when used. Its results go into the same
`TranslationJob` (`acceptTexts`).

## Using it

- **Desktop:** the "Phụ đề" panel.
  - Track picker and "＋ Track"; "＋ Thêm dòng" adds a line at the playhead (Ctrl+Enter while typing
    adds the next line right after); "Nhập tệp…", "Xuất…". Opening the app with an .srt/.vtt/.ass
    file on the command line imports it.
  - The list shows every line (start, end, text): double-click to edit, click to jump there.
  - Styles (14), font (+ "Thêm phông…"), size, colour, outline, background; 7 positions and the margin;
    "Chỉ áp dụng cho dòng đang chọn"; "Dời cả track" to resync.
  - "Dịch bằng AI…": connector, address, model, key or command, from/to language, notes, result
    mode, lines per request; progress bar and "Dừng".
  - On the preview, drag a line to place the subtitles anywhere; pull a corner to resize.
- **Mobile:** the "Phụ đề" tool, four tabs: Dòng (add, type, list, import/export), Kiểu, Vị trí
  (presets, margin, ±0.1/0.5 s resync) and Dịch AI. Drag or pinch the line on the preview.

## C API

`vs_subtitle_styles_json`, `vs_subtitle_positions_json`, `vs_subtitle_languages_json`,
`vs_translation_providers_json`, `vs_add_subtitle_track`, `vs_add_subtitle`, `vs_import_subtitles`,
`vs_export_subtitles`, `vs_set_subtitle_settings`, `vs_shift_subtitles`, and the translation job:
`vs_translation_begin`, `vs_translation_count`, `vs_translation_done`, `vs_translation_request_json`,
`vs_translation_accept`, `vs_translation_error`, `vs_translation_apply`, `vs_translation_free`.
