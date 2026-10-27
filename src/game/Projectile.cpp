#include "game/Projectile.hpp"

#include <cmath>

#include "game/Level.hpp"
#include "render/SceneRenderer.hpp"

namespace pm {

namespace {

using namespace projflag;

/// What to blame for a projectile's damage: the creature that cast it, else the spell.
const char* projectileCause(const Level& level, const Projectile& p) {
    if (p.team == Team::Monster)
        if (const char* name = level.actorName(p.ownerId)) return name;
    return p.spell == SpellId::None ? nullptr : spellDef(p.spell).name;
}

Rgba scaled(Rgba c, float k) {
    return {static_cast<uint8_t>(std::min(255.0f, c.r * k)), static_cast<uint8_t>(std::min(255.0f, c.g * k)),
            static_cast<uint8_t>(std::min(255.0f, c.b * k)), c.a};
}

void impact(Level& level, Projectile& p, Vec2 at) {
    const ProjectileDef& d = p.def;
    Rng& rng = level.rng();
    const bool fiery = (d.flags & kIncendiary) || d.damageType == DamageType::Fire;
    if (d.explosionRadius > 0.0f) {
        ExplosionSpec e;
        e.radius = d.explosionRadius;
        e.damage = d.explosionDamage * p.damageMul;
        e.power = static_cast<uint8_t>(std::min(215.0f, 70.0f + d.explosionRadius * 6.0f));
        e.incendiary = fiery;
        e.team = p.team;
        e.knockback = 120.0f + d.explosionRadius * 9.0f;
        e.cause = projectileCause(level, p);
        level.explode(at, e);
    }
    if (d.spawnMat != mat::Empty && d.spawnAmount > 0) level.spawnBlob(at, d.spawnMat, d.spawnAmount, p.vel * -0.15f);
    if (fiery && d.explosionRadius <= 0.0f) {
        for (int i = 0; i < 6; ++i)
            level.world().placeIfEmpty(floorToInt(at.x) + rng.rangeInt(-2, 2), floorToInt(at.y) + rng.rangeInt(-2, 2), mat::Fire);
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) level.world().ignite(floorToInt(at.x) + dx, floorToInt(at.y) + dy);
    }
    if (d.flags & kFreezing) level.freezeArea(at, 7.0f);
    if (d.flags & kElectric) level.electrify(at, d.damage * p.damageMul * 0.8f, p.team);
    if (d.flags & kTransmute) level.transmuteArea(at, 6.0f);
    if ((d.flags & kTeleport) && p.team == Team::Player && level.player().alive()) {
        Player& pl = level.player();
        Vec2 back = p.vel.lengthSq() > 1.0f ? p.vel.normalized() * -4.0f : Vec2{0.0f, -4.0f};
        Vec2 from = pl.body.pos;
        pl.body.pos = pl.body.prevPos = at + back;
        unbury(pl.body, level.world());
        pl.body.vel = {};
        level.particles().burst(from, 20, 120.0f, {120, 255, 220, 255}, {60, 200, 160, 255}, 0.5f, rng, 0.0f);
        level.particles().burst(pl.body.pos, 20, 120.0f, {120, 255, 220, 255}, {60, 200, 160, 255}, 0.5f, rng, 0.0f);
        level.audio().play(Sfx::Portal, 0.35f, 2.0f);
    }
    if (p.payload) {
        Vec2 dir = p.vel.lengthSq() > 1.0f ? p.vel.normalized() : Vec2{1.0f, 0.0f};
        level.castShot(*p.payload, at - dir * 1.5f, angleOf(dir), p.team, p.ownerId, p.damageMul);
    }
    if (d.explosionRadius <= 0.0f) {
        // Impact sparks streak off the hit (cosmetic randomness: gameplay streams are untouched).
        Rgba c = hexRgba(d.color);
        level.particles().burst(at, 10, 110.0f, c, scaled(c, 0.8f), 0.22f, level.fxRng(), 0.3f,
                                partflag::kFade | partflag::kDieOnHit | partflag::kStreak);
    }
    p.dead = true;
}

void hitActor(Level& level, Projectile& p, Actor& a) {
    const ProjectileDef& d = p.def;
    Rng& rng = level.rng();
    float dmg = d.damage * p.damageMul;
    bool crit = false;
    float critChance = p.critChance + (p.team == Team::Player && level.run().hasPerk(PerkId::CriticalMind)
                                           ? 0.15f * level.run().perkCount(PerkId::CriticalMind)
                                           : 0.0f);
    if (critChance > 0.0f && rng.chance(critChance)) {
        dmg *= 3.0f;
        crit = true;
    }
    Vec2 kb = p.vel.lengthSq() > 1.0f ? p.vel.normalized() * d.knockback : Vec2{};
    if (dmg > 0.0f) a.applyDamage(level, {dmg, d.damageType, kb, p.team, crit, false, projectileCause(level, p)});
    if (crit && p.team == Team::Player) level.critPunch(a.body.pos);
    if (((d.flags & kIncendiary) || d.damageType == DamageType::Fire) && a.status.wet <= 0.0f) a.status.burning = std::max(a.status.burning, 3.0f);
    if (d.flags & kFreezing) a.status.chilled = 2.5f;
    if (d.flags & kElectric) a.status.shocked = 0.4f;
    p.rememberHit(a.id);

    // Arc sparks leap to the next foe.
    if (p.chainsLeft > 0) {
        if (Actor* next = level.nearestHostile(a.body.pos, p.team, 90.0f, a.id)) {
            Projectile arc = p;
            arc.chainsLeft = p.chainsLeft - 1;
            arc.age = 0.0f;
            arc.pos = arc.prevPos = a.body.pos;
            arc.vel = (next->body.pos - a.body.pos).normalized() * d.speed;
            arc.damageMul *= 0.85f;
            arc.payload.reset();
            arc.dead = false;
            level.addProjectile(arc);
        }
        p.chainsLeft = 0;
    }
}

/// Returns the creature overlapping `pos` (expanded by `radius`) that this projectile may hit.
Actor* findVictim(Level& level, const Projectile& p, Vec2 pos) {
    const float r = p.def.radius;
    auto overlaps = [&](const Actor& a) {
        Rectf b = a.bounds();
        return pos.x >= b.x - r && pos.x <= b.x + b.w + r && pos.y >= b.y - r && pos.y <= b.y + b.h + r;
    };
    if (p.team == Team::Player) {
        for (auto& e : level.enemies()) {
            if (e->alive() && e->team != Team::Player && !p.alreadyHit(e->id) && overlaps(*e)) return e.get();
        }
    } else {
        Player& pl = level.player();
        if (pl.alive() && !p.alreadyHit(pl.id) && overlaps(pl)) return &pl;
        for (auto& e : level.enemies()) {
            if (e->alive() && e->team == Team::Player && !p.alreadyHit(e->id) && overlaps(*e)) return e.get();
        }
    }
    return nullptr;
}

/// Homing, gravity and drag.
void steer(Level& level, Projectile& p, float dt) {
    const ProjectileDef& d = p.def;
    if (d.flags & kHoming) {
        if (Actor* t = level.nearestHostile(p.pos, p.team, 170.0f)) {
            float speed = p.vel.length();
            float cur = angleOf(p.vel), want = angleOf(t->body.pos - p.pos);
            float turn = clamp(wrapAngle(want - cur), -7.0f * dt, 7.0f * dt);
            p.vel = fromAngle(cur + turn, speed);
        }
    }
    p.vel.y += kGravity * d.gravity * dt;
    if (d.drag > 0.0f) p.vel *= 1.0f / (1.0f + d.drag * dt);
}

/// A gravity well eats the terrain it floats through and drags foes in, grinding those at
/// its core.
void pullIntoWell(Level& level, Projectile& p, float dt) {
    const ProjectileDef& d = p.def;
    level.dig(p.pos, d.digRadius, d.digPower, false);
    for (auto& e : level.enemies()) {
        if (!e->alive() || e->team == p.team) continue;
        Vec2 to = p.pos - e->body.pos;
        float dist = to.length();
        if (dist < 70.0f && dist > 0.5f) {
            e->body.vel += to.normalized() * (900.0f * dt * (1.0f - dist / 70.0f));
            if (dist < d.digRadius + 3.0f && p.hitCooldown <= 0.0f)
                e->applyDamage(level, {d.damage * p.damageMul, DamageType::Physical, {}, p.team, false, true});
        }
    }
    if (p.hitCooldown <= 0.0f) p.hitCooldown = 0.1f;
}

/// Matter trails and sparkle left along this tick's path.
void emitTrail(Level& level, Projectile& p) {
    const ProjectileDef& d = p.def;
    World& world = level.world();
    Rng& rng = level.rng();
    if (d.trailMat != mat::Empty && phaseOf(d.trailMat) == Phase::Solid) {
        // A solid trail (rime) is laid cell by cell along the whole step, so it forms an
        // unbroken bridge - though never inside the caster.
        const Rectf keepClear = level.player().bounds();
        const Vec2 step = p.pos - p.prevPos;
        const int n = std::max(1, static_cast<int>(std::ceil(step.length())));
        for (int i = 0; i <= n; ++i) {
            const Vec2 t = p.prevPos + step * (static_cast<float>(i) / static_cast<float>(n));
            if (t.x > keepClear.x - 2.0f && t.x < keepClear.x + keepClear.w + 2.0f && t.y > keepClear.y - 2.0f &&
                t.y < keepClear.y + keepClear.h + 2.0f)
                continue;
            world.placeIfEmpty(floorToInt(t.x), floorToInt(t.y), d.trailMat);
        }
    } else if (d.trailMat != mat::Empty && d.trailRate > 0.0f) {
        p.trailAccum += d.trailRate;
        while (p.trailAccum >= 1.0f) {
            p.trailAccum -= 1.0f;
            Vec2 t = lerp(p.prevPos, p.pos, rng.uniform());
            int tx = floorToInt(t.x), ty = floorToInt(t.y);
            if (phaseOf(d.trailMat) == Phase::Fire || phaseOf(d.trailMat) == Phase::Gas) {
                world.placeIfEmpty(tx, ty, d.trailMat);
            } else {
                Cell c = makeCell(d.trailMat, rng.next());
                c.flags |= cellflag::kPlayerMade;
                level.particles().addMatter(t, p.vel * 0.1f, c);
            }
        }
    }
    const bool sparkly = d.visual == ProjVisual::Star || d.visual == ProjVisual::Spark || d.visual == ProjVisual::Bolt ||
                         d.visual == ProjVisual::Orb || d.visual == ProjVisual::BigOrb;
    if (sparkly && rng.chance(d.visual == ProjVisual::Star ? 0.9f : 0.35f)) {
        Rgba c = hexRgba(d.color);
        level.particles().addVisual(lerp(p.prevPos, p.pos, rng.uniform()), p.vel * 0.05f + Vec2{rng.range(-8.0f, 8.0f), rng.range(-8.0f, 8.0f)},
                                    c, scaled(c, 0.7f), rng.range(0.15f, 0.4f), d.visual == ProjVisual::Star ? 0.15f : 0.0f);
    }
    if (d.visual == ProjVisual::Bomb && rng.chance(0.5f)) {
        level.particles().addVisual(p.pos + Vec2{0.0f, -2.0f}, {rng.range(-10.0f, 10.0f), rng.range(-30.0f, -10.0f)},
                                    {255, 200, 80, 255}, {255, 150, 40, 255}, 0.2f, 0.0f);
    }
}

} // namespace

