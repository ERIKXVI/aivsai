#pragma once
// Material definitions: the physical and visual properties of every substance in the world,
// plus the pairwise reaction table. Data lives in Material.cpp; everything here is read-only
// after `initMaterials()` runs at startup.

#include <array>
#include <cstdint>
#include <string>

#include "sim/Cell.hpp"
#include "sim/Codex.hpp"

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

/// 0xRRGGBB to Rgba (the simulation-side twin of render/ColorUtil.hpp's hexColor).
constexpr Rgba hexRgba(uint32_t hex, uint8_t a = 255) {
    return {static_cast<uint8_t>((hex >> 16) & 0xFF), static_cast<uint8_t>((hex >> 8) & 0xFF), static_cast<uint8_t>(hex & 0xFF), a};
}

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
    CodexEntry codex = CodexEntry::WaterLava; ///< the transmutation it is in the Alchemical Codex
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

namespace detail {
/// The one table instance: written only by initMaterials(), read through materials().
extern MaterialTables g_materialTables;
} // namespace detail

/// Read-only view of the material tables. Inline so the per-cell helpers below compile to a
/// plain indexed load rather than a call into another translation unit (the hot sim path).
inline const MaterialTables& materials() { return detail::g_materialTables; }

inline const MaterialDef& matDef(MatId id) { return materials().defs[id]; }
inline Phase phaseOf(MatId id) { return materials().defs[id].phase; }
inline bool hasFlag(MatId id, uint32_t flag) { return (materials().defs[id].flags & flag) != 0; }

/// Solid for collision purposes: creatures and projectiles cannot pass through.
inline bool isSolidForCollision(MatId id) {
    const MaterialDef& d = materials().defs[id];
    return (d.phase == Phase::Solid || d.phase == Phase::Powder) && !(d.flags & matflag::kNoCollide);
}

// Only mat::Empty (air) has Phase::Empty, so testing the id first skips the table lookup.
/// Air or gas: new matter may be written here.
inline bool isVacant(MatId id) { return id == mat::Empty || phaseOf(id) == Phase::Gas; }

/// Air, gas or fire: falling matter and particles pass straight through.
inline bool isPassable(MatId id) {
    if (id == mat::Empty) return true;
    const Phase p = phaseOf(id);
    return p == Phase::Gas || p == Phase::Fire;
}

/// Representative (mid-palette) colour of a material, for UI and effects.
inline Rgba materialColor(MatId id) { return materials().palette[id][128]; }

/// Builds a fresh cell of the given material with randomized variation and lifetime.
Cell makeCell(MatId id, uint32_t randomBits);

/// Ticks a freshly ignited cell of material `d` burns: its burnTime shortened by a random
/// 0..burnTime/4, never below one.
inline uint8_t burnLife(const MaterialDef& d, uint32_t randomBits) {
    const int life = d.burnTime - static_cast<int>(randomBits % (d.burnTime / 4u + 1u));
    return static_cast<uint8_t>(life < 1 ? 1 : life);
}

/// One line on what a material does, for the in-game matter lens: hand-written for the
/// alchemically interesting ones, otherwise read off its flags. Empty for everyday matter
/// (air, rock, soil, sand, plants...) so the lens only speaks up when there is news.
std::string matDescription(MatId id);

} // namespace pm
