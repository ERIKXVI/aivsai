#include "game/Run.hpp"

#include <cmath>

namespace pm {

bool RunState::addSpell(SpellId s) {
    for (SpellId& slot : inventory) {
        if (slot == SpellId::None) {
            slot = s;
            return true;
        }
    }
    return false;
}

void RunState::applyPerk(PerkId p) {
    perks[static_cast<size_t>(p)] = static_cast<uint8_t>(perks[static_cast<size_t>(p)] + 1);
    switch (p) {
    case PerkId::Vitality:
        maxHp += 40.0f;
        hp = maxHp;
        break;
    case PerkId::GlassCannon:
        maxHp = std::max(20.0f, maxHp * 0.5f);
        hp = std::min(hp, maxHp);
        break;
    case PerkId::Levitation: levitationMax *= 2.0f; break;
    case PerkId::Expansion:
        for (Wand& w : wands) {
            w.capacity += 2;
            w.slots.resize(static_cast<size_t>(w.capacity), SpellId::None);
            w.resetDeck();
        }
        break;
    default: break;
    }
}

float RunState::damageDealtMul() const {
    float m = hasPerk(PerkId::GlassCannon) ? 2.0f : 1.0f;
    if (difficulty == Difficulty::Apprentice) m *= 1.25f;
    return m;
}

float RunState::damageTakenMul() const {
    switch (difficulty) {
    case Difficulty::Apprentice: return 0.55f;
    case Difficulty::Adept: return 1.0f;
    case Difficulty::Archmage: return 1.5f;
    }
    return 1.0f;
}

float RunState::castSpeedMul() const {
    return std::pow(0.8f, static_cast<float>(perkCount(PerkId::PerpetualMotion)));
}

RunState newRun(uint32_t seed, Difficulty difficulty, bool daily) {
    RunState r;
    r.seed = seed;
    r.daily = daily;
    r.difficulty = difficulty;
    r.wands.push_back(makeStarterWand(0));
    r.wands.push_back(makeStarterWand(1));
    r.inventory.fill(SpellId::None);
    r.addSpell(SpellId::BouncingBurst);
    r.flask.mat = mat::Water;
    r.flask.amount = 500;
    if (difficulty == Difficulty::Apprentice) {
        r.maxHp = r.hp = 170.0f;
        r.addSpell(SpellId::DamageUp);
    } else if (difficulty == Difficulty::Archmage) {
        r.maxHp = r.hp = 90.0f;
    }
    return r;
}

} // namespace pm
