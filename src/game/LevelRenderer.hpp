#pragma once
// Draws a Level through the SceneRenderer pipeline: world layer, parallax background,
// props, pickups, creatures, projectiles, particles and every light source.

#include "core/Math.hpp"
#include "render/Background.hpp"
#include "render/Sky.hpp"

namespace pm {

class Level;
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

private:
    void drawProps(const Level& level, float time, bool emissive);
    void drawPickup(const Level& level, const Pickup& pk, float time, bool emissive);
    void addLights(const Level& level, float alpha, float time);

    SceneRenderer& scene_;
    const SpriteBank& sprites_;
    ThreadPool& pool_;
    Background background_;
    Sky sky_;
    bool highQuality_ = true;
};

} // namespace pm
