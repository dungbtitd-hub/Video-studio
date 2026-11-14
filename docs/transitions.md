# Transitions between clips

A cut can dissolve, dip through black, slide, wipe, open as a circle… Nothing pops in or out: every transition
is eased, wipes have soft edges, and the sound fades with the picture. Preview and export use the same code on
all four OSes.

## Model

Transitions are stored on the clip: `Clip::transitionIn` / `transitionOut = {"type": "<id>", "duration": <ticks>}`.

| Where | Stored on | Plays |
|-------|-----------|-------|
| Cut between two clips that touch | the right clip's `transitionIn` | centred on the cut: `[cut − d/2, cut + d/2]` |
| Free start of a clip (nothing just before it) | its `transitionIn` | fades in from what is below (or black): `[in, in + d]` |
| Free end of a clip (nothing just after it) | its `transitionOut` | fades out: `[out − d, out]` |

* **Clips never move.** During a cut the outgoing clip plays on past its out point, using its source handle, or
  its last frame held when there is no more footage. The incoming clip starts before its in point the same way.
  Adding, changing or removing a transition is one undo step and changes nothing else on the timeline.
* The end of a clip that touches the next one *is* that cut. Setting a transition there stores it on the next
  clip's start.
* Durations are 0.1–5 s (default 0.5 s). When played, they are clamped so that the windows at the two ends of a
  clip never meet: at most each clip's length for a cut, and half the clip for a free edge.
* Splitting a clip keeps its start on the left half and its end on the right half. The new cut starts without a
  transition.

## The 33 transitions

| Group | Transitions |
|-------|-------------|
| Cơ bản | fade, dip_black, dip_white, flash, blur, luma |
| Chuyển động | slide_left/right/up/down, push_left/right/up/down, zoom_in, zoom_out, spin |
| Lau | wipe_left/right/up/down, wipe_diagonal, split_h, split_v, blinds, clock |
| Hình dạng | circle_open, circle_close, diamond, checker |
| Hiệu ứng | pixelate, glitch, ripple |

## Why they look smooth

* **Easing.** Fades use smoothstep and motion uses cubic ease-in-out, so speed is zero at both ends.
* **Soft edges.** Wipe and shape masks have a soft edge, and they sweep from fully off to fully on, so the first
  and last frames are exactly the two clips.
* **No black corners.** Zooms and spins stay zoomed in enough to cover the frame. Distortions such as glitch and
  ripple repeat the border pixels instead of showing black.
* **Sound.** The outgoing clip's sound fades out over the first half of a cut and the incoming one fades in over
  the second half. Free edges fade over the whole window. Gain is ramped every 32 samples, so there are no
  clicks.
* **Tested.** A core test runs every transition in 50 steps. It checks that the first and last frames match the
  two clips and that no single step changes the picture abruptly.

## Rendering

The renderer finds the transition playing on each video track at time `t`. It draws each side, with its looks,
covers and keyframes, onto its own transparent canvas, blends the two, and composites the result as one layer
(GPU or CPU, as usual). The incoming side reads through its own decoder, so the two sides never make each other
seek. Only frames inside a transition window pay this cost.

## Using it

- **Desktop:** the "Chuyển cảnh" panel.
  - Hover a thumbnail to see it play.
  - Drag a transition onto a cut on the timeline, or select a cut (its badge) or a clip (its start) and click a
    transition.
  - Set the length with the slider; it is committed on release.
  - "Áp dụng cho tất cả" puts the transition on every cut, and "Bỏ chuyển cảnh" removes one.
- **Mobile:** the timeline shows a badge under every cut, and at the free edges of the selected clip. Tap a badge,
  or use the "Chuyển cảnh" tool.
  - The sheet has the thumbnails, a small preview that plays the chosen transition, the length, "Áp dụng cho tất
    cả" and "Bỏ".

## C API

`vs_transitions_json`, `vs_set_transition(clip, edge 0|1, type, duration)`, `vs_set_transition_everywhere`,
`vs_cut_points_json`, `vs_transition_preview`.
