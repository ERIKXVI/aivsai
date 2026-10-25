#include "game/Player.hpp"

#include <cmath>

#include "core/Timing.hpp"
#include "game/Level.hpp"
#include "render/ColorUtil.hpp"
#include "render/SceneRenderer.hpp"
#include "render/Sprites.hpp"

namespace pm {

namespace {

constexpr float kRunSpeed = 72.0f;
constexpr float kGroundAccel = 950.0f;
constexpr float kGroundFriction = 1300.0f;
constexpr float kAirAccel = 560.0f;
constexpr float kJumpSpeed = 158.0f;
constexpr float kLevitateSpeed = 82.0f;
constexpr float kLevitateAccel = 1500.0f;
constexpr float kCoyoteTime = 0.1f;
constexpr float kJumpBufferTime = 0.12f;
constexpr float kMaxFallSpeed = 420.0f;
constexpr int kSprayPerTick = 7;
constexpr int kAbsorbPerTick = 30;
constexpr int kDrinkAmount = 100;
// Elixirs: a full draught's duration and strength.
constexpr float kHasteTime = 15.0f;
constexpr float kHasteSpeed = 1.4f;
constexpr float kFuryTime = 12.0f;
constexpr float kFuryDamage = 1.5f;
constexpr float kCastFlashTime = 0.08f; ///< the gem flashes white and lights up this long after a cast
constexpr Vec2 kHandOffset{1.5f, 0.5f}; ///< the wand's grip relative to the body centre, facing right

Vec2 handPos(Vec2 bodyPos, bool facingRight) {
    return {bodyPos.x + (facingRight ? kHandOffset.x : -kHandOffset.x), bodyPos.y + kHandOffset.y};
}

struct Pose {
    SpriteId sprite;
    int frame;
};

/// The alchemist's sprite and frame; shared by the albedo and emission passes so the glowing
/// eyes always sit on the frame drawn.
Pose currentPose(const Body& body, bool jumpHeld, float animTime) {
    if (!body.onGround) return {SpriteId::PlayerAir, (jumpHeld && body.vel.y > -40.0f) ? static_cast<int>(animTime * 12.0f) % 2 : 0};
    if (std::fabs(body.vel.x) > 8.0f) return {SpriteId::PlayerRun, static_cast<int>(animTime * 11.0f) % 4};
    return {SpriteId::PlayerIdle, static_cast<int>(animTime / 0.6f) % 2};
}

} // namespace

Player::Player(Vec2 pos, RunState& run) : run_(run) {
    body.pos = body.prevPos = pos;
    body.halfW = 3.0f;
    body.halfH = 6.0f;
    body.stepHeight = 3.0f;
    team = Team::Player;
    hp = run.hp;
    refreshFromRun();
    invulnerable = 1.0f; // grace period on arrival
}

void Player::refreshFromRun() {
    maxHp = run_.maxHp;
    hp = std::min(hp, maxHp);
    resist.fill(1.0f);
    if (run_.hasPerk(PerkId::Fireproof)) resist[static_cast<size_t>(DamageType::Fire)] = 0.0f;
    if (run_.hasPerk(PerkId::BlastShield)) resist[static_cast<size_t>(DamageType::Explosion)] = 0.0f;
    if (run_.hasPerk(PerkId::ToxicBlood)) {
        resist[static_cast<size_t>(DamageType::Acid)] = 0.0f;
        resist[static_cast<size_t>(DamageType::Toxic)] = 0.0f;
    }
    if (run_.hasPerk(PerkId::Insulated)) resist[static_cast<size_t>(DamageType::Electric)] = 0.0f;
    for (float& r : resist) r *= run_.damageTakenMul();
}

float Player::damageMul() const { return run_.damageDealtMul() * (status.fury > 0.0f ? kFuryDamage : 1.0f); }

Vec2 Player::wandTip() const {
    const Wand* w = run_.currentWand();
    const float len = w ? static_cast<float>(w->length) : 8.0f;
    return handPos(body.pos, facingRight_) + aimDir_ * len;
}

void Player::tick(Level& level) {
    body.prevPos = body.pos;
    const float dt = kTickDt;
    animTime_ += dt;
    castFlash_ = std::max(0.0f, castFlash_ - dt);

    // Never leave the alchemist entombed: if no free spot is near, dig one out.
    if (!unbury(body, level.world())) level.dig(body.pos, std::max(body.halfW, body.halfH) + 1.0f, 254, false);
    Environment env = sampleEnvironment(body, level.world());
    if (run_.hasPerk(PerkId::PhoenixStep)) {
        // The phoenix is healed by ordinary flames and never set alight; lava still burns.
        if (env.fire) hp = std::min(maxHp, hp + 6.0f * dt);
        if (env.contactMat == mat::Fire) env.contactDps = 0.0f;
        if (env.contactMat != mat::Lava) env.fire = false;
        status.burning = 0.0f;
    }
    updateStatus(level, env);
    if (dead) return;

    Vec2 toAim = input_.aim - body.pos;
    if (toAim.lengthSq() > 1.0f) aimDir_ = toAim.normalized();
    facingRight_ = aimDir_.x >= 0.0f;

    move(level, env);
    handleWands(level);
    handleFlask(level);
    collectGold(level);

    // Drowning.
    const bool headUnder = env.submerged > 0.85f;
    if (headUnder && !run_.hasPerk(PerkId::Gills)) {
        breath_ = std::max(0.0f, breath_ - dt / 9.0f);
        if (breath_ <= 0.0f) {
            drownTimer_ += dt;
            applyDamage(level, {12.0f * dt, DamageType::Physical, {}, Team::Monster, false, true, "drowning"});
        }
    } else {
        breath_ = std::min(1.0f, breath_ + dt / 2.0f);
    }
    if (env.submerged > 0.3f && !wasInLiquid_ && body.vel.y > 60.0f) level.audio().playAt(Sfx::Splash, body.pos, 0.6f);
    wasInLiquid_ = env.submerged > 0.3f;

    if (run_.hasPerk(PerkId::PhoenixStep) && body.onGround && std::fabs(body.vel.x) > 20.0f && level.rng().chance(0.25f)) {
        // Flames are left behind the alchemist, outside the body.
        const int fx = body.vel.x > 0.0f ? floorToInt(body.pos.x - body.halfW) - 2 : floorToInt(body.pos.x + body.halfW) + 1;
        level.world().placeIfEmpty(fx, floorToInt(body.pos.y + body.halfH - 1.0f), mat::Fire);
    }
    // Elixirs show on the body: quicksilver streaks off a hasted alchemist, embers rise from a furious one.
    Rng& rng = level.rng();
    if (status.hasted > 0.0f && std::fabs(body.vel.x) > 30.0f && rng.chance(0.5f)) {
        const float back = body.vel.x > 0.0f ? -body.halfW : body.halfW;
        level.particles().addVisual({body.pos.x + back, body.pos.y + rng.range(-body.halfH, body.halfH)},
                                    {-body.vel.x * 0.3f, rng.range(-6.0f, 6.0f)}, {220, 228, 240, 200}, {120, 130, 150, 120},
                                    rng.range(0.15f, 0.3f), 0.0f);
    }
    if (status.fury > 0.0f && rng.chance(0.35f)) {
        level.particles().addVisual({body.pos.x + rng.range(-body.halfW, body.halfW), body.pos.y + rng.range(-body.halfH, body.halfH)},
                                    {rng.range(-8.0f, 8.0f), rng.range(-40.0f, -20.0f)}, {255, 170, 60, 230}, {255, 110, 30, 220},
                                    rng.range(0.3f, 0.6f), 0.0f);
    }

    run_.hp = hp;
    run_.maxHp = maxHp;
}

void Player::move(Level& level, const Environment& env) {
    const float dt = kTickDt;
    const bool swimming = env.submerged > 0.35f;
    float speedMul = run_.moveSpeedMul();
    if (env.sticky) speedMul *= 0.45f;
    if (status.chilled > 0.0f) speedMul *= 0.6f;
    if (status.hasted > 0.0f) speedMul *= kHasteSpeed;
    if (swimming) speedMul *= run_.hasPerk(PerkId::Gills) ? 1.0f : 0.7f;

    // Horizontal control.
    const float target = input_.moveX * kRunSpeed * speedMul;
    const bool grounded = body.onGround;
    if (grounded) coyote_ = kCoyoteTime;
    else coyote_ = std::max(0.0f, coyote_ - dt);
    float accel = grounded ? (std::fabs(target) > 1.0f ? kGroundAccel : kGroundFriction) : kAirAccel;
    body.vel.x = approach(body.vel.x, target, accel * dt);

    // Jumping with buffer and coyote time; holding the button afterwards levitates.
    if (input_.jumpPressed) jumpBuffer_ = kJumpBufferTime;
    else jumpBuffer_ = std::max(0.0f, jumpBuffer_ - dt);
    if (!input_.jumpHeld) jumpedThisPress_ = false;

    if (jumpBuffer_ > 0.0f && coyote_ > 0.0f && !swimming) {
        body.vel.y = -kJumpSpeed;
        coyote_ = 0.0f;
        jumpBuffer_ = 0.0f;
        jumpedThisPress_ = true;
        level.audio().play(Sfx::Jump, 0.5f, level.rng().range(0.95f, 1.05f));
    }

    const float levMax = std::max(0.1f, run_.levitationMax);
    if (grounded || swimming) {
        levitation_ = std::min(1.0f, levitation_ + dt * 1.6f);
    }
    if (swimming) {
        body.vel.y -= kGravity * 0.92f * dt; // buoyancy
        body.vel *= 1.0f / (1.0f + 3.0f * dt);
        if (input_.jumpHeld) body.vel.y = approach(body.vel.y, -70.0f * speedMul, 600.0f * dt);
        if (input_.downHeld) body.vel.y = approach(body.vel.y, 60.0f, 600.0f * dt);
    } else if (!grounded && input_.jumpHeld) {
        // Variable jump: keep rising on the initial press, then levitate once falling.
        bool rising = body.vel.y < 0.0f && jumpedThisPress_;
        if (!rising && levitation_ > 0.0f) {
            body.vel.y = approach(body.vel.y, -kLevitateSpeed, kLevitateAccel * dt);
            levitation_ = std::max(0.0f, levitation_ - dt / levMax);
            Rng& rng = level.rng();
            if (rng.chance(0.7f)) {
                level.particles().addVisual({body.pos.x + rng.range(-2.0f, 2.0f), body.pos.y + body.halfH},
                                            {rng.range(-15.0f, 15.0f), rng.range(30.0f, 70.0f)}, {170, 150, 255, 220},
                                            {90, 70, 200, 255}, rng.range(0.2f, 0.4f), 0.0f);
            }
        }
    } else if (!grounded && !input_.jumpHeld && body.vel.y < 0.0f && jumpedThisPress_ == false) {
        // Released early: cut the jump short for precise hops.
        body.vel.y *= 0.88f;
    }

    if (body.vel.y > kMaxFallSpeed) body.vel.y = kMaxFallSpeed;
    lastFallSpeed_ = body.vel.y;
    moveBody(body, level.world(), dt);
    if (body.onGround && !grounded && lastFallSpeed_ > 150.0f) {
        level.audio().play(Sfx::Land, std::min(1.0f, lastFallSpeed_ / 400.0f));
        for (int i = 0; i < 4; ++i) {
            Rng& rng = level.rng();
            level.particles().addVisual({body.pos.x + rng.range(-3.0f, 3.0f), body.pos.y + body.halfH - 0.5f},
                                        {rng.range(-40.0f, 40.0f), rng.range(-30.0f, -5.0f)}, {120, 110, 100, 200},
                                        {0, 0, 0, 0}, 0.35f, 0.8f);
        }
    }
}

void Player::handleWands(Level& level) {
    if (run_.wands.empty()) return;
    const int count = static_cast<int>(run_.wands.size());
    if (input_.selectWand >= 0 && input_.selectWand < count) run_.activeWand = input_.selectWand;
    if (input_.cycleWand != 0) run_.activeWand = positiveMod(run_.activeWand + input_.cycleWand, count);
    run_.activeWand = clamp(run_.activeWand, 0, count - 1);

    const float regen = run_.manaRegenMul();
    for (Wand& w : run_.wands) {
        w.tick(kTickDt);
        if (regen > 1.0f) w.mana = std::min(w.manaMax, w.mana + w.manaCharge * (regen - 1.0f) * kTickDt);
    }

    Wand& wand = *run_.currentWand(); // there is a wand: the count was checked and the index clamped above
    if (!input_.castHeld) return;
    const bool wasReady = wand.cooldown <= 0.0f;
    CastOutcome outcome = wand.tryCast(level.rng());
    if (wasReady && wand.cooldown > 0.0f) {
        // Perpetual Motion shortens every delay and recharge the press started, including a
        // recharge after a modifier-only tail or a skipped unaffordable group.
        wand.cooldown *= run_.castSpeedMul();
        wand.cooldownTotal = wand.cooldown; // keeps the HUD reload bar honest
    }
    if (outcome.status == CastStatus::NoMana) {
        if (level.ticks() % 20 == 0) level.audio().play(Sfx::UiDeny, 0.25f);
        return;
    }
    if (outcome.status != CastStatus::Cast) return;
    float angle = angleOf(aimDir_);
    level.castShot(outcome.shot, wandTip(), angle, Team::Player, id, damageMul());
    run_.stats.spellsCast++;
    castFlash_ = kCastFlashTime;

    // Sound keyed to the strongest projectile in the cast.
    Sfx sfx = Sfx::CastSpark;
    float heaviest = 0.0f;
    for (const ProjectileLaunch& l : outcome.shot.projectiles) {
        float weight = l.def.explosionDamage + l.def.damage;
        if (weight < heaviest) continue;
        heaviest = weight;
        const ProjectileDef& d = l.def;
        if (d.flags & projflag::kHitscan) sfx = Sfx::CastLightning;
        else if (d.visual == ProjVisual::Bomb || d.explosionRadius > 15.0f) sfx = Sfx::CastHeavy;
        else if (d.damageType == DamageType::Fire) sfx = Sfx::CastFire;
        else if (d.flags & projflag::kDigs) sfx = Sfx::Dig;
        else if (d.visual == ProjVisual::Orb || d.visual == ProjVisual::BigOrb || d.visual == ProjVisual::Star) sfx = Sfx::CastMagic;
        else if (d.visual == ProjVisual::Bolt || d.visual == ProjVisual::Shard) sfx = Sfx::CastBolt;
        else sfx = Sfx::CastSpark;
    }
    level.audio().play(sfx, 0.55f, level.rng().range(0.93f, 1.07f));
    recoil_ -= aimDir_ * std::min(1.5f, 0.15f + heaviest * 0.02f);
}

void Player::handleFlask(Level& level) {
    Flask& flask = run_.flask;
    World& world = level.world();
    Rng& rng = level.rng();

    if (input_.absorbHeld) {
        // Suck matter from around the cursor into the flask, one material at a time.
        Vec2 c = input_.aim;
        if ((c - body.pos).length() > 70.0f) c = body.pos + (c - body.pos).normalized() * 70.0f;
        int taken = 0;
        bool otherMatter = false; // something absorbable, but not what the flask holds
        const int r = 6;
        for (int dy = -r; dy <= r && taken < kAbsorbPerTick; ++dy) {
            for (int dx = -r; dx <= r && taken < kAbsorbPerTick; ++dx) {
                if (dx * dx + dy * dy > r * r || flask.amount >= Flask::kCapacity) continue;
                int x = floorToInt(c.x) + dx, y = floorToInt(c.y) + dy;
                if (!world.inBounds(x, y)) continue;
                MatId m = world.at(x, y).mat;
                Phase ph = phaseOf(m);
                if (ph != Phase::Liquid && ph != Phase::Powder) continue;
                if (!flask.empty() && flask.mat != m) {
                    otherMatter = true;
                    continue;
                }
                flask.mat = m;
                ++flask.amount;
                ++taken;
                world.clear(x, y);
                if (rng.chance(0.3f)) {
                    Vec2 from{static_cast<float>(x), static_cast<float>(y)};
                    Vec2 v = (wandTip() - from).normalized() * 160.0f;
                    Rgba col = cellColor(makeCell(m, rng.next()), x, y, 0);
                    level.particles().addVisual(from, v, col, {0, 0, 0, 0}, 0.25f, 0.0f, partflag::kFade);
                }
            }
        }
        if (taken > 0 && level.ticks() % 6 == 0) level.audio().play(Sfx::Potion, 0.25f, 1.4f);
        // The flask holds one kind of matter at a time: say so instead of silently refusing.
        if (taken == 0 && otherMatter && level.ticks() >= nextFlaskNotice_) {
            nextFlaskNotice_ = level.ticks() + 90;
            level.addFloatText(c - Vec2{0.0f, 8.0f}, std::string("Flask holds ") + matDef(flask.mat).name + " - pour it out first", 0xd0c8ff);
            level.audio().play(Sfx::UiDeny, 0.5f);
        }
    } else if (input_.sprayHeld && !flask.empty()) {
        for (int i = 0; i < kSprayPerTick && flask.amount > 0; ++i) {
            float a = angleOf(aimDir_) + rng.range(-0.12f, 0.12f);
            Cell cell = makeCell(flask.mat, rng.next());
            cell.flags |= cellflag::kPlayerMade;
            level.particles().addMatter(wandTip(), fromAngle(a, rng.range(170.0f, 240.0f)), cell);
            --flask.amount;
        }
        if (flask.amount <= 0) flask = Flask{};
        if (level.ticks() % 8 == 0) level.audio().play(Sfx::Splash, 0.2f, 1.6f);
    }

    if (input_.drinkPressed && !flask.empty()) {
        int amount = std::min(flask.amount, kDrinkAmount);
        flask.amount -= amount;
        MatId m = flask.mat;
        if (flask.amount <= 0) flask = Flask{};
        level.audio().play(Sfx::Drink, 0.7f);
        float fraction = static_cast<float>(amount) / kDrinkAmount;
        if (m == mat::Vitae && hp < maxHp) level.earnFeat(FeatId::Vitae); // before Iron Stomach tops them up
        if (run_.hasPerk(PerkId::IronStomach)) hp = std::min(maxHp, hp + 10.0f * fraction);
        if ((m == mat::Water || m == mat::Brine) && status.burning > 0.0f) level.earnFeat(FeatId::Doused);
        switch (m) {
        case mat::Vitae:
            hp = std::min(maxHp, hp + 40.0f * fraction);
            level.addFloatText(body.pos, "+" + std::to_string(static_cast<int>(40.0f * fraction)), 0xff7ab8);
            break;
        case mat::Water:
        case mat::Brine:
            status.burning = 0.0f;
            status.wet = 8.0f;
            break;
        case mat::Oil: status.oiled = 10.0f; break;
        case mat::Lava:
            status.burning = 6.0f;
            applyDamage(level, {30.0f, DamageType::Fire, {}, Team::Monster, false, false, "a draught of lava"});
            break;
        case mat::Acid: applyDamage(level, {25.0f * fraction, DamageType::Acid, {}, Team::Monster, false, false, "a draught of acid"}); break;
        case mat::Toxic: status.poisoned = 8.0f; break;
        case mat::Blood: hp = std::min(maxHp, hp + 4.0f * fraction); break;
        case mat::Midas: {
            // Paid by the amount drunk, so sipping a flask in tiny gulps earns nothing extra.
            const int gold = static_cast<int>(std::lround(60.0f * fraction));
            if (gold > 0) {
                run_.gold += gold;
                level.addFloatText(body.pos, "+" + std::to_string(gold) + " gold", 0xffd040);
            }
            break;
        }
        case mat::Quicksilver:
            status.chilled = 0.0f;
            status.hasted = std::max(status.hasted, kHasteTime * fraction);
            level.addFloatText(body.pos, "Haste!", 0xdce4ec);
            break;
        case mat::Ichor:
            status.burning = 0.0f;
            status.fury = std::max(status.fury, kFuryTime * fraction);
            level.addFloatText(body.pos, "Fury!", 0xffb040);
            break;
        default: break;
        }
    }
}

void Player::collectGold(Level& level) {
    World& world = level.world();
    const bool magnet = run_.hasPerk(PerkId::MidasGreed);
    const int reach = magnet ? 2 : 1;
    int collected = 0;
    const int x0 = floorToInt(body.pos.x - body.halfW) - reach, x1 = floorToInt(body.pos.x + body.halfW) + reach;
    const int y0 = floorToInt(body.pos.y - body.halfH) - reach, y1 = floorToInt(body.pos.y + body.halfH) + reach;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (world.inBounds(x, y) && (matDef(world.at(x, y).mat).flags & matflag::kCollectible)) {
                world.clear(x, y);
                ++collected;
            }
    // Midas' Greed pulls nearby gold dust towards the player.
    if (magnet && level.ticks() % 3 == 0) {
        const int r = 30;
        for (int i = 0; i < 60; ++i) {
            int x = floorToInt(body.pos.x) + level.rng().rangeInt(-r, r);
            int y = floorToInt(body.pos.y) + level.rng().rangeInt(-r, r);
            if (!world.inBounds(x, y) || world.at(x, y).mat != mat::GoldDust) continue;
            // Only grains with a clear line to the alchemist fly: none through rock or vault
            // walls, nor from inside a pile (the test starts just outside the grain itself).
            const Vec2 from{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
            const Vec2 to = body.pos - from;
            if (to.length() > 1.5f && !level.lineOfSight(from + to.normalized() * 1.5f, body.pos)) continue;
            world.clear(x, y);
            ++collected;
            // The grain is banked at once; a glint streaks to the alchemist to show it.
            level.particles().addVisual(from, to.normalized() * 220.0f, {255, 214, 90, 255}, {255, 190, 60, 200},
                                        std::max(kTickDt, to.length() / 220.0f), 0.0f, partflag::kFade | partflag::kNoGravity);
        }
    }
    if (collected > 0) {
        run_.gold += collected;
        run_.stats.goldCollected += collected;
        if (level.ticks() % 4 == 0) level.audio().play(Sfx::Coin, 0.35f, level.rng().range(0.95f, 1.15f));
    }
}

void Player::onDamaged(Level& level, const DamageInfo& info, float dealt) {
    run_.stats.damageTaken += dealt;
    if (info.silent) {
        // Damage over time (burning, poison, acid) only drips; a stream of blood every tick
        // would also douse the alchemist's own flames.
        if (level.rng().chance(0.05f)) level.bleed(body.pos, mat::Blood, 1, {});
        return;
    }
    level.bleed(body.pos, mat::Blood, std::min(12, static_cast<int>(dealt * 0.5f) + 1), info.knockback);
    invulnerable = 0.6f;
    if (dealt >= 18.0f) level.addHitStop(2);
    level.audio().play(Sfx::PlayerHurt, 0.7f, level.rng().range(0.95f, 1.05f));
    level.addShake(std::min(0.5f, dealt / 40.0f + 0.15f));
    level.addFloatText(body.pos + Vec2{0.0f, -8.0f}, std::to_string(static_cast<int>(std::ceil(dealt))), 0xff5050);
}

void Player::onKilled(Level& level, const DamageInfo&) {
    if (run_.hasPerk(PerkId::SecondLife)) {
        run_.perks[static_cast<size_t>(PerkId::SecondLife)] = 0;
        dead = false;
        hp = maxHp * 0.5f;
        invulnerable = 2.5f;
        status = StatusEffects{};
        level.addFloatText(body.pos + Vec2{0.0f, -12.0f}, "Second Life!", 0xfff0c0, 1.5f);
        level.audio().play(Sfx::Perk, 0.8f);
        level.addFlash(body.pos, 0xfff0c0, 3.0f, 120.0f, 0.8f);
        return;
    }
    level.onPlayerDeath();
}

void Player::drawAlbedo(DrawContext& ctx) const {
    const Vec2 p = body.interpolated(ctx.alpha);
    const auto [sprite, frame] = currentPose(body, input_.jumpHeld, animTime_);
    const SpriteInfo& info = ctx.sprites.info(sprite);
    Vector2 lp = ctx.scene.layerPos({p.x - info.w * 0.5f, p.y + body.halfH - static_cast<float>(info.h)});
    const bool blink = invulnerable > 0.0f && static_cast<int>(invulnerable * 20.0f) % 2 == 0;
    if (!blink) {
        ctx.sprites.draw(sprite, frame, lp, !facingRight_);
        if (hurtFlash > 0.0f) ctx.sprites.drawSilhouette(sprite, frame, lp, !facingRight_, Color{255, 255, 255, 200});
        else if (status.burning > 0.0f)
            ctx.sprites.drawSilhouette(sprite, frame, lp, !facingRight_, Color{255, 120, 30, 90});
        else if (status.fury > 0.0f) {
            const auto pulse = static_cast<unsigned char>(45.0f + 30.0f * std::sin(animTime_ * 9.0f));
            ctx.sprites.drawSilhouette(sprite, frame, lp, !facingRight_, Color{255, 80, 30, pulse});
        }
    }

    drawEssences(ctx, p);

    // The wand: a line of pixels from the hand towards the cursor, gem at the tip.
    if (const Wand* wand = run_.currentWand()) {
        const Wand& w = *wand;
        const Vec2 hand = handPos(p, facingRight_);
        for (int i = 0; i <= w.length; ++i) {
            Vec2 q = hand + aimDir_ * static_cast<float>(i);
            Vector2 lq = ctx.scene.layerPos(q);
            Color c = i >= w.length - 1 ? hexColor(w.gemColor) : hexColor(w.handleColor);
            DrawPixel(static_cast<int>(std::floor(lq.x)), static_cast<int>(std::floor(lq.y)), c);
        }
    }
}

void Player::drawEssences(DrawContext& ctx, Vec2 p) const {
    // Each guardian's essence gathered this run circles the alchemist (in both layers, so
    // the mote is seen and also glows).
    for (int i = 0; i < kGuardianCount; ++i) {
        if (!((run_.essences >> i) & 1u)) continue;
        const float a = animTime_ * 1.6f + static_cast<float>(i) * kTau / static_cast<float>(kGuardianCount);
        const Vector2 m = ctx.scene.layerPos(p + Vec2{std::cos(a) * 11.0f, std::sin(a) * 5.0f - 2.0f});
        const int x = static_cast<int>(std::floor(m.x)), y = static_cast<int>(std::floor(m.y));
        const Color c = hexColor(essenceColor(i));
        const Color halo{c.r, c.g, c.b, 110};
        DrawPixel(x, y, c);
        DrawPixel(x - 1, y, halo);
        DrawPixel(x + 1, y, halo);
        DrawPixel(x, y - 1, halo);
        DrawPixel(x, y + 1, halo);
    }
}

void Player::drawEmission(DrawContext& ctx) const {
    const Vec2 p = body.interpolated(ctx.alpha);
    const auto [sprite, frame] = currentPose(body, input_.jumpHeld, animTime_);
    const SpriteInfo& info = ctx.sprites.info(sprite);
    Vector2 lp = ctx.scene.layerPos({p.x - info.w * 0.5f, p.y + body.halfH - static_cast<float>(info.h)});
    ctx.sprites.drawEmissive(sprite, frame, lp, !facingRight_);
    drawEssences(ctx, p);
    if (const Wand* wand = run_.currentWand()) {
        const Wand& w = *wand;
        Vec2 tip = handPos(p, facingRight_) + aimDir_ * static_cast<float>(w.length);
        Vector2 lt = ctx.scene.layerPos(tip);
        Color g = hexColor(w.gemColor);
        if (castFlash_ > 0.0f) g = WHITE;
        DrawPixel(static_cast<int>(std::floor(lt.x)), static_cast<int>(std::floor(lt.y)), g);
    }
}

void Player::addLights(DrawContext& ctx) const {
    const Vec2 p = body.interpolated(ctx.alpha);
    ctx.scene.addLight(PointLight{{p.x, p.y - 3.0f}, 2.6f, 2.2f, 1.75f, 150.0f, 3.0f, 100.0f}); // always gets a slot
    if (status.fury > 0.0f) ctx.scene.addLight(PointLight{p, 1.6f, 0.55f, 0.15f, 28.0f, 1.0f, 40.0f});
    if (const Wand* w = run_.currentWand(); castFlash_ > 0.0f && w) {
        Color g = hexColor(w->gemColor);
        float k = castFlash_ / kCastFlashTime * 2.5f;
        ctx.scene.addLight(PointLight{wandTip(), g.r / 255.0f * k, g.g / 255.0f * k, g.b / 255.0f * k, 45.0f, 1.0f, 2.0f});
    }
}

} // namespace pm
