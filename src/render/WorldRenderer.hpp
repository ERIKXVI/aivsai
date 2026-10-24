#pragma once
// Converts the visible part of the world into GPU textures every frame:
//   * color:    albedo of every cell (alpha = coverage, 0 for air)
//   * emission: light emitted by the cell (rgb) and how much it blocks light (alpha)
// Gameplay layers (particles, projectiles) can stamp extra pixels into the CPU buffers
// between build() and upload().

#include <cstdint>
#include <vector>

#include "raylib.h"
#include "sim/Material.hpp"

namespace pm {

class World;

class WorldRenderer {
public:
    WorldRenderer() = default;
    ~WorldRenderer();

    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    /// (Re)creates the textures if the requested size differs from the current one.
    void resize(int width, int height);

    /// Fills the CPU buffers with the world region whose top-left cell is (originX, originY).
    void build(const World& world, int originX, int originY, uint32_t tick);

    /// Writes a pixel into the buffers in world coordinates (ignored when off-screen).
    void stamp(int worldX, int worldY, Rgba color, Rgba emission);
    void stampEmission(int worldX, int worldY, Rgba emission);

    void upload();

    int width() const { return w_; }
    int height() const { return h_; }
    int originX() const { return ox_; }
    int originY() const { return oy_; }
    const Texture2D& colorTexture() const { return colorTex_; }
    const Texture2D& emissionTexture() const { return emitTex_; }

private:
    void release();

    int w_ = 0, h_ = 0;
    int ox_ = 0, oy_ = 0;
    std::vector<Rgba> color_;
    std::vector<Rgba> emission_;
    Texture2D colorTex_{};
    Texture2D emitTex_{};
};

/// Color of a cell as drawn on screen (animated for liquids, fire, burning cells).
Rgba cellColor(const Cell& c, int x, int y, uint32_t tick);
/// Light emitted by a cell; alpha carries its light opacity.
Rgba cellEmission(const Cell& c, int x, int y, uint32_t tick);

} // namespace pm
