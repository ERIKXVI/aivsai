#pragma once
// Draws a Level through the SceneRenderer pipeline: world layer, parallax background,
// props, pickups, creatures, projectiles, particles and every light source.

#include "core/Math.hpp"
#include "render/Background.hpp"
#include "render/Sky.hpp"

namespace pm {

class Level;
struct LightingParams;
struct Pickup;
class SceneRenderer;
class SpriteBank;
class ThreadPool;

class LevelRenderer {
public:
    LevelRenderer(SceneRenderer& scene, const SpriteBank& sprites, ThreadPool& pool)
        : scene_(scene), sprites_(sprites), pool_(pool) {}

    void render(const Level& level, Vec2 camera, float alpha, float time, int screenW, int screenH);
    void setHighQuality(bool high) { highQuality_ = high; }
    /// 0..1: the world drains of colour and light as the alchemist dies.
    void setDeathFade(float k) { deathFade_ = k; }

private:
    void drawProps(const Level& level, float alpha, float time, bool emissive);
    void drawPickup(const Pickup& pk, float alpha, float time, bool emissive);
    void addLights(const Level& level, float alpha, float time);
    /// Biome light and tint, the death fade, open-air haze, moonlight near the surface and the hurt pulse.
    LightingParams lightingParams(const Level& level, Vec2 camera, float time, int originY) const;

    SceneRenderer& scene_;
    const SpriteBank& sprites_;
    ThreadPool& pool_;
    Background background_;
    Sky sky_;
    bool highQuality_ = true;
    float deathFade_ = 0.0f;
};

} // namespace pm
