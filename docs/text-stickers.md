# Text and stickers

Text and stickers are clips without media (overlay effects work the same way: see [overlays.md](overlays.md)). The core draws them itself, as vectors, at the output size, so they stay
sharp at any zoom and look the same in the preview and the export, on all four OSes. Every one comes in and goes out
with an eased animation: nothing pops on or off the picture.

## Model

A text or sticker clip has an empty `assetId` and a `content` object that says what it draws:

```json
{"kind": "text", "text": "Xin chào", "font": "be-vietnam-pro-bold", "size": 96, "color": "#ffffff",
 "stroke": {"color": "#000000", "width": 0.06},
 "in": {"type": "fade", "duration": 0.4}, "out": {"type": "fade", "duration": 0.4}, "loop": {"type": "", "speed": 1}}
```

* It sits on a video track like any clip. A new one goes on the topmost free overlay track (never the bottom video
  layer), or on a new top track named "Chữ" / "Sticker". Adding it is one undo step.
* Moving and scaling it uses the clip's own transform (`x`, `y`, `scale`), so keyframes work on it like on video.
* Editing it is a JSON merge patch into `content` (one undo step; `null` removes a key).
* Drawing goes through the generator module interface (`IClipGenerator`, capability `generate.<kind>`). The built-in
  `builtin.text` and `builtin.sticker` can be replaced or joined by external modules.
* A still frame (no animation playing, sticker not "alive") is cached like a decoded frame, so a long title costs
  nothing after its first frame.

### Text content

| Key | Meaning |
|-----|---------|
| `text`, `font`, `size` | What it says (lines split on `\n`), font id, size in canvas px |
| `color`, `color2` | Fill; with `color2`, a top-to-bottom gradient |
| `align`, `italic`, `letter_spacing`, `line_height`, `rotation` | Layout |
| `stroke {color, width}` | Outline; `width` is the visible thickness outside the letters, in em |
| `shadow {color, opacity, blur, x, y}` | Drop shadow |
| `glow {color, opacity, radius}` | Glow (the neon presets) |
| `box {color, opacity, padding, radius}` | Rounded label behind the text |
| `in`, `out {type, duration}`, `loop {type, speed}` | Animations, below |

### Sticker content

`{"kind": "sticker", "sticker": "<id>", "size": 320, "rotation": 0, "flip": false, "alive": true, "in": …, "out": …,
"loop": …}`. With `alive`, the sticker plays its own idle motion: a heart beats, a flower sways, a star twinkles.
Stickers that draw themselves (arrows, ticks) draw once and then hold, so they never restart with a pop.

## Animations

The same 31 animations work for the in and the out. The out plays the in backwards, so a text leaves the way it came.
When the in and out are longer than the clip, both shrink to fit.

| Group | Animations |
|-------|------------|
| Whole text | fade, zoom_in, zoom_out, pop, blur_in (blurred to clear), zoom_blur, slide_up/down/left/right, spin_in, flip_in, bounce_in, elastic, stretch, swing, wipe_right, wipe_up |
| Letter by letter | typewriter, fade_letters, rise_letters, drop_letters, pop_letters, blur_letters, wave_in, rotate_letters, jump_letters, shutter, scatter, split, tracking |

Loops play while the text or sticker is on screen: pulse, heartbeat, float, swing, shake, jelly, blink, and, letter
by letter, wave and bounce. A loop fades in over 0.35 s after the in animation, so it never jumps.

All motion is eased (smoothstep or cubic; pop overshoots a little and elastic settles like a spring). Blur and wipe edges are soft.

## Text presets

16 one-tap looks: Cơ bản, Viền đen, Bóng đổ, Nhãn đen, Nhãn trắng, Phụ đề, Vàng viền, Neon hồng, Neon xanh, Hoàng
hôn, Biển xanh, Kẹo ngọt, Truyện tranh, Viết tay, Chữ ký vàng, Cổ điển. A preset is a style patch merged into the
content; it keeps the text and the animations.

## Fonts

Seven bundled fonts, all under the SIL Open Font License 1.1 (see `core/assets/fonts/`): Be Vietnam Pro (Regular, Bold,
ExtraBold), Bungee, Lobster, Pacifico, Patrick Hand. Each covers Vietnamese; combining marks are composed to the
precomposed letters before shaping.

A company font can be added at run time with `vs_register_font(id, name, bytes, size)` (TrueType or OpenType outlines).
Projects store the font id, so the same font must be registered on every device that opens the project.

## Stickers

About 340 stickers in 24 groups, all drawn in-house as vectors: Mặt cười, Mạng xã hội, Trái tim, Hoa lá, Sao lấp
lánh, Thời tiết, Lễ hội, Mũi tên & dấu, Đồ ăn, Động vật, Nhãn khuyến mãi, Chữ hiệu ứng, Vẽ tay, Video & kênh, Thể
thao & game, Tết & mùa lễ, Du lịch & đời sống, Game 8-bit, Thập niên 80, Động vật biểu cảm, Đồ uống & đời sống, Sinh
nhật, Giáng sinh, Ngày lễ trong năm. The groups follow what video apps commonly offer.

* **Game 8-bit** stickers are drawn on a pixel grid with a 5x7 pixel font (coins, chests, "PRESS START", "K.O.",
  a counting high score); **Thập niên 80** adds the neon sunset, boombox, cassette and disco ball.
