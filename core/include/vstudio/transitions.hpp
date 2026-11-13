// Transitions between clips: cross-dissolve, dip to black, slides, wipes, iris,
// zoom, pixelate... (~30). Nothing pops: every transition is eased (smooth start
// and end), soft-edged, and the audio fades with it.
//
// Storage (model.hpp): Clip::transitionIn / transitionOut = {"type": id, "duration": ticks}.
//  - A cut between two clips that touch on a track uses the RIGHT clip's transitionIn.
//    It is centred on the cut: the left clip runs on past its out point (its source
//    handle, or its last frame held) and the right clip starts before its in point.
//    Clip positions never change, so adding a transition never moves anything.
//  - A clip with nothing right before it: transitionIn fades it in from nothing (the
//    layers below, or black), inside the clip: [in, in + d].
//  - A clip with nothing right after it: transitionOut fades it out, inside: [out - d, out].
// Durations are clamped at render time so the windows at both ends of a clip never meet.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"
#include "vstudio/time.hpp"

namespace vstudio {

struct TransitionInfo {
    std::string id;
    std::string name;      // Vietnamese UI name
    std::string category;  // "Cơ bản", "Chuyển động", "Lau", "Hình dạng", "Hiệu ứng"
};
const std::vector<TransitionInfo>& transitionCatalog();
const TransitionInfo* findTransition(const std::string& id);

inline Time defaultTransitionDuration() { return Time::fromMilliseconds(500); }
inline Time minTransitionDuration() { return Time::fromMilliseconds(100); }
inline Time maxTransitionDuration() { return Time::fromSeconds(5.0); }

enum class TransitionEdge { In, Out };

// A transition playing on a track at some instant.
struct ActiveTransition {
    const Clip* from = nullptr;  // null: fading in from nothing
    const Clip* to = nullptr;    // null: fading out to nothing
    std::string type;
    double progress = 0;  // 0..1, linear in time (each transition eases it itself)
    Time start, end;      // the window on the timeline
};

// The transition playing on `track` at timeline time `t`, if any.
std::optional<ActiveTransition> transitionAt(const Track& track, Time t);

// Every place on the track where a transition can go: each cut between touching clips,
// plus the free start/end of clips. `transition` is what is set there (null if none).
struct CutPoint {
    Time at;
    const Clip* from = nullptr;  // clip ending here (null: free start of `to`)
    const Clip* to = nullptr;    // clip starting here (null: free end of `from`)
    Json transition;
    Time duration;               // effective (clamped) duration when set
    // The clip and edge that store this place's transition.
    [[nodiscard]] const Clip* owner() const { return to ? to : from; }
    [[nodiscard]] TransitionEdge edge() const { return to ? TransitionEdge::In : TransitionEdge::Out; }
};
std::vector<CutPoint> cutPoints(const Track& track);

// Audio gain of `clip` at timeline `t` from its transitions (1 outside them): eased fade
// out before a cut, in after it, so sound never starts or stops with a click.
double transitionGain(const Track& track, const Clip& clip, Time t);

// Blends `a` (outgoing) and `b` (incoming) into `out` at `progress` (0..1, linear).
// a, b and out have the same size and format (RGBA8 or RGBA16F, straight alpha; a
// transparent pixel means "nothing there"). `t` seeds the animated ones (glitch).
void applyTransition(const std::string& type, const VideoFrame& a, const VideoFrame& b, double progress,
                     VideoFrame& out, Time t = {});

}  // namespace vstudio
