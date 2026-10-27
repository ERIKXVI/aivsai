#pragma once
// Wands hold a deck of spell cards. Casting draws cards in order: modifiers accumulate,
// multicasts draw extra cards, projectiles are launched, and trigger projectiles capture the
// following card as their impact payload. When the deck runs out the wand recharges.

#include <memory>
#include <string>
#include <vector>

#include "core/Rng.hpp"
#include "game/Spells.hpp"

namespace pm {

struct CastShot;

struct ProjectileLaunch {
    SpellId spell = SpellId::None;
    ProjectileDef def;
    float critChance = 0.0f;
    float angleOffset = 0.0f;          ///< radians, relative to the aim
    std::shared_ptr<CastShot> payload; ///< cast on impact (trigger spells)
};

struct CastShot {
    std::vector<ProjectileLaunch> projectiles;
    float mana = 0.0f;
    float castDelay = 0.0f;
    float recharge = 0.0f;
    float spreadDeg = 0.0f;
};

enum class CastStatus { Cast, Cooldown, NoMana, Empty };

struct CastOutcome {
    CastStatus status = CastStatus::Cooldown;
    CastShot shot;
};

struct Wand {
    std::string name = "Wand";
    int capacity = 4;
    float castDelay = 0.15f;   ///< seconds between casts
    float rechargeTime = 0.5f; ///< seconds after the deck is exhausted
    float manaMax = 300.0f;
    float manaCharge = 100.0f; ///< mana per second
    float spreadDeg = 2.0f;
    float speedMul = 1.0f;
    int spellsPerCast = 1;
    bool shuffle = false;
    std::vector<SpellId> slots;

    // Look
    uint32_t handleColor = 0x6a4a2a;
    uint32_t gemColor = 0x80c0ff;
    int length = 9;

    // Runtime
    float mana = 300.0f;
    float cooldown = 0.0f;
    float cooldownTotal = 0.0f;
    bool recharging = false;
    int deckPos = 0;
    std::vector<int> order; ///< shuffled slot order (shuffle wands)

    void tick(float dt);
    CastOutcome tryCast(Rng& rng);
    /// Number of non-empty slots.
    int spellCount() const;
    /// Resets the deck to the start (after editing).
    void resetDeck();
    /// Builds the shot the wand would cast right now without consuming anything (UI preview).
    CastShot preview() const;
};

/// Builds a cast from `slots` starting at `pos`, drawing `draws` projectile groups.
/// `pos` is advanced past every consumed card.
CastShot buildShot(const std::vector<SpellId>& slots, const std::vector<int>& order, int& pos, int draws);
/// Puts `s` at `at`, pushing the spells from there one slot to the right into the first empty
/// slot after it. Returns false, changing nothing, when no slot after `at` is empty.
bool insertSpell(std::vector<SpellId>& slots, int at, SpellId s);
/// The deck's last spell when it is a modifier or multicast, with nothing after it to work on;
/// None otherwise.
SpellId trailingBooster(const std::vector<SpellId>& slots);

Wand makeStarterWand(int index);
/// Random wand appropriate for a depth tier (0..kMaxTier), pre-loaded with a few spells.
Wand makeRandomWand(int tier, Rng& rng);
/// Random spell appropriate for a depth tier.
SpellId randomSpell(int tier, Rng& rng);
/// A spell lying on the floor or in a chest: like randomSpell, but now and then from one tier
/// deeper.
SpellId randomFind(int tier, Rng& rng);
/// A rare find: an attack spell from deeper down than `tier` (tiers 2-3 even on the first
/// stage), so the spells that bend the world turn up within the first hour. One draw.
SpellId randomRareSpell(int tier, Rng& rng);

} // namespace pm
