// In/out/loop animations shared by text (per letter or whole) and stickers (whole).
// An "in" animation goes from hidden (progress 0) to at rest (progress 1) with no jump at
// either end; the matching "out" animation is the same motion played backwards. Loops sway
// around the rest pose and fade their amplitude in and out, so nothing pops.
#pragma once

#include <string>
#include <vector>

#include "vstudio/model.hpp"
#include "vstudio/vector.hpp"

namespace vstudio::motion {

// Pose of one element relative to its rest pose. Lengths are in element units (the font
// size for text, the sticker size for stickers); rotation in radians.
struct Pose {
    double dx = 0, dy = 0;
    double sx = 1, sy = 1;
    double rotation = 0;
    double opacity = 1;
    double blur = 0;    // blur radius in element units
    double reveal = 1;  // wipes: share of the element shown (0..1), see `wipe`
    int wipe = 0;       // 0 none, 1 left to right, 2 bottom to top
    // Combines two poses (offsets add, scales multiply...).
    [[nodiscard]] Pose operator*(const Pose& o) const;
    // dx/dy/scale/rotation as a transform around `centre` (element units scaled by `unit`).
    [[nodiscard]] vec::Affine transform(vec::Pt centre, double unit) const;
};

struct Info {
    std::string id;
    std::string name;      // Vietnamese, shown in the UI
    bool perLetter = false;  // text only: each letter moves on its own
};

const std::vector<Info>& animations();  // in (and, mirrored, out)
const std::vector<Info>& loops();
[[nodiscard]] const Info* findAnimation(const std::string& id);
[[nodiscard]] const Info* findLoop(const std::string& id);

// Pose of element `i` of `n` while animation `id` is at `progress` (0 hidden .. 1 at rest).
// `seed` varies scattered animations between clips.
Pose inPose(const std::string& id, double progress, int i, int n, unsigned seed = 0);
// Loop pose at `seconds`, amplitude scaled by `amount` (0..1).
Pose loopPose(const std::string& id, double seconds, double speed, int i, int n, double amount);

// Timing of a generated clip's animations from content {"in": {"type", "duration"}, "out": ..., "loop": {"type", "speed"}}.
struct Timing {
    std::string in, out, loop;
    double inProgress = 1;   // 1 = done
    double outProgress = 0;  // 0 = not started
    double loopAmount = 0;   // loop envelope 0..1
    double loopSpeed = 1;
    double seconds = 0;
    [[nodiscard]] bool still() const { return inProgress >= 1 && outProgress <= 0 && loopAmount <= 0; }
};
Timing timing(const Json& content, double local, double length);
// Pose of element i of n at that timing (in, out and loop combined).
Pose pose(const Timing& t, int i, int n, unsigned seed = 0);

// Default lengths in seconds.
inline constexpr double kDefaultIn = 0.6, kDefaultOut = 0.5;

}  // namespace vstudio::motion
