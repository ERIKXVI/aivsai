#pragma once
// State that persists across the levels of one run: the alchemist's health, gold, wands,
// spell inventory, flask, perks and statistics.

#include <array>
#include <cstdint>
#include <vector>

#include "game/Perks.hpp"
#include "game/Spells.hpp"
#include "game/Wand.hpp"

namespace pm {

enum class Difficulty : uint8_t { Apprentice, Adept, Archmage };

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
    int levelIndex = 0;      ///< 0..4 = biomes, then victory
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

    bool hasPerk(PerkId p) const { return perks[static_cast<size_t>(p)] > 0; }
    int perkCount(PerkId p) const { return perks[static_cast<size_t>(p)]; }
    /// Puts a spell into the first free inventory slot; false if full.
    bool addSpell(SpellId s);
    void applyPerk(PerkId p);

    float damageDealtMul() const;
    float damageTakenMul() const;
    float moveSpeedMul() const { return 1.0f + 0.25f * static_cast<float>(perkCount(PerkId::Swiftness)); }
    float manaRegenMul() const { return 1.0f + 0.6f * static_cast<float>(perkCount(PerkId::ArcaneFlow)); }
    float castSpeedMul() const;
};

RunState newRun(uint32_t seed, Difficulty difficulty, bool daily);

} // namespace pm
