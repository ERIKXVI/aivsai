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
struct Wand;

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
    /// The pedestal a shop's ware stands on, in the albedo layer only.
    void drawPedestal(Vec2 pos, bool emissive) const;
    /// A gold nugget, whose glow glints now and then.
    void drawGold(Vec2 pos, float time, bool emissive) const;
    /// A spell card in its spell's colour, with a twinkling gold star when it is a rare find.
    void drawSpellCard(const Pickup& pk, Vec2 pos, float bob, float time, bool emissive) const;
    /// A loose wand drawn as a rod of pixels; only its gem glows.
    void drawWand(const Wand& wand, Vec2 pos, float bob, float time, bool emissive) const;
    /// A potion tinted by its contents; the flask glows faintly when they do.
    void drawPotion(const Pickup& pk, Vec2 pos, bool emissive) const;
    /// A healing heart, drawn pixel by pixel.
    void drawHeart(Vec2 pos, float bob, bool emissive) const;
    /// A chest, shut or opened.
    void drawChest(const Pickup& pk, Vec2 pos, bool emissive) const;
    /// A perk altar with an orb of the perk's colour floating above it.
    void drawPerkAltar(const Pickup& pk, Vec2 pos, float bob, bool emissive) const;
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
