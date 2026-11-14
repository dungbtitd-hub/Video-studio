# Covering part of the picture

Blur a logo, pixelate a face, put a black bar over a number plate. A clip can carry any number of covers. Each
cover has its own region, shape and style, and can follow a moving subject. Preview and export use the same code
on all four OSes.

## Model

Each cover is one effect on the clip: `{"type": "builtin.cover", "effect_id": ..., "params": {...}}`.

| Param | Meaning |
|-------|---------|
| `x, y, w, h` | Region in the **source** frame, 0..1. The cover stays on the subject when the clip is zoomed or moved by keyframes. |
| `shape` | One of 11: `rect`, `rounded`, `ellipse`, `circle`, `diamond`, `triangle`, `hexagon`, `star`, `heart`, `line` (split: the side below the line through the region's centre, turned by `rotation`), `band` (a strip of height `h` across the whole frame) |
| `style` | One of 36 styles in 5 groups (below) |
| `amount` | Strength, 0..1, independent of resolution |
| `feather` | Soft edge, 0..1 of the region's smaller half size (`line`/`band`: of a quarter of the frame's smaller side) |
| `rotation` | Degrees about the region's centre |
| `invert` | `true` covers everything **except** the region |
| `color` | `#rrggbb` for the solid-colour styles |
| `anim` | Keyframes for `x, y, w, h, rotation, amount`, in source time (same format as `Clip::keyframes`) |

### The 38 styles

| Group | Styles |
|-------|--------|
| Làm mờ | blur, blur_strong, frosted, frosted_dark, motion, zoom_blur, glow, bokeh |
| Khảm | mosaic, mosaic_large, dots, hex, crystal, triangles, glass_blocks, halftone |
| Che kín | black, white, color, gradient, stripes, checker |
| Họa tiết (animated) | noise, glitch, sparkle, hearts, ripple, snow |
| Mặt nạ (masks) | mask_keep, mask_cut |
| Nghệ thuật | posterize, sketch, emboss, thermal, invert, night, neon, oil |

The work is done only inside the region's bounding box, plus the blur margin. A small cover on a 4K frame stays
cheap. The edge mask is a signed distance field smoothed by `feather`, so every shape has a clean, soft edge.

## Following a moving subject

There are two ways, and both produce ordinary keys that can be edited afterwards:

1. **Keyframes ("Theo keyframe").** Move the region at a few moments. In between, it glides linearly.
2. **Tracking ("Bám theo").** A built-in tracker follows the region from the playhead to the end of the clip.
   - It runs on a background job, so the UI never waits for it.
   - It matches a ~40 px template (normalised cross-correlation): a coarse pass at half resolution, then a fine
     pass ±2 px at 3 scales.
   - The template adapts slowly and stays anchored to the first look, which limits drift.
   - The search window and the size drift (½×..2×) are bounded, so one frame always costs about the same.
   - The track is cut where confidence stays low for 4 frames. The part tracked so far is kept and the user is
     told.
   - The samples are reduced to a few keys with Douglas–Peucker and applied as **one undo step**.

Automatic face detection is planned later. It needs a decision between the platform detectors (Apple Vision,
ML Kit) and one shared model.

## Using it

- **Desktop:** the "Che mờ" panel, docked beside "Bộ lọc".
  - Pick a style from the 36 thumbnails and a shape.
  - Drag the region on the preview: drag inside to move, drag a corner to resize, or draw a new box. Esc cancels.
  - Sliders for strength, soft edge and rotation. Each one commits on release, as one undo step.
- **Mobile:** the "Che mờ" tool in the bottom bar.
  - One finger moves the region, the bottom-right handle resizes it, and two fingers scale it.
  - The style strip, shape chips, strength slider, "Bám theo", "Theo keyframe", "Che bên ngoài" and delete sit
    under the preview.

## C API

`vs_cover_styles_json`, `vs_cover_shapes_json`, `vs_add_cover`, `vs_update_cover`, `vs_remove_cover`,
`vs_set_cover_at`, `vs_key_cover`, `vs_clear_cover_keys`, `vs_cover_state_at`, `vs_covers_json`,
`vs_cover_canvas_rect`, `vs_set_cover_canvas_rect`, `vs_cover_preview`, `vs_track_cover_start`, `vs_track_status`,
`vs_track_cancel`.

Tracking results are applied on the editing thread, by the `vs_track_status` call that first sees the job
finished.

## Masks

The two styles in the **Mặt nạ** group change only the clip's transparency, so the tracks below show through.
`mask_keep` keeps the region and makes the rest transparent; `mask_cut` makes the region transparent. `invert`
swaps the two. Everything else is shared with covers: all 11 shapes, soft edge, rotation, keyframes and following
a moving subject. Colours are never touched, so a mask commutes with the colour grade, Look and chroma key that run
in the GPU compositor. On mobile the **Mặt nạ** tool opens the same panel and starts with a soft oval mask.
