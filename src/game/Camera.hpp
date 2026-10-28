#pragma once
// Smooth follow camera with aim look-ahead and trauma-based screen shake.

#include "core/Math.hpp"

namespace pm {

class FollowCamera {
public:
    void reset(Vec2 pos) {
        pos_ = pos;
        prev_ = pos;
    }
    /// Advances the camera. `viewSize` is the visible area in cells. `fallSpeed` (cells/s, down
    /// positive) leads the view down a long drop, so the alchemist sees what they fall into.
    void update(Vec2 target, Vec2 aim, float fallSpeed, float dt, Vec2 worldSize, Vec2 viewSize);
    /// Camera centre including shake (`shake` 0..1) and cast recoil.
    Vec2 position(float shake, float time, bool shakeEnabled) const;
    /// Camera centre without shake or recoil (which move the view by a few cells at most).
    Vec2 center() const { return pos_; }
    /// Nudges the view (cast recoil, in cells); it springs back within about a tenth of a second.
    void kick(Vec2 offset);

private:
    Vec2 pos_;
    Vec2 prev_;
    Vec2 kick_;
};

} // namespace pm