void fireHitscan(Level& level, Projectile& p, float range) {
    World& world = level.world();
    const Vec2 dir = p.vel.lengthSq() > 1.0f ? p.vel.normalized() : Vec2{1.0f, 0.0f};
    Vec2 q = p.pos;
    for (float t = 0.0f; t < range; t += 1.0f) {
        Vec2 next = q + dir;
        if (!world.inBounds(floorToInt(next.x), floorToInt(next.y)) ||
            isSolidForCollision(world.matAt(floorToInt(next.x), floorToInt(next.y))))
            break;
        q = next;
        if (Actor* victim = findVictim(level, p, q)) hitActor(level, p, *victim);
    }
    p.beamEnd = q;
    if (p.team == Team::Player) level.knockLanterns(p.pos, q, 2.5f);
    Projectile effects = p;
    effects.vel = dir * 100.0f;
    impact(level, effects, q);
    p.dead = false;
    p.age = 0.0f;
}

void updateProjectile(Level& level, Projectile& p, float dt) {
    const ProjectileDef& d = p.def;
    World& world = level.world();
    Rng& rng = level.rng();
    p.prevPos = p.pos;
    p.age += dt;
    p.hitCooldown = std::max(0.0f, p.hitCooldown - dt);

    if (d.flags & kHitscan) {
        if (p.age >= d.lifetime) p.dead = true;
        return;
    }
    if (p.age >= d.lifetime) {
        impact(level, p, p.pos);
        return;
    }

    steer(level, p, dt);
    if (d.flags & kGravityWell) pullIntoWell(level, p, dt);

    // The alchemist's spells knock down lanterns they pass, however the march below ends.
    struct LanternSweep {
        Level& level;
        const Projectile& p;
        Vec2 from;
        ~LanternSweep() {
            if (p.team == Team::Player) level.knockLanterns(from, p.pos, 2.5f + p.def.radius);
        }
    } lanternSweep{level, p, p.pos};

    // March along the path in sub-cell steps.
    Vec2 delta = p.vel * dt;
    const float dist = delta.length();
    const int steps = std::max(1, static_cast<int>(std::ceil(dist)));
    const Vec2 step = delta / static_cast<float>(steps);
    for (int s = 0; s < steps && !p.dead; ++s) {
        Vec2 next = p.pos + step;
        const int nx = floorToInt(next.x), ny = floorToInt(next.y);
        if (!world.inBounds(nx, ny)) {
            p.dead = true;
            return;
        }

        if ((d.flags & kDigs) && !(d.flags & kGravityWell)) level.dig(next, d.digRadius, d.digPower, true);

        const MatId m = world.at(nx, ny).mat;
        const MaterialDef& md = matDef(m);
        if (isSolidForCollision(m) && !(d.flags & kNoCollideWorld)) {
            if (d.flags & kRolls) {
                // Saw blades ride along floors and turn around at walls.
                bool wallX = isSolidForCollision(world.matAt(nx, floorToInt(p.pos.y)));
                if (wallX) p.vel.x = -p.vel.x;
                else p.vel.y = std::min(p.vel.y, 0.0f) * 0.2f;
                break;
            }
            if (p.bouncesLeft > 0) {
                bool blockX = isSolidForCollision(world.matAt(nx, floorToInt(p.pos.y)));
                bool blockY = isSolidForCollision(world.matAt(floorToInt(p.pos.x), ny));
                if (blockX) p.vel.x = -p.vel.x;
                if (blockY) p.vel.y = -p.vel.y;
                if (!blockX && !blockY) p.vel = -p.vel;
                float damp = d.visual == ProjVisual::Bomb ? 0.45f : 0.9f;
                p.vel *= damp;
                --p.bouncesLeft;
                if (d.visual == ProjVisual::Bomb && p.vel.length() > 60.0f) level.audio().playAt(Sfx::Land, p.pos, 0.3f, 1.5f);
                break;
            }
            impact(level, p, p.pos);
            return;
        }
        if (md.phase == Phase::Liquid) {
            if ((d.damageType == DamageType::Fire || (d.flags & kIncendiary)) && (md.flags & matflag::kExtinguisher) &&
                d.explosionRadius < 12.0f) {
                world.placeIfEmpty(floorToInt(p.pos.x), floorToInt(p.pos.y) - 1, mat::Steam);
                level.particles().burst(p.pos, 8, 50.0f, {220, 230, 240, 200}, {0, 0, 0, 0}, 0.4f, rng, -0.3f);
                level.audio().playAt(Sfx::Sizzle, p.pos, 0.4f);
                p.dead = true;
                return;
            }
            if (d.flags & kFreezing) {
                impact(level, p, next);
                return;
            }
            p.vel *= 0.985f;
        }

        if (Actor* victim = findVictim(level, p, next)) {
            if (d.flags & kRolls) {
                if (p.hitCooldown <= 0.0f) {
                    hitActor(level, p, *victim);
                    p.hitCount = 0; // saws may hit the same foe again after the cooldown
                    p.hitCooldown = 0.15f;
                }
            } else {
                hitActor(level, p, *victim);
                if (!(d.flags & kPiercing)) {
                    impact(level, p, next);
                    return;
                }
            }
        }
        p.pos = next;
    }

    emitTrail(level, p);
}

