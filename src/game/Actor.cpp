#include "game/Actor.hpp"

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

} // namespace pm
