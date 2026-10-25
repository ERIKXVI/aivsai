#include "game/Wand.hpp"

#include <algorithm>
#include <numeric>

namespace pm {

namespace {

int slotAt(const std::vector<int>& order, int i) { return order.empty() ? i : order[static_cast<size_t>(i)]; }

void applyModifier(ProjectileLaunch& l, const ModifierDef& m) {
    ProjectileDef& p = l.def;
    p.damage = (p.damage + m.damageAdd) * m.damageMul;
    if (p.explosionRadius > 0.0f) p.explosionDamage = (p.explosionDamage + m.damageAdd) * m.damageMul;
    p.speed *= m.speedMul;
    p.lifetime *= m.lifetimeMul;
    p.gravity += m.gravityAdd;
    if (m.zeroGravity) p.gravity = 0.0f;
    if (m.explosionAdd > 0.0f) {
        p.explosionRadius += m.explosionAdd;
        p.explosionDamage += m.explosionAdd * 3.0f;
    }
    p.radius *= m.radiusMul;
    p.bounces += m.bouncesAdd;
    if (m.trailMat != mat::Empty) {
        p.trailMat = m.trailMat;
        p.trailRate = std::max(p.trailRate, 1.0f);
    }
    p.flags |= m.addFlags;
    l.critChance += m.critChance;
}

} // namespace

CastShot buildShot(const std::vector<SpellId>& slots, const std::vector<int>& order, int& pos, int draws) {
    CastShot shot;
    std::vector<const ModifierDef*> mods;
    bool fan = false;
    int remaining = draws;
    const int n = static_cast<int>(slots.size());
    while (remaining > 0) {
        // Skip empty slots.
        while (pos < n && slots[static_cast<size_t>(slotAt(order, pos))] == SpellId::None) ++pos;
        if (pos >= n) break;
        const SpellId id = slots[static_cast<size_t>(slotAt(order, pos))];
        ++pos;
        const SpellDef& d = spellDef(id);
        shot.mana += d.mana;
        shot.castDelay += d.castDelay;
        shot.recharge += d.recharge;
        switch (d.type) {
        case SpellType::Modifier:
            mods.push_back(&d.mod);
            shot.spreadDeg += d.mod.spreadAdd;
            break;
        case SpellType::Multicast:
            remaining += d.multicast - 1;
            shot.spreadDeg += d.spread;
            if (id == SpellId::Formation) fan = true;
            break;
        case SpellType::Projectile: {
            ProjectileLaunch launch;
            launch.spell = id;
            launch.def = d.proj;
            if (d.trigger) {
                auto payload = std::make_shared<CastShot>(buildShot(slots, order, pos, 1));
                shot.mana += payload->mana;
                shot.castDelay += payload->castDelay;
                shot.recharge += payload->recharge;
                if (!payload->projectiles.empty()) launch.payload = std::move(payload);
            }
            shot.projectiles.push_back(std::move(launch));
            --remaining;
            break;
        }
        }
    }
    for (ProjectileLaunch& l : shot.projectiles)
        for (const ModifierDef* m : mods) applyModifier(l, *m);
    if (fan && shot.projectiles.size() > 1) {
        const float total = 0.9f; // ~52 degrees
        const float stepA = total / static_cast<float>(shot.projectiles.size() - 1);
        for (size_t i = 0; i < shot.projectiles.size(); ++i)
            shot.projectiles[i].angleOffset = -total * 0.5f + stepA * static_cast<float>(i);
    }
    return shot;
}

void Wand::tick(float dt) {
    mana = std::min(manaMax, mana + manaCharge * dt);
    if (cooldown > 0.0f) {
        cooldown -= dt;
        if (cooldown <= 0.0f) {
            cooldown = 0.0f;
            recharging = false;
        }
    }
}

int Wand::spellCount() const {
    return static_cast<int>(std::count_if(slots.begin(), slots.end(), [](SpellId s) { return s != SpellId::None; }));
}

void Wand::resetDeck() {
    deckPos = 0;
    order.clear();
    if (shuffle) {
        order.resize(slots.size());
        std::iota(order.begin(), order.end(), 0);
    }
}

CastShot Wand::preview() const {
    int pos = deckPos;
    return buildShot(slots, order, pos, spellsPerCast);
}

CastOutcome Wand::tryCast(Rng& rng) {
    CastOutcome out;
    if (cooldown > 0.0f) return out;
    if (shuffle && order.size() != slots.size()) resetDeck();

    // Cards left after the read head?
    auto cardsRemain = [&] {
        for (int i = deckPos; i < static_cast<int>(slots.size()); ++i)
            if (slots[static_cast<size_t>(order.empty() ? i : order[static_cast<size_t>(i)])] != SpellId::None) return true;
        return false;
    };
    // Back to the top of the deck after a recharge (shuffle wands reshuffle).
    auto startRecharge = [&](float extra) {
        deckPos = 0;
        recharging = true;
        cooldown = std::max(cooldown, std::max(0.05f, rechargeTime + extra));
        if (shuffle) {
            for (size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[static_cast<size_t>(rng.rangeInt(0, static_cast<int>(i) - 1))]);
        }
    };

    int pos = deckPos;
    CastShot shot = buildShot(slots, order, pos, spellsPerCast);
    if (shot.projectiles.empty()) {
        // Only modifiers (or nothing) left before the end of the deck: recharge without casting.
        out.status = CastStatus::Empty;
        if (spellCount() == 0) return out;
        startRecharge(0.0f);
        cooldownTotal = cooldown;
        return out;
    }
    if (shot.mana > manaMax) {
        // Dearer than the wand's whole mana pool, so it can never be cast: skip past it rather
        // than locking the deck.
        out.status = CastStatus::NoMana;
        deckPos = pos;
        cooldown = std::max(0.03f, castDelay);
        if (!cardsRemain()) startRecharge(0.0f);
        cooldownTotal = cooldown;
        return out;
    }
    if (shot.mana > mana) {
        out.status = CastStatus::NoMana;
        return out;
    }
    mana -= shot.mana;
    mana = std::min(mana, manaMax);
    deckPos = pos;
    cooldown = std::max(0.03f, castDelay + shot.castDelay);
    if (!cardsRemain()) startRecharge(shot.recharge);
    cooldownTotal = cooldown;
    shot.spreadDeg += spreadDeg;
    for (ProjectileLaunch& l : shot.projectiles) l.def.speed *= speedMul;
    out.status = CastStatus::Cast;
    out.shot = std::move(shot);
    return out;
}

Wand makeStarterWand(int index) {
    Wand w;
    if (index == 0) {
        w.name = "Apprentice's Rod";
        w.capacity = 4;
        w.castDelay = 0.11f;
        w.rechargeTime = 0.32f;
        w.manaMax = w.mana = 260.0f;
        w.manaCharge = 110.0f;
        w.spreadDeg = 1.5f;
        w.slots = {SpellId::SparkBolt, SpellId::SparkBolt, SpellId::SparkBolt, SpellId::None};
        w.handleColor = 0x7a5232;
        w.gemColor = 0x8ad8ff;
        w.length = 9;
    } else {
        w.name = "Delver's Wand";
        w.capacity = 3;
        w.castDelay = 0.08f;
        w.rechargeTime = 0.6f;
        w.manaMax = w.mana = 200.0f;
        w.manaCharge = 80.0f;
        w.spreadDeg = 0.0f;
        w.slots = {SpellId::DiggingBolt, SpellId::Bomb, SpellId::None};
        w.handleColor = 0x4a4a52;
        w.gemColor = 0xffb040;
        w.length = 8;
    }
    w.resetDeck();
    return w;
}

SpellId randomSpell(int tier, Rng& rng) {
    // Weighted towards spells near the current tier; deeper spells are rarer.
    float total = 0.0f;
    float weights[kSpellCount] = {};
    for (int i = 1; i < kSpellCount; ++i) {
        const SpellDef& d = spellDef(static_cast<SpellId>(i));
        if (d.tier > tier) continue;
        float w = 1.0f + 0.4f * static_cast<float>(d.tier);
        if (d.type == SpellType::Projectile) w *= 1.3f;
        weights[i] = w;
        total += w;
    }
    float r = rng.uniform() * total;
    for (int i = 1; i < kSpellCount; ++i) {
        if (weights[i] <= 0.0f) continue;
        if (r < weights[i]) return static_cast<SpellId>(i);
        r -= weights[i];
    }
    return SpellId::SparkBolt;
}

Wand makeRandomWand(int tier, Rng& rng) {
    static constexpr const char* kPrefixes[] = {"Rod", "Wand", "Staff", "Scepter", "Branch", "Spire", "Focus"};
    static constexpr const char* kSuffixes[] = {"of Embers", "of Salt", "of Sulphur", "of the Deep", "of Quicksilver",
                                                "of Ash", "of the Rebis", "of Mercury", "of the Ouroboros", "of Vitriol"};
    Wand w;
    const float t = static_cast<float>(tier);
    w.capacity = std::min(12, 2 + tier + rng.rangeInt(0, 3));
    w.castDelay = std::max(0.02f, rng.range(0.06f, 0.32f) - t * 0.02f);
    w.rechargeTime = std::max(0.1f, rng.range(0.25f, 1.0f) - t * 0.07f);
    w.manaMax = 160.0f + t * 110.0f + rng.range(0.0f, 160.0f);
    w.manaCharge = 60.0f + t * 45.0f + rng.range(0.0f, 60.0f);
    w.spreadDeg = rng.range(-1.0f, 7.0f);
    w.speedMul = rng.range(0.9f, 1.25f);
    w.spellsPerCast = rng.chance(0.08f + 0.05f * t) ? 2 : 1;
    w.shuffle = rng.chance(0.3f);
    if (w.shuffle) {
        // Shuffle wands compensate with better raw stats.
        w.manaMax *= 1.3f;
        w.castDelay *= 0.7f;
        w.rechargeTime *= 0.7f;
    }
    w.mana = w.manaMax;
    w.name = std::string(kPrefixes[rng.rangeInt(0, 6)]) + " " + kSuffixes[rng.rangeInt(0, 9)];
    w.slots.assign(static_cast<size_t>(w.capacity), SpellId::None);
    // Always at least one projectile so the wand works out of the box.
    int fill = std::min(w.capacity, 1 + rng.rangeInt(0, 2));
    for (int i = 0; i < fill; ++i) {
        SpellId s = randomSpell(tier, rng);
        if (i == fill - 1 && spellDef(s).type != SpellType::Projectile) {
            do s = randomSpell(tier, rng);
            while (spellDef(s).type != SpellType::Projectile);
        }
        w.slots[static_cast<size_t>(i)] = s;
    }
    static constexpr uint32_t kHandles[] = {0x7a5232, 0x4a4a52, 0x8a7a5a, 0x5a3a5a, 0x3a5a4a, 0x9a8a7a};
    static constexpr uint32_t kGems[] = {0x8ad8ff, 0xffb040, 0xff5a8a, 0x8aff7a, 0xc080ff, 0xfff080};
    w.handleColor = kHandles[rng.rangeInt(0, 5)];
    w.gemColor = kGems[rng.rangeInt(0, 5)];
    w.length = 7 + std::min(5, w.capacity / 2);
    w.resetDeck();
    return w;
}

} // namespace pm
