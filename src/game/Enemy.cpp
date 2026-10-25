#include "game/Enemy.hpp"

#include <cmath>

#include "core/Timing.hpp"
#include "game/Level.hpp"
#include "render/ColorUtil.hpp"
#include "render/SceneRenderer.hpp"

namespace pm {

namespace {

constexpr int kWormSegments = 9;
constexpr float kWormSpacing = 5.5f;
/// Ranged attacks only begin inside this box around the player, which stays in view even when
/// the camera looks ahead, so nothing opens fire from off screen.
constexpr float kEngageHalfW = 190.0f, kEngageHalfH = 110.0f;

float sign(float v) { return v < 0.0f ? -1.0f : 1.0f; }

} // namespace

Enemy::Enemy(EnemyType type, Vec2 pos, uint32_t id_, float hpScale) : type_(type), def_(&enemyDef(type)) {
    id = id_;
    body.pos = body.prevPos = pos;
    body.halfW = def_->halfW;
    body.halfH = def_->halfH;
    body.stepHeight = def_->behaviour == Behaviour::Brute ? 4.0f : 3.0f;
    flying = def_->flying;
    if (flying) body.gravityScale = 0.0f;
    maxHp = hp = def_->hp * hpScale;
    resist = def_->resist;
    team = Team::Monster;
    home_ = pos;
    lastSeen_ = pos;
    wanderDir_ = (id_ & 1u) ? 1.0f : -1.0f;
    attackTimer_ = def_->attackCooldown * 0.5f;
    if (def_->behaviour == Behaviour::Burrower) {
        body.collides = false;
        for (int i = 0; i < kWormSegments; ++i) segments.push_back(pos + Vec2{0.0f, kWormSpacing * static_cast<float>(i)});
    }
}

void Enemy::makeElite() {
    elite_ = true;
    maxHp *= 2.6f;
    hp = maxHp;
}

void Enemy::tick(Level& level) {
    body.prevPos = body.pos;
    const float dt = kTickDt;
    animTime_ += dt;
    attackTimer_ -= dt;
    stateTimer_ -= dt;

    World& world = level.world();
    if (body.collides) unbury(body, world);
    Environment env = sampleEnvironment(body, world);
    updateStatus(level, env);
    if (dead) return;
    if (def_->behaviour == Behaviour::Companion) {
        tickCompanion(level);
        return;
    }

    Player& pl = level.player();
    Vec2 toPlayer = pl.body.pos - body.pos;
    float dist = toPlayer.length();
    bool sees = pl.alive() && dist < def_->sight &&
                (def_->behaviour == Behaviour::Burrower || level.lineOfSight(body.pos, pl.body.pos));
    if (sees) {
        if (!aggro_ && def_->behaviour == Behaviour::Boss) {
            level.audio().play(Sfx::Roar, 1.0f);
            level.addShake(0.6f);
        }
        aggro_ = true;
        lastSeen_ = pl.body.pos;
        lostSight_ = 0.0f;
    } else if (aggro_) {
        lostSight_ += dt;
        if (lostSight_ > 7.0f && def_->behaviour != Behaviour::Boss) aggro_ = false;
    }
    if (!pl.alive()) aggro_ = false;

    switch (def_->behaviour) {
    case Behaviour::Hopper: tickHopper(level, toPlayer, sees); break;
    case Behaviour::Walker: tickWalker(level, toPlayer, sees); break;
    case Behaviour::Flyer: tickFlyer(level, toPlayer, sees); break;
    case Behaviour::Caster: tickCaster(level, toPlayer, sees); break;
    case Behaviour::Spore: tickSpore(level, toPlayer, sees); break;
    case Behaviour::Turret: tickTurret(level, toPlayer, sees); break;
    case Behaviour::Kamikaze: tickKamikaze(level, toPlayer, sees); break;
    case Behaviour::Burrower: tickBurrower(level, toPlayer, sees); break;
    case Behaviour::Brute: tickBrute(level, toPlayer, sees); break;
    case Behaviour::Boss: tickBoss(level, toPlayer, sees); break;
    case Behaviour::Companion: break;
    }
    if (dead) return;

    // Chilled creatures move sluggishly.
    if (status.chilled > 0.0f) body.vel.x *= 0.9f;
    if (env.submerged > 0.5f && !flying) {
        body.vel.y -= kGravity * 0.7f * dt;
        body.vel *= 0.96f;
    }
    if (def_->behaviour != Behaviour::Burrower) moveBody(body, world, dt);
    if (std::fabs(body.vel.x) > 3.0f) facingRight_ = body.vel.x > 0.0f;
    if (aggro_ && def_->behaviour != Behaviour::Flyer && def_->behaviour != Behaviour::Hopper) facingRight_ = toPlayer.x > 0.0f;
    contactPlayer(level);
}

void Enemy::contactPlayer(Level& level) {
    if (def_->contactDamage <= 0.0f || contactDamageCooldown > 0.0f) return;
    Player& pl = level.player();
    if (!pl.alive()) return;
    bool touching = pl.bounds().overlaps(bounds());
    if (!touching && !segments.empty()) {
        for (const Vec2& s : segments) {
            if (pl.bounds().overlaps(Rectf{s.x - 3.5f, s.y - 3.5f, 7.0f, 7.0f})) {
                touching = true;
                break;
            }
        }
    }
    if (!touching) return;
    Vec2 kb = Vec2{sign(pl.body.pos.x - body.pos.x) * 140.0f, -90.0f};
    float scale = 1.0f + 0.1f * static_cast<float>(level.layout().levelIndex);
    pl.applyDamage(level, {def_->contactDamage * scale * damageScale(), def_->contactType, kb, Team::Monster, false, false, def_->name});
    if (def_->contactType == DamageType::Ice) pl.status.chilled = 2.0f;
    contactDamageCooldown = 0.7f;
    if (level.run().hasPerk(PerkId::Thorns))
        applyDamage(level, {15.0f * level.run().perkCount(PerkId::Thorns), DamageType::Physical, kb * -1.0f, Team::Player});
}

void Enemy::fireAt(Level& level, Vec2 target, SpellId spell, float spreadRad) {
    ProjectileLaunch l;
    l.spell = spell;
    l.def = spellDef(spell).proj;
    l.def.speed *= 0.72f; // monster spells are slower so they can be dodged
    if (l.def.lifetime < 1.2f) l.def.lifetime *= 1.5f;
    l.def.explosionDamage *= 0.5f; // monster explosives are scary, not lethal
    Vec2 origin = body.pos + Vec2{0.0f, -body.halfH * 0.3f};
    Vec2 to = target - origin;
    float angle = angleOf(to);
    if (l.def.gravity > 0.0f) {
        // Lob arcing projectiles: tilt the throw upwards (y grows downwards).
        float lift = clamp(to.length() / 380.0f, 0.05f, 0.6f) * std::min(1.0f, l.def.gravity);
        angle += to.x >= 0.0f ? -lift : lift;
    }
    angle += level.rng().range(-spreadRad, spreadRad);
    float dmgMul = (1.0f + 0.1f * static_cast<float>(level.layout().levelIndex)) * damageScale();
    level.launch(l, origin, angle, team, id, dmgMul);
}

bool Enemy::readyToFire(Level& level, bool wantsToFire) {
    constexpr float kWindup = 0.4f;
    if (windup_ > 0.0f) {
        windup_ -= kTickDt;
        // Sparks gather at the caster while it charges.
        Rng& rng = level.rng();
        const Vec2 core = body.pos + Vec2{0.0f, -body.halfH * 0.3f};
        const Vec2 from = core + fromAngle(rng.range(0.0f, kTau), rng.range(8.0f, 14.0f));
        const Color c = def_->glow ? hexColor(def_->glow) : Color{255, 240, 200, 255};
        level.particles().addVisual(from, (core - from) * 4.0f, {c.r, c.g, c.b, 230}, {c.r, c.g, c.b, 0}, 0.22f, 0.0f,
                                    partflag::kFade | partflag::kNoGravity);
        if (windup_ <= 0.0f) {
            windup_ = 0.0f;
            return true;
        }
        return false;
    }
    const Vec2 toPlayer = level.player().body.pos - body.pos;
    if (wantsToFire && std::fabs(toPlayer.x) < kEngageHalfW && std::fabs(toPlayer.y) < kEngageHalfH) windup_ = kWindup;
    return false;
}

void Enemy::tickHopper(Level& level, Vec2 toPlayer, bool sees) {
    Rng& rng = level.rng();
    if (body.onGround) {
        body.vel.x = approach(body.vel.x, 0.0f, 600.0f * kTickDt);
        if (stateTimer_ <= 0.0f) {
            float dir = aggro_ ? sign(toPlayer.x) : (rng.chance(0.5f) ? -1.0f : 1.0f);
            float power = aggro_ ? rng.range(0.9f, 1.25f) : rng.range(0.4f, 0.7f);
            body.vel = {dir * def_->speed * power * (status.chilled > 0 ? 0.5f : 1.0f), -165.0f * power};
            stateTimer_ = def_->attackCooldown * rng.range(0.7f, 1.3f) * (aggro_ ? 0.8f : 2.0f);
            special_ = 0.25f; // squash timer
            if (sees) level.audio().playAt(Sfx::Squish, body.pos, 0.35f, rng.range(0.9f, 1.2f));
        }
    }
    special_ = std::max(0.0f, special_ - kTickDt);
}

void Enemy::tickWalker(Level& level, Vec2 toPlayer, bool sees) {
    const float dt = kTickDt;
    World& world = level.world();
    float speed = def_->speed * (status.chilled > 0.0f ? 0.5f : 1.0f);
    float dist = toPlayer.length();
    float dir = wanderDir_;
    float targetSpeed = speed * 0.4f;
    if (aggro_) {
        dir = sign(toPlayer.x);
        targetSpeed = speed;
        if (def_->attack != SpellId::None) {
            // Ranged walkers keep their distance and lob explosives.
            if (dist < 55.0f) dir = -dir;
            else if (dist < 120.0f) targetSpeed = 0.0f;
            if (readyToFire(level, sees && attackTimer_ <= 0.0f && dist < 200.0f)) {
                fireAt(level, level.player().body.pos, def_->attack, 0.05f);
                attackTimer_ = def_->attackCooldown * level.rng().range(0.8f, 1.2f);
                special_ = 0.35f;
            }
        } else if (dist < 14.0f) {
            targetSpeed = speed * 0.3f;
            if (attackTimer_ <= 0.0f) {
                special_ = 0.3f; // swing animation; contact damage does the rest
                attackTimer_ = def_->attackCooldown;
                body.vel.x += dir * 60.0f;
            }
        }
    } else {
        // Patrol: turn around at walls and ledges.
        int aheadX = floorToInt(body.pos.x + dir * (body.halfW + 2.0f));
        int footY = floorToInt(body.pos.y + body.halfH + 1.0f);
        bool ledge = true;
        for (int k = 0; k < 6; ++k)
            if (isSolidForCollision(world.matAt(aheadX, footY + k))) ledge = false;
        if ((body.hitWall || ledge) && body.onGround) wanderDir_ = -wanderDir_;
        if (stateTimer_ <= 0.0f) {
            stateTimer_ = level.rng().range(2.0f, 5.0f);
            if (level.rng().chance(0.3f)) wanderDir_ = -wanderDir_;
        }
        dir = wanderDir_;
    }
    body.vel.x = approach(body.vel.x, dir * targetSpeed, 500.0f * dt);
    if (aggro_ && body.hitWall && body.onGround) body.vel.y = -150.0f;
    special_ = std::max(0.0f, special_ - dt);
}

void Enemy::tickFlyer(Level& level, Vec2 toPlayer, bool sees) {
    const float dt = kTickDt;
    float speed = def_->speed * (status.chilled > 0.0f ? 0.5f : 1.0f);
    Vec2 desired;
    if (aggro_) {
        Vec2 target = sees ? level.player().body.pos : lastSeen_;
        Vec2 to = target - body.pos;
        Vec2 perp{-to.y, to.x};
        desired = to.normalized() * speed + perp.normalized() * (std::sin(animTime_ * 5.0f + static_cast<float>(id)) * 35.0f);
        // Periodic swoop.
        if (stateTimer_ <= 0.0f && to.length() < 90.0f) {
            body.vel = to.normalized() * speed * 1.8f;
            stateTimer_ = level.rng().range(1.2f, 2.2f);
        }
    } else {
        Vec2 to = home_ + Vec2{std::sin(animTime_ * 0.7f + static_cast<float>(id)) * 40.0f, std::cos(animTime_ * 1.1f) * 15.0f} - body.pos;
        desired = to.normalized() * speed * 0.4f;
    }
    body.vel = damp(body.vel, desired, 3.0f, dt);
    if (body.hitWall) body.vel.x = -body.vel.x * 0.5f;
    if (body.hitCeiling || body.onGround) body.vel.y = -body.vel.y * 0.5f;
    (void)toPlayer;
}

void Enemy::tickCaster(Level& level, Vec2 toPlayer, bool sees) {
    const float dt = kTickDt;
    float speed = def_->speed * (status.chilled > 0.0f ? 0.5f : 1.0f);
    float dist = toPlayer.length();
    Vec2 desired;
    if (aggro_) {
        Vec2 to = (sees ? level.player().body.pos : lastSeen_) - body.pos;
        Vec2 dir = to.normalized();
        Vec2 perp{-dir.y, dir.x};
        float orbit = std::sin(animTime_ * 0.8f + static_cast<float>(id)) > 0.0f ? 1.0f : -1.0f;
        if (dist > 110.0f || !sees) desired = dir * speed;
        else if (dist < 65.0f) desired = dir * -speed;
        else desired = perp * speed * 0.6f * orbit + Vec2{0.0f, -10.0f};
        if (readyToFire(level, sees && attackTimer_ <= 0.0f)) {
            Vec2 lead = level.player().body.pos + level.player().body.vel * (dist / 300.0f);
            fireAt(level, lead, def_->attack, 0.08f);
            attackTimer_ = def_->attackCooldown * level.rng().range(0.8f, 1.3f);
            special_ = 0.3f;
        }
    } else {
        Vec2 to = home_ + Vec2{std::sin(animTime_ * 0.5f + static_cast<float>(id)) * 30.0f, std::sin(animTime_ * 0.9f) * 10.0f} - body.pos;
        desired = to.normalized() * speed * 0.3f;
    }
    body.vel = damp(body.vel, desired, 2.5f, dt);
    if (body.hitWall) body.vel.x *= -0.5f;
    if (body.hitCeiling || body.onGround) body.vel.y *= -0.5f;
    special_ = std::max(0.0f, special_ - dt);
}

void Enemy::tickSpore(Level& level, Vec2 toPlayer, bool) {
    body.vel.x = 0.0f;
    if (toPlayer.length() < 75.0f && attackTimer_ <= 0.0f) {
        Rng& rng = level.rng();
        for (int i = 0; i < 22; ++i) {
            Vec2 p = body.pos + Vec2{rng.range(-4.0f, 4.0f), -body.halfH - rng.range(0.0f, 3.0f)};
            level.world().placeIfEmpty(floorToInt(p.x + rng.range(-6.0f, 6.0f)), floorToInt(p.y + rng.range(-6.0f, 2.0f)), mat::ToxicGas);
        }
        for (int i = 0; i < 10; ++i)
            level.particles().addMatter(body.pos + Vec2{0.0f, -body.halfH}, {rng.range(-60.0f, 60.0f), rng.range(-90.0f, -30.0f)},
                                        makeCell(mat::Spore, rng.next()));
        attackTimer_ = def_->attackCooldown;
        special_ = 0.5f;
        level.audio().playAt(Sfx::Squish, body.pos, 0.4f, 0.7f);
    }
    special_ = std::max(0.0f, special_ - kTickDt);
}

void Enemy::tickTurret(Level& level, Vec2 toPlayer, bool sees) {
    body.vel.x = 0.0f;
    if (sees && aggro_) {
        // Three-shot burst after a wind-up, then a long recharge.
        if (attackTimer_ <= 0.0f && (special_ >= 1.0f || readyToFire(level, true))) {
            special_ += 1.0f;
            fireAt(level, level.player().body.pos, def_->attack, 0.06f);
            level.audio().playAt(Sfx::CastSpark, body.pos, 0.5f, 0.8f);
            if (special_ >= 3.0f) {
                special_ = 0.0f;
                attackTimer_ = def_->attackCooldown * 2.2f;
            } else {
                attackTimer_ = 0.18f;
            }
        }
    }
    (void)toPlayer;
}

void Enemy::tickKamikaze(Level& level, Vec2 toPlayer, bool sees) {
    float dist = toPlayer.length();
    float dir = aggro_ ? sign(toPlayer.x) : wanderDir_;
    float speed = aggro_ ? def_->speed : def_->speed * 0.3f;
    if (status.chilled > 0.0f) speed *= 0.5f;
    body.vel.x = approach(body.vel.x, dir * speed, 400.0f * kTickDt);
    if (body.hitWall && body.onGround) {
        if (aggro_) body.vel.y = -130.0f;
        else wanderDir_ = -wanderDir_;
    }
    if (aggro_ && dist < 10.0f) {
        // Detonate on contact.
        hp = 0.0f;
        dead = true;
        onKilled(level, {});
    }
    (void)sees;
}

void Enemy::tickBurrower(Level& level, Vec2 toPlayer, bool) {
    const float dt = kTickDt;
    float speed = def_->speed * (status.chilled > 0.0f ? 0.5f : 1.0f);
    Vec2 target = aggro_ ? level.player().body.pos
                         : home_ + Vec2{std::cos(animTime_ * 0.4f) * 60.0f, std::sin(animTime_ * 0.5f) * 40.0f};
    Vec2 to = target - body.pos;
    float cur = body.vel.lengthSq() > 1.0f ? angleOf(body.vel) : angleOf(to);
    float want = angleOf(to) + std::sin(animTime_ * 3.0f) * 0.5f;
    float turn = clamp(wrapAngle(want - cur), -2.6f * dt, 2.6f * dt);
    body.vel = fromAngle(cur + turn, aggro_ ? speed : speed * 0.5f);
    body.pos += body.vel * dt;
    World& w = level.world();
    body.pos.x = clamp(body.pos.x, 8.0f, static_cast<float>(w.width() - 8));
    body.pos.y = clamp(body.pos.y, 8.0f, static_cast<float>(w.height() - 8));
    level.dig(body.pos + body.vel.normalized() * 3.0f, 5.5f, 160, level.rng().chance(0.3f));

    // Body segments trail the head at fixed spacing.
    Vec2 prev = body.pos;
    for (Vec2& s : segments) {
        Vec2 d = s - prev;
        float len = d.length();
        if (len > kWormSpacing) s = prev + d / len * kWormSpacing;
        prev = s;
    }
    if (aggro_ && level.rng().chance(0.02f)) level.addShake(0.05f);
    (void)toPlayer;
}

void Enemy::tickBrute(Level& level, Vec2 toPlayer, bool sees) {
    const float dt = kTickDt;
    float dist = toPlayer.length();
    if (phase_ == 1) {
        // Mid-air during a slam: wait for landing.
        if (body.onGround) {
            phase_ = 0;
            ExplosionSpec s;
            s.radius = 16.0f;
            s.damage = 26.0f;
            s.power = 60;
            s.team = Team::Monster;
            s.knockback = 320.0f;
            s.hurtsOwner = false;
            s.cause = def_->name;
            level.explode(body.pos + Vec2{0.0f, body.halfH}, s);
            level.addShake(0.5f);
        }
        return;
    }
    tickWalker(level, toPlayer, sees);
    if (aggro_ && sees && dist < 36.0f && body.onGround && attackTimer_ <= 0.0f) {
        body.vel = {sign(toPlayer.x) * 40.0f, -170.0f};
        phase_ = 1;
        attackTimer_ = def_->attackCooldown;
        level.audio().playAt(Sfx::Roar, body.pos, 0.35f, 1.6f);
    }
    (void)dt;
}

void Enemy::tickCompanion(Level& level) {
    const float dt = kTickDt;
    Player& pl = level.player();
    hp = std::min(maxHp, hp + 2.0f * dt);
    Vec2 toPlayer = pl.body.pos - body.pos;
    float dist = toPlayer.length();
    if (dist > 220.0f) {
        body.pos = body.prevPos = pl.body.pos + Vec2{-8.0f, -4.0f};
        unbury(body, level.world());
        dist = 0.0f;
    }
    if (dist > 28.0f) body.vel.x = approach(body.vel.x, sign(toPlayer.x) * def_->speed, 500.0f * dt);
    else body.vel.x = approach(body.vel.x, 0.0f, 400.0f * dt);
    if (body.onGround && (body.hitWall || toPlayer.y < -24.0f)) body.vel.y = -175.0f;
    if (Actor* foe = level.nearestHostile(body.pos, Team::Player, 150.0f)) {
        facingRight_ = foe->body.pos.x > body.pos.x;
        if (attackTimer_ <= 0.0f && level.lineOfSight(body.pos, foe->body.pos)) {
            fireAt(level, foe->body.pos, def_->attack, 0.04f);
            attackTimer_ = def_->attackCooldown;
        }
    } else if (std::fabs(body.vel.x) > 3.0f) {
        facingRight_ = body.vel.x > 0.0f;
    }
    moveBody(body, level.world(), dt);
}

void Enemy::tickBoss(Level& level, Vec2 toPlayer, bool sees) {
    const float dt = kTickDt;
    Rng& rng = level.rng();
    Player& pl = level.player();
    const float hpFrac = hp / maxHp;
    int newPhase = hpFrac > 0.66f ? 0 : (hpFrac > 0.33f ? 1 : 2);
    if (newPhase != phase_) {
        phase_ = newPhase;
        level.audio().play(Sfx::Roar, 1.0f, 0.8f + 0.1f * static_cast<float>(phase_));
        level.addShake(0.7f);
        level.addFlash(body.pos, 0xffd070, 4.0f, 200.0f, 0.8f);
        attackTimer_ = 1.0f;
    }
    if (!aggro_) {
        body.vel = damp(body.vel, {0.0f, std::sin(animTime_) * 10.0f}, 2.0f, dt);
        return;
    }
    // Hover above the alchemist, weaving side to side.
    Vec2 anchor = pl.body.pos + Vec2{std::sin(animTime_ * (0.5f + 0.15f * static_cast<float>(phase_))) * 90.0f, -70.0f};
    Vec2 to = anchor - body.pos;
    float speed = def_->speed * (1.0f + 0.35f * static_cast<float>(phase_));
    body.vel = damp(body.vel, to.normalized() * std::min(speed, to.length() * 2.0f), 2.0f, dt);
    if (body.hitWall) body.vel.x *= -0.5f;
    if (body.hitCeiling || body.onGround) body.vel.y *= -0.5f;

    if (attackTimer_ > 0.0f || !pl.alive()) return;
    const float dmgMul = 1.0f;
    int pattern = rng.rangeInt(0, phase_ + 1);
    switch (pattern) {
    case 0: {
        // Fan of fireballs.
        ProjectileLaunch l;
        l.spell = SpellId::Fireball;
        l.def = spellDef(SpellId::Fireball).proj;
        l.def.speed *= 0.7f;
        float base = angleOf(toPlayer);
        for (int i = -2; i <= 2; ++i) level.launch(l, body.pos, base + i * 0.22f, team, id, dmgMul);
        level.audio().play(Sfx::CastFire, 0.9f, 0.7f);
        attackTimer_ = 2.2f - 0.3f * static_cast<float>(phase_);
        break;
    }
    case 1: {
        // Acid rain and summoned imps.
        for (int i = 0; i < 6; ++i)
            level.spawnBlob(pl.body.pos + Vec2{rng.range(-60.0f, 60.0f), -90.0f}, mat::Acid, 14, {0.0f, 40.0f});
        int imps = 0;
        for (auto& e : level.enemies())
            if (e->alive() && e->type() == EnemyType::Imp) ++imps;
        if (imps < 3) level.spawnEnemy(EnemyType::Imp, body.pos + Vec2{rng.range(-20.0f, 20.0f), 10.0f});
        level.audio().play(Sfx::Roar, 0.6f, 1.3f);
        attackTimer_ = 2.8f - 0.3f * static_cast<float>(phase_);
        break;
    }
    case 2: {
        // Radial burst of seeking stars.
        ProjectileLaunch l;
        l.spell = SpellId::StarShower;
        l.def = spellDef(SpellId::StarShower).proj;
        l.def.speed *= 0.6f;
        l.def.lifetime = 2.4f;
        for (int i = 0; i < 10; ++i) level.launch(l, body.pos, kTau * static_cast<float>(i) / 10.0f, team, id, dmgMul);
        level.audio().play(Sfx::CastMagic, 0.9f, 0.6f);
        attackTimer_ = 2.6f;
        break;
    }
    default: {
        // Lightning strikes around the player and lava drops.
        ProjectileLaunch l;
        l.spell = SpellId::Lightning;
        l.def = spellDef(SpellId::Lightning).proj;
        l.def.damage = 18.0f;
        for (int i = 0; i < 3; ++i) {
            Vec2 from = pl.body.pos + Vec2{rng.range(-50.0f, 50.0f), -120.0f};
            level.launch(l, from, kPi * 0.5f + rng.range(-0.2f, 0.2f), team, id, dmgMul);
        }
        level.spawnBlob(pl.body.pos + Vec2{rng.range(-40.0f, 40.0f), -100.0f}, mat::Lava, 10, {0.0f, 30.0f});
        attackTimer_ = 3.0f;
        break;
    }
    }
    (void)sees;
}

void Enemy::onDamaged(Level& level, const DamageInfo& info, float dealt) {
    // Provoked without seeing the player: head for the shooter, or hold position when the harm
    // came from the world (burning, lava, a stray blast).
    if (!aggro_) lastSeen_ = info.source == Team::Player ? level.player().body.pos : body.pos;
    aggro_ = true;
    lostSight_ = 0.0f;
    if (team == Team::Monster) level.run().stats.damageDealt += dealt;
    if (info.silent) {
        if (level.rng().chance(0.05f)) level.bleed(body.pos, def_->blood, 1, {});
        return;
    }
    level.bleed(body.pos, def_->blood, std::min(10, 2 + static_cast<int>(dealt / 4.0f)), info.knockback);
    level.audio().playAt(Sfx::EnemyHurt, body.pos, 0.45f, level.rng().range(0.85f, 1.2f));
    std::string text = std::to_string(static_cast<int>(std::ceil(dealt)));
    level.addFloatText(body.pos + Vec2{level.rng().range(-3.0f, 3.0f), -body.halfH - 4.0f}, info.crit ? text + "!" : text,
                       info.crit ? 0xffe040 : 0xffffff, info.crit ? 1.3f : 1.0f);
    if (flying || def_->behaviour == Behaviour::Burrower) body.vel += info.knockback * 0.5f;
}

void Enemy::onKilled(Level& level, const DamageInfo&) {
    if (type_ == EnemyType::Beetle) {
        ExplosionSpec s;
        s.radius = 13.0f;
        s.damage = 32.0f;
        s.power = 120;
        s.incendiary = true;
        s.team = Team::Monster;
        s.hurtsOwner = true;
        s.cause = def_->name;
        level.explode(body.pos, s);
    }
    if (type_ == EnemyType::Boss) {
        for (int i = 0; i < 6; ++i) {
            ExplosionSpec s;
            s.radius = 14.0f + static_cast<float>(i) * 2.0f;
            s.damage = 0.0f;
            s.power = 0;
            s.team = Team::Player;
            s.hurtsOwner = false;
            level.explode(body.pos + level.rng().inCircle(20.0f), s);
        }
        level.spawnBlob(body.pos, mat::GoldDust, 400, {0.0f, -80.0f});
        level.addFloatText(body.pos - Vec2{0.0f, 20.0f}, "The Great Work is complete", 0xffe080, 1.8f);
    }
    for (const Vec2& s : segments) level.bleed(s, def_->blood, 8, {0.0f, -30.0f});
    level.onEnemyKilled(*this);
}

int Enemy::animFrame(float time) const {
    switch (def_->behaviour) {
    case Behaviour::Hopper:
        if (!body.onGround) return 2;
        return special_ > 0.0f ? 1 : 0;
    case Behaviour::Flyer: return static_cast<int>(time * 12.0f) % 2;
    case Behaviour::Walker:
    case Behaviour::Brute:
        if (special_ > 0.0f && def_->behaviour == Behaviour::Walker) return 2;
        return std::fabs(body.vel.x) > 4.0f ? static_cast<int>(time * 7.0f) % 2 : 0;
    case Behaviour::Turret: return attackTimer_ < 0.4f && aggro_ ? 1 : 0;
    case Behaviour::Spore: return special_ > 0.0f ? 1 : 0;
    default: return static_cast<int>(time * 6.0f) % 2;
    }
}

void Enemy::drawAlbedo(DrawContext& ctx) const {
    const Vec2 p = body.interpolated(ctx.alpha);
    Color tint = WHITE;
    if (type_ == EnemyType::FrostSlime) tint = Color{150, 210, 255, 255};
    if (type_ == EnemyType::Sapper) tint = Color{255, 220, 160, 255};
    if (status.chilled > 0.0f) tint = Color{static_cast<unsigned char>(tint.r * 0.7f), static_cast<unsigned char>(tint.g * 0.85f), 255, 255};

    if (!segments.empty()) {
        const SpriteInfo& bi = ctx.sprites.info(SpriteId::WormBody);
        for (int i = static_cast<int>(segments.size()) - 1; i >= 0; --i) {
            const Vec2& s = segments[static_cast<size_t>(i)];
            Vector2 lp = ctx.scene.layerPos({s.x - bi.w * 0.5f, s.y - bi.h * 0.5f});
            ctx.sprites.draw(SpriteId::WormBody, 0, lp, false, tint);
            if (hurtFlash > 0.0f) ctx.sprites.drawSilhouette(SpriteId::WormBody, 0, lp, false, Color{255, 255, 255, 180});
        }
    }
    if (type_ == EnemyType::Boss) {
        drawBoss(ctx, p, tint);
        return;
    }
    const SpriteInfo& info = ctx.sprites.info(def_->sprite);
    const int frame = animFrame(ctx.time);
    float top = flying || !segments.empty() ? p.y - info.h * 0.5f : p.y + body.halfH - static_cast<float>(info.h);
    Vector2 lp = ctx.scene.layerPos({p.x - info.w * 0.5f, top});
    const bool flip = !facingRight_;
    ctx.sprites.draw(def_->sprite, frame, lp, flip, tint);
    if (elite_) {
        const unsigned char a = static_cast<unsigned char>(60.0f + 40.0f * std::sin(ctx.time * 5.0f));
        ctx.sprites.drawSilhouette(def_->sprite, frame, lp, flip, Color{255, 200, 80, a});
    }
    if (windup_ > 0.0f) {
        // Telegraph: the creature brightens as its attack charges.
        const Color c = def_->glow ? hexColor(def_->glow) : Color{255, 240, 200, 255};
        const float charge = 1.0f - windup_ / 0.4f;
        ctx.sprites.drawSilhouette(def_->sprite, frame, lp, flip, Color{c.r, c.g, c.b, static_cast<unsigned char>(60.0f + 150.0f * charge)});
    }
    if (hurtFlash > 0.0f) ctx.sprites.drawSilhouette(def_->sprite, frame, lp, flip, Color{255, 255, 255, 210});
    else if (status.burning > 0.0f) ctx.sprites.drawSilhouette(def_->sprite, frame, lp, flip, Color{255, 120, 30, 100});
    else if (status.shocked > 0.0f) ctx.sprites.drawSilhouette(def_->sprite, frame, lp, flip, Color{200, 220, 255, 150});

}

void Enemy::drawBoss(DrawContext& ctx, Vec2 p, Color tint) const {
    constexpr int kScale = 2;
    const SpriteInfo& info = ctx.sprites.info(SpriteId::Boss);
    // A slow bob makes the giant feel alive.
    Vec2 topLeft{p.x - info.w * kScale * 0.5f, p.y - info.h * kScale * 0.5f + std::sin(ctx.time * 1.7f) * 1.5f};
    Vector2 lp = ctx.scene.layerPos(topLeft);
    ctx.sprites.drawScaled(SpriteId::Boss, 0, lp, kScale, false, tint);
    if (hurtFlash > 0.0f) ctx.sprites.drawScaled(SpriteId::Boss, 0, lp, kScale, false, Color{255, 255, 255, 45}, false, true);
    // The philosopher's stone pulses at the Rebis' heart.
    const SpriteInfo& ci = ctx.sprites.info(SpriteId::BossCore);
    Vector2 cp = ctx.scene.layerPos({p.x - ci.w * 0.5f, topLeft.y + info.h * kScale * 0.5f - ci.h * 0.5f + 2.0f});
    ctx.sprites.draw(SpriteId::BossCore, 0, cp, false);
}

void Enemy::drawEmission(DrawContext& ctx) const {
    const Vec2 p = body.interpolated(ctx.alpha);
    if (type_ == EnemyType::Boss) {
        constexpr int kScale = 2;
        const SpriteInfo& info = ctx.sprites.info(SpriteId::Boss);
        Vec2 topLeft{p.x - info.w * kScale * 0.5f, p.y - info.h * kScale * 0.5f + std::sin(ctx.time * 1.7f) * 1.5f};
        ctx.sprites.drawScaled(SpriteId::Boss, 0, ctx.scene.layerPos(topLeft), kScale, false, WHITE, true);
        const SpriteInfo& ci = ctx.sprites.info(SpriteId::BossCore);
        Vector2 cp = ctx.scene.layerPos({p.x - ci.w * 0.5f, topLeft.y + info.h * kScale * 0.5f - ci.h * 0.5f + 2.0f});
        float pulse = 0.6f + 0.4f * std::sin(ctx.time * 4.0f);
        ctx.sprites.drawEmissive(SpriteId::BossCore, 0, cp, false, Color{255, 255, 255, static_cast<unsigned char>(255 * pulse)});
        return;
    }
    const SpriteInfo& info = ctx.sprites.info(def_->sprite);
    float top = flying || !segments.empty() ? p.y - info.h * 0.5f : p.y + body.halfH - static_cast<float>(info.h);
    Vector2 lp = ctx.scene.layerPos({p.x - info.w * 0.5f, top});
    ctx.sprites.drawEmissive(def_->sprite, animFrame(ctx.time), lp, !facingRight_);
}

void Enemy::addLights(DrawContext& ctx) const {
    const Vec2 p = body.interpolated(ctx.alpha);
    if (elite_) ctx.scene.addLight(PointLight{p, 1.4f, 1.0f, 0.35f, 46.0f, 4.0f});
    if (windup_ > 0.0f) {
        const Color c = def_->glow ? hexColor(def_->glow) : Color{255, 240, 200, 255};
        const float k = 0.4f + 1.4f * (1.0f - windup_ / 0.4f);
        ctx.scene.addLight(PointLight{p, c.r / 255.0f * k, c.g / 255.0f * k, c.b / 255.0f * k, 34.0f, 2.0f});
    }
    if (def_->glow == 0) return;
    Color c = hexColor(def_->glow);
    if (type_ == EnemyType::Boss) {
        // The Rebis lights its arena from above its heads, so its own body is not blown out.
        ctx.scene.addLight(PointLight{p - Vec2{0.0f, 44.0f}, c.r / 255.0f * 0.95f, c.g / 255.0f * 0.95f, c.b / 255.0f * 0.95f, 170.0f, 0.0f});
        return;
    }
    constexpr float k = 0.9f;
    ctx.scene.addLight(PointLight{p, c.r / 255.0f * k, c.g / 255.0f * k, c.b / 255.0f * k, 40.0f, 4.0f});
}

} // namespace pm
