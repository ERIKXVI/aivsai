#include "game/Run.hpp"

#include <algorithm>
#include <cmath>

namespace pm {

bool RunState::addSpell(SpellId s) {
    noteSpell(s);
    for (SpellId& slot : inventory) {
        if (slot == SpellId::None) {
            slot = s;
            return true;
        }
    }
    return false;
}

bool safeToAutoSlot(SpellId s) {
    // Small impact bursts (Firebolt, Lightning) are fine; anything that can catch the caster is not.
    constexpr float kMaxBlast = 5.0f;
    const SpellDef& def = spellDef(s);
    if (def.type != SpellType::Projectile || def.proj.explosionRadius > kMaxBlast) return false;
    // Nor does a spell whose spilled matter burns or corrodes whoever it lands on (Acid Glob).
    const ProjectileDef& pd = def.proj;
    if (pd.spawnMat != mat::Empty && pd.spawnAmount > 0 && matDef(pd.spawnMat).contactDamage > 0.0f) return false;
    switch (s) {
    case SpellId::DiggingBolt:
    case SpellId::TeleportBolt:
    case SpellId::BlackHole:
    case SpellId::OilOrb:
    case SpellId::LavaOrb:
    case SpellId::SporeBomb: return false;
    default: return true;
    }
}

bool RunState::canAutoSlot(SpellId s) const {
    const Wand* w = currentWand();
    return w && safeToAutoSlot(s) && std::find(w->slots.begin(), w->slots.end(), SpellId::None) != w->slots.end();
}

bool RunState::trySlotIntoActiveWand(SpellId s) {
    if (!canAutoSlot(s)) return false;
    Wand* w = currentWand();
    *std::find(w->slots.begin(), w->slots.end(), SpellId::None) = s;
    noteSpell(s);
    w->resetDeck();
    return true;
}

void RunState::applyPerk(PerkId p) {
    perks[static_cast<size_t>(p)] = static_cast<uint8_t>(perks[static_cast<size_t>(p)] + 1);
    perksSeen |= 1u << static_cast<unsigned>(p);
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

const char* difficultyName(Difficulty d) {
    switch (d) {
    case Difficulty::Apprentice: return "Apprentice";
    case Difficulty::Adept: return "Adept";
    case Difficulty::Archmage: return "Archmage";
    }
    return "";
}

const char* difficultyDetail(Difficulty d) {
    // Keep in step with damageDealtMul, damageTakenMul, foeHealthMul and newRun.
    switch (d) {
    case Difficulty::Apprentice:
        return "50 more health, foes deal 45% less damage and are 20% frailer, your spells hit 25% harder, "
               "and you start with a Damage Plus card.";
    case Difficulty::Adept: return "The intended trial.";
    case Difficulty::Archmage: return "30 less health, foes deal 50% more damage and are 30% hardier. For seasoned alchemists.";
    }
    return "";
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

float RunState::foeHealthMul() const {
    switch (difficulty) {
    case Difficulty::Apprentice: return 0.8f;
    case Difficulty::Adept: return 1.0f;
    case Difficulty::Archmage: return 1.3f;
    }
    return 1.0f;
}

float RunState::castSpeedMul() const {
    return std::pow(0.8f, static_cast<float>(perkCount(PerkId::PerpetualMotion)));
}

RunState newRun(uint32_t seed, Difficulty difficulty, bool daily, LoadoutId loadout, int ordeal) {
    RunState r;
    r.seed = seed;
    r.daily = daily;
    r.difficulty = difficulty;
    r.ordeal = clamp(ordeal, 0, kMaxOrdeal);
    r.wands.push_back(makeStarterWand(0));
    r.wands.push_back(makeStarterWand(1));
    r.inventory.fill(SpellId::None);
    // A seeded first card for the satchel, so the guided first wand edit differs from run to
    // run (the Daily Rite's is the same for everyone). Apprentices get Damage Plus anyway.
    static constexpr SpellId kGifts[] = {SpellId::BouncingBurst, SpellId::FireBolt, SpellId::IceShard,
                                         SpellId::DamageUp,      SpellId::DoubleCast, SpellId::Critical};
    Rng gift(seed ^ 0x6a1f7u);
    SpellId first = gift.pick(kGifts);
    if (difficulty == Difficulty::Apprentice && first == SpellId::DamageUp) first = SpellId::BouncingBurst;
    // Other kits pack their own satchel; adding it to theirs would mark it seen in the Grimoire.
    if (loadout == LoadoutId::Apprentice) r.addSpell(first);
    r.flask.mat = mat::Water;
    r.flask.amount = 500;
    equipLoadout(r, loadout);
    if (difficulty == Difficulty::Apprentice) {
        r.maxHp += 50.0f;
        r.addSpell(SpellId::DamageUp);
    } else if (difficulty == Difficulty::Archmage) {
        r.maxHp -= 30.0f;
    }
    if (r.under(Ordeal::FrailFlesh)) r.maxHp = std::round(r.maxHp * 0.75f);
    r.hp = r.maxHp;
    return r;
}

} // namespace pm
