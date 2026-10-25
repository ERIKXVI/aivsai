#pragma once
// Feats: lifetime achievements, from a first kill to completing the Great Work on Archmage.
// Each is earned once per profile and announced with a toast; the Grimoire lists them.

#include <cstdint>

namespace pm {

// Values are bit positions in the profile's feat mask: only ever append.
enum class FeatId : uint8_t {
    FirstBlood,
    Spellwright,
    Brimming,
    Vitae,
    Doused,
    TreasureHunter,
    Lanternfall,
    Transmutation,
    VaultRaider,
    EliteHunter,
    Carnage,
    BlackToad,
    WhiteWraith,
    MineKing,
    RedColossus,
    DeepDelver,
    Untouched,
    Midas,
    MagnumOpus,
    Archmage,
    DailyRite,
    Pathfinder,
    Polymath,
    Mimic,
    Count
};

constexpr int kFeatCount = static_cast<int>(FeatId::Count);
static_assert(kFeatCount <= 32, "feats are kept in a 32-bit mask");

struct FeatDef {
    const char* name;
    const char* description;
};

const FeatDef& featDef(FeatId id);
constexpr uint32_t featBit(FeatId id) { return 1u << static_cast<unsigned>(id); }

} // namespace pm
