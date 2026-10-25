#pragma once
// State that persists across the levels of one run: the alchemist's health, gold, wands,
// spell inventory, flask, perks and statistics.

#include <array>
#include <cstdint>
#include <vector>

#include "game/Loadouts.hpp"
#include "game/Ordeals.hpp"
#include "game/Perks.hpp"
#include "game/Spells.hpp"
#include "game/Wand.hpp"

namespace pm {

enum class Difficulty : uint8_t { Apprentice, Adept, Archmage };

/// Room for every EnemyType in kill counts (saves and profiles), with space to grow.
constexpr size_t kMaxCreatureTypes = 24;

struct Flask {
    static constexpr int kCapacity = 1000;
    MatId mat = mat::Empty;
    int amount = 0;
    bool empty() const { return amount <= 0 || mat == mat::Empty; }
};

struct RunStats {
    float time = 0.0f;
    int kills = 0;
    int goldCollected = 0;
    int spellsCast = 0;
    int deepest = 0;
    int chestsOpened = 0;
    float damageDealt = 0.0f;
    float damageTaken = 0.0f;
};

struct RunState {
    static constexpr int kMaxWands = 4;
    static constexpr int kInventorySize = 16;

    uint32_t seed = 0;
    bool daily = false;
    Difficulty difficulty = Difficulty::Adept;
    LoadoutId loadout = LoadoutId::Apprentice;
    int levelIndex = 0;      ///< 0..4 = biomes, then victory
    int ordeal = 0;          ///< ordeal level the run is under (0 = none)
    bool inSanctum = false;

    float maxHp = 120.0f;
    float hp = 120.0f;
    int gold = 0;
    float levitationMax = 2.0f;
    std::vector<Wand> wands;
    int activeWand = 0;
    std::array<SpellId, kInventorySize> inventory{};
    Flask flask;
    std::array<uint8_t, kPerkCount> perks{};
    RunStats stats;
    uint64_t spellsSeen = 0;            ///< discoveries this run, merged into the profile
    std::array<int, kMaxCreatureTypes> killsByType{}; ///< indexed by EnemyType

    void noteSpell(SpellId s) { spellsSeen |= 1ull << static_cast<unsigned>(s); }
    void noteWand(const Wand& w) {
        for (SpellId s : w.slots)
            if (s != SpellId::None) noteSpell(s);
    }

    /// True when the run is bound by `o` (an ordeal level includes every one beneath it).
    bool under(Ordeal o) const { return ordeal >= static_cast<int>(o); }
    bool hasPerk(PerkId p) const { return perks[static_cast<size_t>(p)] > 0; }
    int perkCount(PerkId p) const { return perks[static_cast<size_t>(p)]; }
    /// Puts a spell into the first free inventory slot; false if full.
    bool addSpell(SpellId s);
    /// Slots a newly found attack spell straight into the active wand's first empty slot, so
    /// a pickup changes how the wand casts without a trip to the editor. Spells that need
    /// placing (modifiers, multicasts) or are risky to fire unawares (explosives, teleports,
    /// matter orbs) are left for the satchel. False if the spell was not slotted.
    bool trySlotIntoActiveWand(SpellId s);
    void applyPerk(PerkId p);

    float damageDealtMul() const;
    float damageTakenMul() const;
    float moveSpeedMul() const { return 1.0f + 0.25f * static_cast<float>(perkCount(PerkId::Swiftness)); }
    float manaRegenMul() const { return 1.0f + 0.6f * static_cast<float>(perkCount(PerkId::ArcaneFlow)); }
    float castSpeedMul() const;
};

RunState newRun(uint32_t seed, Difficulty difficulty, bool daily, LoadoutId loadout = LoadoutId::Apprentice, int ordeal = 0);

} // namespace pm
