#include "gen/Biome.hpp"

namespace pm {

namespace {

using namespace mat;

// clang-format off
const BiomeDef kBiomes[static_cast<int>(BiomeId::Count)] = {
    // I. Nigredo — the blackening: charcoal caverns, moss, black water and oil.
    {
        "Nigredo", "The Blackened Caverns",
        Rock, Coal, Soil, Soil, Basalt,
        {{{Coal, 0.05f, 0.70f}, {GoldOre, 0.07f, 0.80f}, {Wood, 0.03f, 0.84f}, {Gravel, 0.05f, 0.80f}}},
        {{{Water, 0.6f}, {Oil, 0.3f}, {Slime, 0.1f}, {Empty, 0.0f}}},
        {{{Sand, 0.5f}, {Gravel, 0.3f}, {Ash, 0.2f}}},
        0.50f, 0.8f,
        0x463d38, 0x231d18,
        {0.38f, 0.36f, 0.42f}, {1.0f, 0.97f, 0.92f}, 0.95f, 0x0c0a0a,
        45, 0,
    },
    // II. Albedo — the whitening: salt glaciers, ice and brine.
    {
        "Albedo", "The Salt Glaciers",
        SaltRock, SaltRock, Ice, Snow, Rock,
        {{{Ice, 0.04f, 0.60f}, {Crystal, 0.06f, 0.82f}, {GoldOre, 0.07f, 0.82f}, {Salt, 0.05f, 0.78f}}},
        {{{Water, 0.5f}, {Brine, 0.4f}, {Quicksilver, 0.1f}, {Empty, 0.0f}}},
        {{{Snow, 0.6f}, {Salt, 0.3f}, {Sand, 0.1f}}},
        0.55f, 0.15f,
        0x46576b, 0x1d2835,
        {0.30f, 0.34f, 0.42f}, {0.92f, 0.98f, 1.06f}, 0.85f, 0x0a0e14,
        50, 1,
    },
    // III. Citrinitas — the yellowing: sulphur mines, gunpowder stores, glowing fungus.
    {
        "Citrinitas", "The Sulphur Mines",
        Rock, Sulphite, Soil, Sand, Basalt,
        {{{Sulphite, 0.05f, 0.60f}, {GoldOre, 0.06f, 0.74f}, {Gunpowder, 0.05f, 0.84f}, {Fungus, 0.06f, 0.80f}}},
        {{{Toxic, 0.4f}, {Acid, 0.3f}, {Water, 0.3f}, {Empty, 0.0f}}},
        {{{Sulphur, 0.4f}, {Sand, 0.4f}, {Gunpowder, 0.2f}}},
        0.52f, 0.6f,
        0x504628, 0x241f10,
        {0.40f, 0.36f, 0.27f}, {1.06f, 1.0f, 0.86f}, 1.0f, 0x100c04,
        47, 2,
    },
    // IV. Rubedo — the reddening: the crimson forge, lava rivers and basalt.
    {
        "Rubedo", "The Crimson Forge",
        Basalt, Obsidian, Rock, Ash, Obsidian,
        {{{Obsidian, 0.04f, 0.62f}, {GoldOre, 0.06f, 0.74f}, {Coal, 0.05f, 0.76f}, {Crystal, 0.06f, 0.86f}}},
        {{{Lava, 0.8f}, {Oil, 0.1f}, {Acid, 0.1f}, {Empty, 0.0f}}},
        {{{Ash, 0.5f}, {Gravel, 0.3f}, {Sand, 0.2f}}},
        0.55f, 0.1f,
        0x602e24, 0x26100c,
        {0.42f, 0.28f, 0.25f}, {1.08f, 0.94f, 0.88f}, 1.05f, 0x140604,
        43, 3,
    },
    // V. The Athanor — the alchemist's furnace where the Great Work is completed.
    {
        "The Athanor", "Where the Great Work Ends",
        Brick, Obsidian, Obsidian, Ash, Rune,
        {{{Obsidian, 0.04f, 0.70f}, {Crystal, 0.05f, 0.85f}, {GoldOre, 0.06f, 0.80f}, {Metal, 0.05f, 0.86f}}},
        {{{Lava, 0.5f}, {Midas, 0.2f}, {Quicksilver, 0.3f}, {Empty, 0.0f}}},
        {{{Ash, 0.5f}, {GoldDust, 0.2f}, {Gravel, 0.3f}}},
        0.6f, 0.0f,
        0x493260, 0x1c1028,
        {0.38f, 0.32f, 0.46f}, {1.0f, 0.94f, 1.08f}, 1.05f, 0x0c0612,
        41, 4,
    },
    // Sanctum — the resting place between stages.
    {
        "Sanctum", "A Moment of Stillness",
        Brick, Rune, Rune, Sand, Rune,
        {{{Crystal, 0.06f, 0.8f}, {Empty, 0.0f, 2.0f}, {Empty, 0.0f, 2.0f}, {Empty, 0.0f, 2.0f}}},
        {{{Vitae, 1.0f}, {Empty, 0.0f}, {Empty, 0.0f}, {Empty, 0.0f}}},
        {{{Sand, 1.0f}, {Empty, 0.0f}, {Empty, 0.0f}}},
        0.0f, 0.3f,
        0x46385d, 0x211a35,
        {0.65f, 0.59f, 0.72f}, {1.0f, 0.98f, 1.04f}, 1.0f, 0x0a0810,
        48, 5,
    },
};
// clang-format on

} // namespace

const BiomeDef& biomeDef(BiomeId id) { return kBiomes[static_cast<int>(id)]; }

BiomeId biomeForDepth(int levelIndex) {
    static constexpr BiomeId kOrder[] = {BiomeId::Nigredo, BiomeId::Albedo, BiomeId::Citrinitas, BiomeId::Rubedo,
                                         BiomeId::Athanor};
    if (levelIndex < 0) levelIndex = 0;
    if (levelIndex > 4) levelIndex = 4;
    return kOrder[levelIndex];
}

} // namespace pm