void drawProjectile(const Projectile& p, DrawContext& ctx, uint32_t tick) {
    const ProjectileDef& d = p.def;
    WorldRenderer& layer = ctx.scene.worldLayer();
    const Vec2 pos = lerp(p.prevPos, p.pos, ctx.alpha);
    const Rgba c = hexRgba(d.color);
    const Rgba glow = scaled(c, 0.9f);
    const int x = floorToInt(pos.x), y = floorToInt(pos.y);
    const Vec2 dir = p.vel.lengthSq() > 1.0f ? p.vel.normalized() : Vec2{1.0f, 0.0f};
    auto put = [&](int px, int py, Rgba col, Rgba g) { layer.stamp(px, py, col, g); };
    const Rgba white{255, 255, 255, 255};

    switch (d.visual) {
    case ProjVisual::Spark:
        put(x, y, white, glow);
        put(floorToInt(pos.x - dir.x), floorToInt(pos.y - dir.y), c, glow);
        break;
    case ProjVisual::Bolt:
        for (int i = 0; i < 4; ++i)
            put(floorToInt(pos.x - dir.x * i), floorToInt(pos.y - dir.y * i), i == 0 ? white : c, scaled(glow, 1.0f - i * 0.2f));
        break;
    case ProjVisual::Orb:
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) put(x + dx, y + dy, (dx == 0 && dy == 0) ? white : c, glow);
        break;
    case ProjVisual::BigOrb: {
        const int r = d.radius > 4.0f ? 4 : 2;
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (dx * dx + dy * dy <= r * r) put(x + dx, y + dy, (dx * dx + dy * dy <= 1) ? white : c, glow);
        break;
    }
    case ProjVisual::Bomb: {
        const Rgba body{40, 36, 40, 255}, rim{90, 80, 80, 255};
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) put(x + dx, y + dy, (dx == -1 && dy == -1) ? rim : body, {0, 0, 0, 0});
        bool blink = ((tick / 6) % 2) == 0;
        put(x, y - 2, blink ? white : c, blink ? Rgba{255, 200, 80, 255} : Rgba{120, 60, 20, 255});
        break;
    }
    case ProjVisual::Shard:
        put(x, y, white, glow);
        put(floorToInt(pos.x - dir.x), floorToInt(pos.y - dir.y), c, glow);
        put(floorToInt(pos.x - dir.x * 2), floorToInt(pos.y - dir.y * 2), c, scaled(glow, 0.5f));
        put(floorToInt(pos.x - dir.y), floorToInt(pos.y + dir.x), c, scaled(glow, 0.5f));
        break;
    case ProjVisual::Saw: {
        float a = static_cast<float>(tick) * 0.6f;
        for (int k = 0; k < 6; ++k) {
            Vec2 o = fromAngle(a + k * kTau / 6.0f, 2.2f);
            put(floorToInt(pos.x + o.x), floorToInt(pos.y + o.y), c, scaled(glow, 0.4f));
        }
        put(x, y, white, scaled(glow, 0.4f));
        break;
    }
    case ProjVisual::Glob:
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (std::abs(dx) + std::abs(dy) <= 1) put(x + dx, y + dy, c, scaled(glow, 0.5f));
        break;
    case ProjVisual::Star: {
        bool phase = (tick / 3) % 2 == 0;
        put(x, y, white, glow);
        if (phase) {
            put(x + 1, y, c, glow);
            put(x - 1, y, c, glow);
            put(x, y + 1, c, glow);
            put(x, y - 1, c, glow);
        } else {
            put(x + 1, y + 1, c, glow);
            put(x - 1, y - 1, c, glow);
            put(x - 1, y + 1, c, glow);
            put(x + 1, y - 1, c, glow);
        }
        break;
    }
    case ProjVisual::Beam: {
        // Jagged lightning between start and end.
        Vec2 a = p.pos, b = p.beamEnd;
        float len = (b - a).length();
        Vec2 n = (b - a).normalized();
        Vec2 perp{-n.y, n.x};
        uint32_t seed = static_cast<uint32_t>(tick / 2) * 7919u;
        for (float t = 0.0f; t < len; t += 1.0f) {
            float jitter = (hashToUnit(hash2(static_cast<int>(t / 6.0f), 0, seed)) - 0.5f) * 6.0f;
            Vec2 q = a + n * t + perp * jitter * std::sin(t / len * kPi);
            put(floorToInt(q.x), floorToInt(q.y), white, {200, 220, 255, 255});
        }
        break;
    }
    }
}

} // namespace pm
