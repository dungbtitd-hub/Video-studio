# Overlay effects

Overlay effects are animated layers laid over the video: falling snow, snowflakes, rain, fog, autumn leaves, cherry
blossom, Tết apricot flowers, confetti, gold glitter, balloons, fireworks, rising hearts, sparkles, fireflies, bokeh,
light leaks, shooting stars, soap bubbles, an old-film look and a vignette (21 in four groups: Mùa & thời tiết, Lễ
hội, Ánh sáng, Phong cách phim).

## Model

An overlay is a generated clip (see [text-stickers.md](text-stickers.md)) with `content.kind = "overlay"`, drawn by
the generator module `builtin.overlay` (capability `generate.overlay`; another module can replace it or add effects).

```json
{"kind": "overlay", "overlay": "snow", "w": 1920, "h": 1080, "ref": 1080, "density": 1, "speed": 1, "size": 1,
 "opacity": 1, "feather": 0.12, "color": "#ffffff", "seed": 1,
 "in": {"type": "fade", "duration": 0.8}, "out": {"type": "fade", "duration": 0.8}}
```

* **Where:** the region is a `w` x `h` box (canvas px) centred on the clip's position (`x`, `y`, `scale` of its
  transform, so keyframes work). It covers the whole frame when added. Its inner edges fade over `feather` (share of
  the smaller side); an edge that reaches the frame does not fade, so a full-frame overlay is full to the border.
* **When:** the clip's place and length on the timeline. It fades in and out as a whole (`in`/`out`; `""` = no fade).
* **How much:** `density` scales the particle count (which also follows the region's area), `speed` the motion,
  `size` the particles, `opacity` the whole layer; `color` tints effects marked `tintable` (snow, rain, sparkles...).
* **Same picture everywhere:** each frame is a pure function of time (seeded per particle, no state), so seeking,
  preview, export and every OS show the same thing.

`Editor::setOverlayRegion(clip, sourceTime, x, y, w, h)` sets the region from a box on the canvas (its size and
position, keyed when the position is animated) in one undo step.

## Speed

Particles are vector shapes culled to the frame. Large soft shapes (fog, light leaks, bokeh, vignettes) are drawn at a
quarter or an eighth of the resolution and blended up, since they have no detail to lose. At 1080p each effect costs
1-20 ms a frame on one CPU core (most under 5 ms); the preview draws at preview size. Overlays are drawn on the CPU
like text and stickers; moving generated layers to the GPU is part of the GPU effects work.

## Using it

- **Desktop:** the "Lớp phủ" panel. Pick a group, hover an effect to see it move, click to lay it over the frame at
  the playhead (5 s). On the preview, drag the box to move the region and pull a corner to resize it to any shape;
  "Phủ toàn khung" covers the whole frame again. Sliders: density, speed, particle size, strength, soft edge, fade;
  colour for tintable effects.
- **Mobile:** the "Lớp phủ" tool. Tabs: Lớp phủ (the library) and Chỉnh (the same settings, colour swatches, "Phủ toàn
  khung"). On the preview, drag the box to move, pull the corner handle to resize, double-tap to open the tool.
- The timeline shows overlays in teal with a ✦ label; drag or trim them like any clip.

## C API

`vs_overlays_json`, `vs_default_overlay_json`, `vs_overlay_icon`, `vs_set_overlay_region`, plus the generated-clip
calls `vs_add_generated_clip`, `vs_update_clip_content`, `vs_generated_rect`, `vs_content_preview`.
