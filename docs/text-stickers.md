# Text and stickers

Text and stickers are clips without media. The core draws them itself, as vectors, at the output size, so they stay
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

About 135 stickers in 9 groups, all drawn in-house as vectors: Mặt cười, Trái tim, Hoa lá, Sao lấp lánh, Thời tiết,
Lễ hội, Mũi tên & dấu, Đồ ăn, Động vật.

## Using it

- **Desktop:** the "Chữ" and "Sticker" panels.
  - "Thêm chữ" adds a text at the playhead. Type in the box; the change is committed after a short pause.
  - Font, size, colour, alignment, outline, shadow and label sliders, and the preset grid.
  - Sticker panel: pick a group, hover an icon to see it move, click to add. Size, flip and "alive".
  - Both panels have the animation box: in, out and loop, the two lengths, and a preview that plays in → rest → out.
  - On the preview, drag the selected text or sticker to move it, or pull a corner to scale it about its centre.
- **Mobile:** the "Chữ" and "Sticker" tools in the bottom bar.
  - "Chữ" edits the selected text, or adds one. The sheet has three tabs: Chữ (text, font, size, alignment), Kiểu
    (presets, colours, outline, shadow, label) and Hiệu ứng (in, out, loop with a playing preview).
  - "Sticker" opens the library; a tap adds the sticker at the playhead, then the Hiệu ứng tab sets size, flip,
    "alive" and the animations.
  - On the preview, drag the box to move, pinch or pull the corner to scale, double-tap to edit. The change is one
    edit when the fingers lift (a keyframe when the clip's picture is animated).

## C API

`vs_fonts_json`, `vs_register_font`, `vs_text_animations_json`, `vs_text_presets_json`, `vs_stickers_json`,
`vs_default_text_json`, `vs_default_sticker_json`, `vs_add_generated_clip`, `vs_update_clip_content`,
`vs_generated_rect`, `vs_sticker_icon`, `vs_content_preview`.
