#pragma once
// The Alchemical Codex, simulation side: an id for every transmutation the cellular automaton can perform.
// A ChunkUpdater notes each one that fires in its chunk (World::codexSeen reads them back after the step);
// the names, formulas and riddles live in game/Codex.hpp, so the simulation knows nothing but these ids.

#include <cstdint>

namespace pm {

// Values are bit positions in the profile's codex mask: only ever append.
enum class CodexEntry : uint8_t {
    // The pairwise reaction table (sim/Material.cpp).
    WaterLava,       ///< water (or brine) quenches lava: steam and obsidian
    LavaIce,         ///< lava melts ice into water (by its reaction or its heat)
    LavaSnow,        ///< lava melts snow into water (by its reaction or its heat)
    LavaSand,        ///< lava bakes sand into glass
    LavaBlood,       ///< lava boils blood away into smoke
    LavaSludge,      ///< lava boils toxic sludge into toxic gas
    LavaSlime,       ///< lava sears slime into smoke
    WaterSalt,       ///< salt dissolves into water as brine
    WaterSaltRock,   ///< water leaches salt rock into brine
    SaltIce,         ///< salt melts ice into water
    VitaeSludge,     ///< Vitae and toxic sludge cancel out into water
    VitaeAcid,       ///< Vitae and acid cancel out into water
    WaterSpore,      ///< watered spores sprout into glowcaps
    QuicksilverGold, ///< quicksilver takes in gold dust as Liquid Midas
    // Fire and burning.
    Kindling,        ///< flame, lava or heat sets flammable matter alight
    Dousing,         ///< wet matter puts out flames and boils into steam
    Ashes,           ///< burnt-out matter crumbles to ash
    Detonation,      ///< burning gunpowder detonates
    // Acid.
    Corrosion,       ///< acid dissolves stone (or anything else it can eat)
    GoldParting,     ///< acid dissolves gold ore around its gold dust
    // Heat.
    Thaw,            ///< flames melt ice or snow into water
    // Liquid Midas.
    Projection,      ///< Liquid Midas turns stone into gold ore
    // Gases.
    Condensation,    ///< steam condenses back into water
    // Hanging plants.
    VineFall,        ///< a vine cut loose crumbles into litter (a glowvine into spores)
    Count
};

constexpr int kCodexCount = static_cast<int>(CodexEntry::Count);
static_assert(kCodexCount <= 32, "codex entries are kept in a 32-bit mask");

constexpr uint32_t codexBit(CodexEntry e) { return 1u << static_cast<unsigned>(e); }
/// Every codex entry's bit (a mask from a newer build may hold more).
constexpr uint32_t kCodexAll = 0xFFFFFFFFu >> (32 - kCodexCount);

} // namespace pm
