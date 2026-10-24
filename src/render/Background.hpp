#pragma once
// Parallax back-wall textures, generated per biome from seamless noise.

#include "core/Math.hpp"
#include "gen/Biome.hpp"
#include "raylib.h"

namespace pm {

class Background {
public:
    static constexpr int kSize = 256;
    static constexpr float kParallax = 0.55f;

    ~Background();
    /// Regenerates the texture if the biome changed.
    void setBiome(BiomeId biome, uint32_t seed);
    /// Draws the tiled wall into the current render target for a layer whose top-left
    /// world cell is (originX, originY).
    void draw(int originX, int originY, int layerW, int layerH) const;

private:
    Texture2D tex_{};
    BiomeId biome_ = BiomeId::Count;
};

} // namespace pm