* **Động vật biểu cảm** are reaction faces (a "wow" cat, a laughing dog, a crying hamster...) built from one head
  and face kit, so eyes blink, mouths move and tears, hearts or sweat drops play in a loop.
* **Đồ uống & đời sống** (phin coffee, trà đá, matcha, bánh mì, phở...) and the event groups (birthdays, Christmas,
  Valentine, 8/3, 20/10, 20/11, 1/6, Trung thu, Halloween, graduation, weddings, New Year, Mother's Day) mix
  drawings with Vietnamese or English greetings in the bundled fonts.

* **Mạng xã hội** buttons (Đăng ký, Thích, Theo dõi, Chia sẻ, Thả tim, in Vietnamese and English) play a click once:
  a hand taps, the button changes colour and text smoothly, the icon reacts, then it holds. They are generic designs
  with no platform logos or names (decided 2026-11-14, to keep store releases safe).
* Text inside stickers (labels, comic words, Tết greetings) uses the bundled fonts, so Vietnamese renders the same
  everywhere.
* Self-drawing stickers (hand-drawn marks, the countdown, the clapper) play once and hold their last frame.
* The drawing kit shared by the groups is `core/src/sticker_kit.hpp`; new groups go in their own file (`stickers_more.cpp`,
  `stickers_extra.cpp`) and register through `kit::Add`.

## Using it

- **Desktop:** the "Chữ" and "Sticker" panels.
  - "Thêm chữ" adds a text at the playhead. Type in the box; the change is committed after a short pause.
  - Font, size, colour, alignment, outline, shadow and label sliders, and the preset grid.
  - Sticker panel: pick a group, hover an icon to see it move, click to add. Size, flip and "alive".
  - Both panels have the animation box: in, out and loop, the two lengths, and a preview that plays in → rest → out.
  - On the preview, drag the selected text or sticker to move it, or pull a corner to scale it about its centre;
    it follows the mouse live (see below).
- **Mobile:** the "Chữ" and "Sticker" tools in the bottom bar.
  - "Chữ" edits the selected text, or adds one. The sheet has three tabs: Chữ (text, font, size, alignment), Kiểu
    (presets, colours, outline, shadow, label) and Hiệu ứng (in, out, loop with a playing preview).
  - "Sticker" opens the library; a tap adds the sticker at the playhead, then the Hiệu ứng tab sets size, flip,
    "alive" and the animations.
  - On the preview, drag the box to move, pinch or pull the corner to scale, double-tap to edit. The change is one
    edit when the fingers lift (a keyframe when the clip's picture is animated).

### Resizing follows the hand

Stickers and text grow, shrink and move with the mouse or fingers while the gesture runs, and the size, outline,
shadow and label sliders change the picture while they move: nothing jumps on release. During the gesture the core
renders the result of the edit on a scratch copy (`Editor::preview`, C API `vs_live_*`); the project and the undo
history change only when the gesture ends, as one undo step. Esc (desktop) or a refused edit drops the live preview.

The sticker "Cỡ" slider shows the size as seen on the canvas (content size × scale at the playhead, 20 to 2000 px),
so it matches a corner drag or a pinch; moving it sets the content size for the current scale. Stickers are vectors
drawn at the output resolution, so they stay sharp at any size.

## Following a moving object (FR-028)

A text or sticker can stick to something that moves in the video below it: a name over a runner, an arrow on a
ball.

1. Place the text or sticker on the object, at the moment it should start following.
2. Press **"Bám theo đối tượng"** (desktop: the animation box; mobile: the Hiệu ứng tab of the text or sticker
   sheet). Tick **"cả kích thước"** to also follow the object getting nearer or further.

How it works (`core/include/vstudio/tracker.hpp`: `followTargetAt`, `followStartBox`, `trackFollow`):

- The target is the topmost video clip under the follower at the playhead. The start box is a square around the
  follower's centre, mapped into that clip's **source** frame, so zoom, position keys and speed of the video clip are
  taken into account.
- The same tracker as covers ([covers.md](covers.md)) runs in a background job from the playhead to the end of the
  follower: the UI never waits, the button shows progress and can stop it.
- Each tracked box is mapped back to the canvas, and the follower keeps its offset from the object (and its scale
  ratio with "cả kích thước"). The samples are reduced with Douglas–Peucker to a few `x`, `y` (and `scale`) keys in
  the follower's source time, applied by `Editor::setFollowTrack` as **one undo step**. Keys of those properties
  inside the tracked span are replaced; keys outside it stay.
- When the object is lost (low confidence for 4 frames), the part followed so far is kept and the user is told.

The result is ordinary keyframes: they can be moved, eased or deleted like any other.

C API: `vs_tool_command` `{"cmd": "start", "job": "follow", "clip", "at", "size"}`, then `job_status` until it is
done (the keys are applied by the call that first sees the job finished).

## C API

`vs_fonts_json`, `vs_register_font`, `vs_text_animations_json`, `vs_text_presets_json`, `vs_stickers_json`,
`vs_default_text_json`, `vs_default_sticker_json`, `vs_add_generated_clip`, `vs_update_clip_content`,
`vs_generated_rect`, `vs_sticker_icon`, `vs_content_preview`.
