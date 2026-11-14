# Video effects ("Hiệu ứng video")

Effects that move the picture inside its frame, for video and photo clips. Colour is left alone
(that is the looks' job). Three groups, and a clip can carry one effect of each, so a slow zoom, a
gentle sway and a water reflection can play together.

| Group | Effects |
|---|---|
| Phóng to (zoom) | Phóng to dần, Thu nhỏ dần, Phóng nhanh, Nhịp đập, Thở chậm, Trôi ngang, Rung máy |
| Xoay (rotate) | Lắc lư, Nghiêng dần, Xoáy vào, Xoay tròn, Xoay 90° phải / trái, Xoay 180°, Lật ngang, Lật dọc |
| Phản chiếu (reflect) | Mặt nước, Hồ gợn sóng, Gương phẳng, Phản chiếu lên trên, Đối xứng trái phải, Kính vạn hoa |

**Water reflection:** the picture is cut at the water line and the part above is flipped below it,
slightly darker, with ripples that grow towards the viewer and thin light streaks. "Mặt nước" sets
where the surface sits (10–90 % of the height; 62 % by default, 40 % for "Phản chiếu lên trên").

Every effect has **Mức độ** (amount, 0–200 %) and **Tốc độ** (speed, 0.25–4×). Slow zoom in/out and
slow tilt run over the whole clip, so they finish at its end whatever its length.

## Using it

* **Desktop:** the "Hiệu ứng video" panel (tab next to "Bộ lọc màu"). Pick a group, then click a
  thumbnail. Thumbnails show the effect on the selected clip's own frame and play while the mouse
  is over them. Sliders commit on release (one undo step). "Bỏ hiệu ứng nhóm này" removes it.
* **Mobile:** the "Hiệu ứng" tool (needs a selected video or photo clip). Group chips, a strip of
  thumbnails; the chosen one plays in its tile. Tap applies at once; sliders commit when the finger
  lifts.

## How it stays smooth

* Every frame is a pure function of time: no state between frames, so seeking, preview and export
  give exactly the same picture.
* One pass per effect: an inverse mapping with bilinear sampling (no intermediate copies). Large
  frames are split across up to 4 threads. About 15 ms for a 1080p frame on one core; reflections
  precompute their ripple per column.
* Zoom never shows the frame edge: the picture is scaled to cover the frame for any rotation.
* The effects run after the colour effects and covers (a blurred region stays on its subject), in
  the fixed order zoom → rotate → reflect.

## Code

* `core/include/vstudio/video_fx.hpp`, `core/src/video_fx.cpp`: catalogue, `applyVideoFx`,
  `drawVideoFxIcon`, and three effect modules `builtin.fx_zoom`, `builtin.fx_rotate`,
  `builtin.fx_reflect` (params `fx`, `amount`, `speed`, `line`; the renderer adds `clip_length`).
* `Editor::setClipVideoFx(clips, group, fx, amount, speed, line)`: replaces the group's effect
  (`""` removes it), one undo step.
* C API: `vs_video_fx_json`, `vs_set_clip_video_fx`, `vs_video_fx_icon`.
* Desktop: `desktop/widgets/VideoFxPanel`. Mobile: `mobile/lib/ui/video_fx_sheet.dart`.
