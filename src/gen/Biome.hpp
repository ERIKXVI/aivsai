#pragma once
// Biome definitions: the materials, palette, lighting and inhabitants of each stage of the
// descent. Purely data; the generator and game read from it.

#include <array>
#include <cstdint>

#include "sim/Material.hpp"

namespace pm {

enum class BiomeId : uint8_t { Nigredo, Albedo, Citrinitas, Rubedo, Athanor, Sanctum, Count };

struct WeightedMat {
    MatId mat = mat::Empty;
    float weight = 0.0f;
};

struct VeinDef {
    MatId mat = mat::Empty;
    float scale = 0.05f;     ///< noise frequency
    float threshold = 0.7f;  ///< higher = rarer
};

struct BiomeDef {
    const char* name;
    const char* subtitle;
    // Terrain
    MatId rock;               ///< bulk material
    MatId rockAlt;            ///< secondary bulk material (strata)
    MatId surface;            ///< thin skin on cave walls
    MatId floor;              ///< loose material resting on floors
    MatId hardRock;           ///< tougher material near the level edges
    std::array<VeinDef, 4> veins;
    std::array<WeightedMat, 4> pools;   ///< liquids that collect in basins
    std::array<WeightedMat, 3> powders; ///< loose piles
    float openness;           ///< 0..1: how much of the level is open cave
    float vegetation;         ///< moss / fungus density
    // Look
    uint32_t backgroundA, backgroundB; ///< back-wall gradient colours
    float ambient[3];
    float tint[3];
    float saturation;
    uint32_t fogColor;
    // Music
    int musicRoot;            ///< MIDI note of the tonic
    int musicMode;            ///< scale index (see audio/Music)
};

const BiomeDef& biomeDef(BiomeId id);

/// The biome visited at a given level index of a run (0-based).
BiomeId biomeForDepth(int levelIndex);
/// Roman numeral of a stage ("I".."V") for titles and the HUD.
const char* stageNumeral(int levelIndex);

} // namespace pm
