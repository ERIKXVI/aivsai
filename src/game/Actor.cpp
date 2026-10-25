#include "game/Actor.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "core/Timing.hpp"
#include "game/Level.hpp"

namespace pm {

float Actor::applyDamage(Level& level, const DamageInfo& info) {
    if (dead || info.amount <= 0.0f) return 0.0f;
    if (invulnerable > 0.0f && !info.silent) return 0.0f;
    float dealt = info.amount * resist[static_cast<size_t>(info.type)];
    if (dealt <= 0.0f) return 0.0f;
    hp -= dealt;
    if (info.cause) {
        lastHitCause = info.cause;
        lastHitSelf = info.source == team;
        lastHitType = info.type;
    }
    if (!info.silent) {
        hurtFlash = 0.12f;
        body.vel += info.knockback;
    }
    onDamaged(level, info, dealt);
    if (hp <= 0.0f) {
        hp = 0.0f;
        dead = true;
        onKilled(level, info);
    }
    return dealt;
}

void Actor::updateStatus(Level& level, const Environment& env) {
    const float dt = kTickDt;
    hurtFlash = std::max(0.0f, hurtFlash - dt);
    invulnerable = std::max(0.0f, invulnerable - dt);
    contactDamageCooldown = std::max(0.0f, contactDamageCooldown - dt);
    status.wet = std::max(0.0f, status.wet - dt);
    status.oiled = std::max(0.0f, status.oiled - dt);
    status.chilled = std::max(0.0f, status.chilled - dt);
    status.shocked = std::max(0.0f, status.shocked - dt);

    const bool fireImmune = resist[static_cast<size_t>(DamageType::Fire)] <= 0.0f;
    if (env.submerged > 0.25f && env.liquid != mat::Empty) {
        const MaterialDef& liquid = matDef(env.liquid);
        if (liquid.flags & matflag::kExtinguisher) {
            status.wet = 5.0f;
            status.burning = 0.0f;
        }
        if (env.liquid == mat::Oil) status.oiled = 8.0f;
    }
    if (env.extinguishing) status.burning = 0.0f;
    if (env.fire && status.wet <= 0.0f && !fireImmune) status.burning = std::max(status.burning, status.oiled > 0.0f ? 7.0f : 3.0f);

    if (status.burning > 0.0f) {
        status.burning -= dt;
        if (!fireImmune) applyDamage(level, {burnDps() * dt, DamageType::Fire, {}, Team::Monster, false, true, "flames"});
        // Burning creatures shed flames and set flammable surroundings alight.
        Rng& rng = level.rng();
        if (rng.chance(0.5f)) {
            Vec2 p{body.pos.x + rng.range(-body.halfW, body.halfW), body.pos.y + rng.range(-body.halfH, body.halfH)};
            level.particles().addVisual(p, {rng.range(-10.0f, 10.0f), rng.range(-60.0f, -20.0f)}, {255, 170, 60, 255},
                                        {255, 120, 30, 255}, rng.range(0.2f, 0.5f), -0.2f);
        }
        if (rng.chance(0.15f)) {
            int x = floorToInt(body.pos.x + rng.range(-body.halfW - 1.0f, body.halfW + 1.0f));
            int y = floorToInt(body.pos.y + body.halfH + rng.range(-2.0f, 1.0f));
            level.world().ignite(x, y);
        }
    }

    // Damaging materials (lava, acid, toxic sludge...) hurt while touched.
    if (env.contactDps > 0.0f && env.contactMat != mat::Empty) {
        DamageType t = DamageType::Physical;
        const MaterialDef& m = matDef(env.contactMat);
        if (m.flags & matflag::kHot) t = DamageType::Fire;
        else if (m.flags & matflag::kCorrosive) t = DamageType::Acid;
        else t = DamageType::Toxic;
        float dealt = applyDamage(level, {env.contactDps * dt, t, {}, Team::Monster, false, true, m.name});
        if (dealt > 0.0f && level.rng().chance(0.08f)) hurtFlash = 0.06f;
    }

    if (env.healing && hp < maxHp) hp = std::min(maxHp, hp + 18.0f * dt);
    if (status.poisoned > 0.0f) {
        status.poisoned -= dt;
        applyDamage(level, {4.0f * dt, DamageType::Toxic, {}, Team::Monster, false, true, "poison"});
    }
}

/// "Slain by a Fire Imp", "Undone by lava", "Undone by your own Bomb"...
std::string deathCause(const Actor& victim) {
    if (victim.lastHitCause == nullptr) return {};
    std::string cause = victim.lastHitCause;
    if (victim.lastHitSelf) return "Undone by your own " + cause;
    if (enemyNamed(victim.lastHitCause)) {
        if (cause.rfind("The ", 0) == 0) return "Slain by the " + cause.substr(4);
        const bool vowel = std::strchr("AEIOU", cause[0]) != nullptr;
        return std::string("Slain by ") + (vowel ? "an " : "a ") + cause;
    }
    std::transform(cause.begin(), cause.end(), cause.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return "Undone by " + cause;
}

const char* deathAdvice(const Actor& victim) {
    const char* cause = victim.lastHitCause;
    if (cause == nullptr) return nullptr;
    if (victim.lastHitSelf) return "Your own spells can hurt you: mind bounces, blast radii and the fires you start.";
    if (const auto killer = enemyNamed(cause)) {
        const EnemyDef& def = enemyDef(*killer);
        if (def.guardian)
            return "Guardians are optional. Come back stronger, and use the ledges - or fight from the doorway it cannot pass.";
        if (def.behaviour == Behaviour::Boss)
            return "The Rebis grows wilder as it weakens. Use the floating ledges and never stop moving.";
        if (def.behaviour == Behaviour::Kamikaze) return "Bomb Beetles burst when they reach you. Strike them from afar.";
        if (def.attack != SpellId::None)
            return "Ranged creatures glow just before they fire. Sidestep, or break their line of sight.";
        return "Keep your distance: back away while casting, and levitate out of reach.";
    }
    switch (victim.lastHitType) {
    case DamageType::Fire:
        if (std::strstr(cause, "Lava") || std::strstr(cause, "lava"))
            return "Lava ignites whatever it touches. Pour water on it to cool it into obsidian.";
        return "Burning? Water puts out the flames: jump in, or drink or pour a flask of water.";
    case DamageType::Explosion: return "Gunpowder chains into great blasts. Keep fire well away from powder stores.";
    case DamageType::Acid: return "Acid eats flesh and stone alike. Never wade through it.";
    case DamageType::Toxic: return "Poison lingers after you leave the sludge. Carry Vitae to heal through it.";
    case DamageType::Electric: return "Electricity arcs through water and metal. Stay dry when sparks fly.";
    default: break;
    }
    if (std::strcmp(cause, "drowning") == 0) return "Watch the breath bar when submerged, and levitate up out of deep liquid.";
    return nullptr;
}

} // namespace pm
