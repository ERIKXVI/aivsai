#pragma once
// Draws a Level through the SceneRenderer pipeline: world layer, parallax background,
// props, pickups, creatures, projectiles, particles and every light source.

#include "core/Math.hpp"
#include "render/Background.hpp"

namespace pm {

class Level;
class SceneRenderer;
class SpriteBank;

class LevelRenderer {
public:
    LevelRenderer(SceneRenderer& scene, const SpriteBank& sprites) : scene_(scene), sprites_(sprites) {}

    void render(const Level& level, Vec2 camera, float alpha, float time, int screenW, int screenH);

private:
    void drawProps(const Level& level, float time, bool emissive);
    void addLights(const Level& level, float alpha, float time);

    SceneRenderer& scene_;
    const SpriteBank& sprites_;
    Background background_;
};

} // namespace pm
