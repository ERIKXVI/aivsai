#pragma once
// Perks: permanent upgrades for the rest of a run, chosen at sanctum altars.

#include <cstdint>

namespace pm {

enum class PerkId : uint8_t {
    Vitality,
    Fireproof,
    BlastShield,
    ToxicBlood,
    Insulated,
    Levitation,
    Swiftness,
    MidasGreed,
    Vampirism,
    GlassCannon,
    ArcaneFlow,
    SecondLife,
    Thorns,
    Gills,
    CriticalMind,
    Expansion,
    LuckyDay,
    PhoenixStep,
    VolatileFoes,
    Companion,
    PerpetualMotion,
    IronStomach,
    Count
};

constexpr int kPerkCount = static_cast<int>(PerkId::Count);

struct PerkDef {
    const char* name;
    const char* description;
    uint32_t color;
    bool stackable;
};

const PerkDef& perkDef(PerkId id);

} // namespace pm
