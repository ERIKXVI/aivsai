#pragma once
// Material definitions: the physical and visual properties of every substance in the world,
// plus the pairwise reaction table. Data lives in Material.cpp; everything here is read-only
// after `initMaterials()` runs at startup.

#include <array>
#include <cstdint>

#include "sim/Cell.hpp"

namespace pm {

enum class Phase : uint8_t { Empty, Solid, Powder, Liquid, Gas, Fire };

namespace mat {
// clang-format off
enum : MatId {
    Empty = 0,
    // Static solids
    Bedrock, Rock, Soil, Wood, Brick, Metal, Ice, Obsidian, Basalt, Coal, GoldOre, Crystal,
    Glass, Moss, Fungus, SaltRock, Bone, Flesh, Sulphite, Rune, Vine, GlowVine, Leaves,
    // Powders
    Sand, Gravel, Ash, Snow, Gunpowder, GoldDust, Salt, Sulphur, Spore, Litter,
    // Liquids
    Water, Oil, Lava, Acid, Blood, Slime, Vitae, Toxic, Midas, Quicksilver, Brine, Ichor,
    // Gases
    Smoke, Steam, ToxicGas, Methane, Miasma,
    // Energy
    Fire, Plasma,
    Count
};
// clang-format on
} // namespace mat

constexpr int kMaterialCount = mat::Count;
constexpr MatId kKeep = 0xFF; ///< reaction result meaning "unchanged"

namespace matflag {
constexpr uint32_t kConductive = 1u << 0;   ///< carries electricity
constexpr uint32_t kAcidProof = 1u << 1;    ///< immune to corrosion
constexpr uint32_t kCorrosive = 1u << 2;    ///< dissolves neighbours (acid)
constexpr uint32_t kIgniter = 1u << 3;      ///< sets flammable neighbours alight (lava)
constexpr uint32_t kExtinguisher = 1u << 4; ///< puts out burning neighbours (water)
constexpr uint32_t kExplosive = 1u << 5;    ///< detonates when it finishes burning
constexpr uint32_t kCollectible = 1u << 6;  ///< picked up by the player on touch (gold)
constexpr uint32_t kTransmuter = 1u << 7;   ///< turns neighbouring solids into gold ore
constexpr uint32_t kMeltable = 1u << 8;     ///< melts near heat (ice, snow)
constexpr uint32_t kGrows = 1u << 9;        ///< slowly spreads over damp surfaces (moss, fungus)
constexpr uint32_t kHealing = 1u << 10;     ///< heals creatures standing in it
constexpr uint32_t kOrganic = 1u << 11;     ///< dissolves extra fast in acid, burns
constexpr uint32_t kSticky = 1u << 12;      ///< slows creatures moving through it
constexpr uint32_t kHot = 1u << 13;         ///< heat source (melts ice, boils water)
constexpr uint32_t kDiggable = 1u << 14;    ///< carved by the digging spells quickly
constexpr uint32_t kNoCollide = 1u << 15;   ///< static but creatures and projectiles pass through (vines)
constexpr uint32_t kHangs = 1u << 16;       ///< hangs from static matter above; cut loose, it crumbles
} // namespace matflag

struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 0;
};

struct MaterialDef {
    const char* name = "?";
    Phase phase = Phase::Empty;
    uint16_t density = 0;       ///< relative weight; air is 10, gases < 10
    uint8_t dispersion = 0;     ///< liquid sideways flow (cells per tick)
    uint8_t hardness = 0;       ///< resistance to explosions and digging; 255 = indestructible
    uint8_t flammability = 0;   ///< ignition chance per tick next to fire (n/256)
    uint8_t burnTime = 0;       ///< ticks a burning cell survives
    MatId burnsInto = mat::Empty;
    uint8_t lifeMin = 0;        ///< gases and fire: random lifetime range in ticks
    uint8_t lifeMax = 0;
    MatId decaysInto = mat::Empty; ///< what a gas becomes at the end of its life
    uint8_t decayChance = 0;       ///< chance (n/256) of decaying into `decaysInto` vs vanishing
    float contactDamage = 0.0f;    ///< damage per second to creatures inside it
    uint32_t flags = 0;
    Rgba colorA, colorB;           ///< palette gradient endpoints
    Rgba glow;                     ///< emitted light (alpha = intensity)
    uint8_t opacity = 255;         ///< how strongly it blocks light (0..255)
};

struct Reaction {
    MatId other = mat::Empty;
    MatId selfInto = kKeep;
    MatId otherInto = kKeep;
    uint8_t chance = 0; ///< n/256 per tick when touching
};

constexpr int kMaxReactions = 10;

struct MaterialTables {
    std::array<MaterialDef, kMaterialCount> defs{};
    std::array<std::array<Rgba, 256>, kMaterialCount> palette{};
    std::array<std::array<Reaction, kMaxReactions>, kMaterialCount> reactions{};
    std::array<uint8_t, kMaterialCount> reactionCount{};
};

/// Builds the material tables and palettes. Must be called once before any simulation.
void initMaterials();

const MaterialTables& materials();

inline const MaterialDef& matDef(MatId id) { return materials().defs[id]; }
inline Phase phaseOf(MatId id) { return materials().defs[id].phase; }
inline bool hasFlag(MatId id, uint32_t flag) { return (materials().defs[id].flags & flag) != 0; }

/// Solid for collision purposes: creatures and projectiles cannot pass through.
inline bool isSolidForCollision(MatId id) {
    const MaterialDef& d = materials().defs[id];
    return (d.phase == Phase::Solid || d.phase == Phase::Powder) && !(d.flags & matflag::kNoCollide);
}

inline bool isFluid(MatId id) {
    Phase p = phaseOf(id);
    return p == Phase::Liquid;
}

/// Representative (mid-palette) colour of a material, for UI and effects.
inline Rgba materialColor(MatId id) { return materials().palette[id][128]; }

/// Builds a fresh cell of the given material with randomized variation and lifetime.
Cell makeCell(MatId id, uint32_t randomBits);

} // namespace pm
