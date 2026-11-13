# Keyframes inside one clip

Zoom in on a subject, then back out, or bring the music up and fade it down,
all inside one clip and without cutting it. The same keys drive preview and
export on all four OSes.

## Model

`Clip::keyframes = { "<property>": [ {"t": <source ticks>, "v": <value>, "ease": "smooth"} ] }`

| Group   | Properties                 | Notes                                         |
|---------|----------------------------|-----------------------------------------------|
| Picture | `scale`, `x`, `y`, `opacity` | x/y in canvas pixels from the centre        |
| Audio   | `volume`                   | gain (stored as a factor of the clip volume) |

* The two groups are independent. ◇ Hình adds or removes picture keys; ◇ Âm thanh adds or removes audio keys.
* Keys are stored in **source** time. They stay on the same picture when the clip is trimmed, moved or sped up, and
  both halves of a split keep the motion and continue it seamlessly.
* Before the first key, the first value holds. After the last key, the last value holds. A clip only animates
  between its keys.

## Why the zoom looks smooth

* **Easing** (`smooth`, the default) is smoothstep. Speed is zero at both keys, so the zoom starts and ends gently,
  with no jolt where the still part meets the moving part. `linear` and `hold` are also available.
* **Zoom is interpolated geometrically.** Going from ×1 to ×2 takes as long as going from ×2 to ×4, so the zoom
  speed looks even.
* **Position follows the zoom about a fixed pivot.** The chosen subject travels in a straight line to its place
  and does not drift sideways.
* **Audio gain** is evaluated every 32 samples and ramped linearly in between, so fades have no steps or clicks.

## Editing (auto-key)

`Editor::setPropertyAt(clip, t, values)` behaves differently depending on whether the property is animated:

* If it is animated, it adds or updates a key at `t`.
* If it is not, it changes the clip's static value.

Every UI goes through this one call.

* **Desktop.** Drag on the preview to draw a box with the frame's shape. The area inside it fills the frame
  (`zoomToRegion`). The keyframe bar under the transport has ◀◆ / ◆▶ to jump between keys, ◇ Hình, ◇ Âm thanh,
  Bỏ zoom and a volume slider.
  * Shortcuts: Ctrl+K and Ctrl+Shift+K toggle keys; `[` and `]` jump between them.
  * Diamonds are drawn on the timeline: picture keys on the top edge, audio keys on the bottom edge. Clicking one
    seeks to it.
* **Mobile.** Pinch and drag with two fingers on the preview. While the fingers move, only the shown image is
  transformed, so nothing is decoded. On release, one edit is committed. Double-tap resets the zoom. The bar and
  the timeline diamonds work as on desktop.

Typical flow:

1. Put the playhead where the zoom should start and press ◇ Hình.
2. Move to where it should be fully zoomed and draw a box (desktop) or pinch (mobile).
3. Optionally, zoom back out further on with Bỏ zoom or a new box.

## C API

All calls take timeline ticks.

* `vs_add_keyframe`, `vs_remove_keyframe`, `vs_clear_keyframes` (group 0 = picture, 1 = audio)
* `vs_set_property_at` (JSON values)
* `vs_zoom_to_region` (normalised rectangle)
* `vs_clip_state_at` (values and flags at a time)
