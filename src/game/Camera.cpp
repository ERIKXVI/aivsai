#include "game/Camera.hpp"

#include <cmath>

#include "core/Noise.hpp"

namespace pm {

void FollowCamera::update(Vec2 target, Vec2 aim, float dt, Vec2 worldSize, Vec2 viewSize) {
    // Look a little towards the cursor so the player sees where they are aiming.
    Vec2 look = (aim - target) * 0.22f;
    float len = look.length();
    if (len > 55.0f) look = look * (55.0f / len);
    Vec2 desired = target + look;
    pos_ = damp(pos_, desired, 9.0f, dt);

    // Keep the view inside the world.
    Vec2 half = viewSize * 0.5f;
    if (worldSize.x > viewSize.x) pos_.x = clamp(pos_.x, half.x, worldSize.x - half.x);
    else pos_.x = worldSize.x * 0.5f;
    if (worldSize.y > viewSize.y) pos_.y = clamp(pos_.y, half.y, worldSize.y - half.y);
    else pos_.y = worldSize.y * 0.5f;
}

Vec2 FollowCamera::position(float shake, float time, bool shakeEnabled) const {
    if (!shakeEnabled || shake <= 0.0f) return pos_;
    float amount = shake * shake * 7.0f;
    float sx = noise::perlin(time * 22.0f, 1.3f, 77u);
    float sy = noise::perlin(time * 22.0f, 9.7f, 91u);
    return pos_ + Vec2{sx, sy} * amount;
}

} // namespace pm
