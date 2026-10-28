#include "game/Projectile.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "game/Level.hpp"
#include "render/SceneRenderer.hpp"

namespace pm {

namespace {

using namespace projflag;

/// A fused charge bounces off terrain it strikes faster than this (cells / s into the surface) and
/// settles below it: well above the speed one tick of gravity adds, so a resting charge stays put.
constexpr float kFuseBounceSpeed = 40.0f;
constexpr float kFuseFriction = 400.0f; ///< cells / s^2 lost by a fused charge rolling along a surface

/// What to blame for a projectile's damage: the creature that cast it, else the spell.
const char* projectileCause(const Level& level, const Projectile& p) {
    if (p.ownerName) return p.ownerName; // named at launch, so a shot still in flight when its caster dies blames it
    if (p.team == Team::Monster)
        if (const char* name = level.actorName(p.ownerId)) return name;
    return p.spell == SpellId::None ? nullptr : spellDef(p.spell).name;
}

/// The chance this projectile's hit (or blast) is critical: its own, plus Critical Mind for the alchemist's shots.
float critChanceOf(const Level& level, const Projectile& p) {
    return p.critChance + (p.team == Team::Player && level.run().hasPerk(PerkId::CriticalMind)
                               ? 0.15f * level.run().perkCount(PerkId::CriticalMind)
                               : 0.0f);
}

Rgba scaled(Rgba c, float k) {
    return {static_cast<uint8_t>(std::min(255.0f, c.r * k)), static_cast<uint8_t>(std::min(255.0f, c.g * k)),
            static_cast<uint8_t>(std::min(255.0f, c.b * k)), c.a};
}

/// What a shot carries for its caster, a Teleport Bolt's blink and a trigger's payload, let go at `at` however the shot
/// ends (on impact, doused, or past the world's edge), since its mana is spent. `sparkle` draws the blink's particles.
void releaseCargo(Level& level, const Projectile& p, Vec2 at, Rng& sparkle) {
    if ((p.def.flags & kTeleport) && p.team == Team::Player && level.player().alive()) {
        Player& pl = level.player();
        Vec2 back = p.vel.lengthSq() > 1.0f ? p.vel.normalized() * -4.0f : Vec2{0.0f, -4.0f};
        Vec2 from = pl.body.pos;
        pl.body.pos = pl.body.prevPos = at + back;
        unbury(pl.body, level.world());
        pl.body.vel = {};
        level.particles().burst(from, 20, 120.0f, {120, 255, 220, 255}, {60, 200, 160, 255}, 0.5f, sparkle, 0.0f);
        level.particles().burst(pl.body.pos, 20, 120.0f, {120, 255, 220, 255}, {60, 200, 160, 255}, 0.5f, sparkle, 0.0f);
        level.audio().play(Sfx::Portal, 0.35f, 2.0f);
    }
    if (p.payload) {
        Vec2 dir = p.vel.lengthSq() > 1.0f ? p.vel.normalized() : Vec2{1.0f, 0.0f};
        level.castShot(*p.payload, at - dir * 1.5f, angleOf(dir), p.team, p.ownerId, p.damageMul);
    }
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
        // A critical strike lands on the blast as on a direct hit: one roll for all it catches.
        const float critChance = critChanceOf(level, p);
        e.crit = critChance > 0.0f && rng.chance(critChance);
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
    // Its own splash is the caster's doing (the shocks carried through charged matter are the world's).
    if (d.flags & kElectric) level.electrify(at, d.damage * p.damageMul * 0.8f, p.team, projectileCause(level, p));
    if (d.flags & kTransmute) level.transmuteArea(at, 6.0f);
    releaseCargo(level, p, at, rng);
    if (d.explosionRadius <= 0.0f) {
        // Impact sparks streak off the hit (cosmetic randomness: gameplay streams are untouched).
        Rgba c = hexRgba(d.color);
        level.particles().burst(at, 10, 110.0f, c, scaled(c, 0.8f), 0.22f, level.fxRng(), 0.3f,
                                partflag::kFade | partflag::kDieOnHit | partflag::kStreak);
        // The bolt's light pops on what it struck instead of switching off with it (hitscan beams
        // flash where they end already, and diggers strike rock all along their tunnel).
        if (!(d.flags & (projflag::kHitscan | projflag::kDigs))) level.addFlash(at, d.color, 1.8f, std::max(16.0f, d.lightRadius), 0.12f);
    }
    p.dead = true;
}

void hitActor(Level& level, Projectile& p, Actor& a) {
    const ProjectileDef& d = p.def;
    Rng& rng = level.rng();
    float dmg = d.damage * p.damageMul;
    bool crit = false;
    const float critChance = critChanceOf(level, p);
    if (critChance > 0.0f && rng.chance(critChance)) {
        dmg *= 3.0f;
        crit = true;
    }
    Vec2 kb = p.vel.lengthSq() > 1.0f ? p.vel.normalized() * d.knockback : Vec2{};
    if (dmg > 0.0f) a.applyDamage(level, {dmg, d.damageType, kb, p.team, crit, false, projectileCause(level, p)});
    if (crit && p.team == Team::Player) level.critPunch(a.body.pos);
    else if (p.team == Team::Player && dmg > 0.0f) level.audio().playAt(Sfx::Hit, a.body.pos, 0.25f, level.fxRng().range(0.9f, 1.1f));
    // A fiery hit sets the victim alight, unless it is soaked or fireproof (unharmed by the flames, it would only carry them).
    const bool fireproof = a.resist[static_cast<size_t>(DamageType::Fire)] <= 0.0f;
    if (((d.flags & kIncendiary) || d.damageType == DamageType::Fire) && a.status.wet <= 0.0f && !fireproof)
        a.status.burning = std::max(a.status.burning, 3.0f);
    if (d.flags & kFreezing) a.status.chilled = 2.5f;
    if (d.flags & kElectric) a.status.shocked = 0.4f;
    p.rememberHit(a.id);

    // Arc sparks leap to the next foe, never back to one already struck (this one is remembered above).
    if (p.chainsLeft > 0) {
        if (Actor* next = level.nearestHostile(a.body.pos, p.team, 90.0f, a.id, &p)) {
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

/// Whether the cell at (x, y) stops a shot of `team`: solid matter (out of bounds reads as bedrock), save the rime the
/// alchemist's trails lay, which their own spells pass through, beams as much as bolts.
bool shotBlocked(const World& world, int x, int y, Team team) {
    if (!world.inBounds(x, y)) return true;
    const Cell& c = world.at(x, y);
    const bool rime = (c.flags & cellflag::kRime) && team == Team::Player;
    return isSolidForCollision(c.mat) && !rime;
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
        // A seeking shot passes over the foes it has struck (a piercing one would circle back to them), flying straight
        // on when no other is in reach; a gravity well keeps to the one it grinds.
        const Projectile* struckBy = (d.flags & kGravityWell) ? nullptr : &p;
        if (Actor* t = level.nearestHostile(p.pos, p.team, 170.0f, 0, struckBy)) {
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
                e->applyDamage(level, {d.damage * p.damageMul, DamageType::Physical, {}, p.team, false, true, projectileCause(level, p)});
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
        // unbroken bridge - though never inside the caster, nor inside a creature it would entomb.
        // Its cells are marked so that spells laying the same trail (this one, its siblings) fly
        // through them instead of crashing.
        const Rectf keepClear = level.player().bounds();
        const Vec2 step = p.pos - p.prevPos;
        // Only creatures whose bodies reach this step's cells are tested cell by cell.
        const Rectf swept{std::min(p.prevPos.x, p.pos.x) - 1.0f, std::min(p.prevPos.y, p.pos.y) - 1.0f, std::fabs(step.x) + 2.0f,
                          std::fabs(step.y) + 2.0f};
        const bool creaturesNear = std::any_of(level.enemies().begin(), level.enemies().end(), [&](const auto& e) {
            return e->alive() && e->body.collides && e->bounds().overlaps(swept);
        });
        auto insideCreature = [&](int tx, int ty) {
            const Rectf cell{static_cast<float>(tx), static_cast<float>(ty), 1.0f, 1.0f};
            for (const auto& e : level.enemies())
                if (e->alive() && e->body.collides && e->bounds().overlaps(cell)) return true;
            return false;
        };
        const int n = std::max(1, static_cast<int>(std::ceil(step.length())));
        for (int i = 0; i <= n; ++i) {
            const Vec2 t = p.prevPos + step * (static_cast<float>(i) / static_cast<float>(n));
            if (t.x > keepClear.x - 2.0f && t.x < keepClear.x + keepClear.w + 2.0f && t.y > keepClear.y - 2.0f &&
                t.y < keepClear.y + keepClear.h + 2.0f)
                continue;
            const int tx = floorToInt(t.x), ty = floorToInt(t.y);
            if (creaturesNear && insideCreature(tx, ty)) continue;
            if (world.placeIfEmpty(tx, ty, d.trailMat)) world.at(tx, ty).flags |= cellflag::kRime;
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
    if (Rng& fx = level.fxRng(); d.visual == ProjVisual::Bomb && fx.chance(0.5f)) {
        // The fuse's embers are decoration: cosmetic randomness, so they never shift gameplay.
        level.particles().addVisual(p.pos + Vec2{0.0f, -2.0f}, {fx.range(-10.0f, 10.0f), fx.range(-30.0f, -10.0f)},
                                    {255, 200, 80, 255}, {255, 150, 40, 255}, 0.2f, 0.0f);
    }
}

/// How one sub-cell step of a shot's march turns out.
enum class StepOutcome {
    Advance, ///< it moves on to the step's point
    Stop,    ///< it stops short for this tick (bounced, turned back or came to rest) and still lays its trail
    Gone,    ///< it is gone (burst, doused or past the world's edge): its tick ends there, without a trail
};

/// One sub-cell step of a shot's march along its path.
struct MarchStep {
    Vec2 next;      ///< the point it steps to (a saw blade's climb or ride and a fused charge's slide move it)
    int nx, ny;     ///< the cell `next` first fell in, which the terrain tests read
    float friction; ///< the speed a fused charge sliding along a surface loses over the step
};

/// A saw blade meeting terrain climbs a lip or rides along the surface; turned back by a taller wall, it stops for the tick.
StepOutcome rollAlongTerrain(const World& world, Projectile& p, MarchStep& step) {
    // Saw blades climb a one-cell lip, turn around at taller walls and ride along floors (and ceilings),
    // keeping only the slide along the surface; they go on to cut whatever they roll into.
    const int row = floorToInt(p.pos.y);
    if (shotBlocked(world, step.nx, row, p.team)) {
        if (shotBlocked(world, step.nx, row - 1, p.team) || shotBlocked(world, floorToInt(p.pos.x), row - 1, p.team)) {
            p.vel.x = -p.vel.x;
            return StepOutcome::Stop;
        }
        step.next.y = p.pos.y - 1.0f;
        p.vel.y = std::min(p.vel.y, 0.0f);
    } else {
        p.vel.y = std::min(p.vel.y, 0.0f) * 0.2f;
        step.next.y = p.pos.y;
    }
    return StepOutcome::Advance;
}

/// A fused charge meeting terrain bounces off it when it strikes hard, else settles against it and slides along it.
StepOutcome settleAgainstTerrain(Level& level, Projectile& p, MarchStep& step) {
    // A fused charge never bursts on terrain: it bounces off what it strikes hard while it has
    // bounces left, else settles against the surface and rolls to a stop along it, until its
    // fuse runs out (its lifetime) or a foe touches it.
    const World& world = level.world();
    const bool blockX = shotBlocked(world, step.nx, floorToInt(p.pos.y), p.team);
    const bool blockY = shotBlocked(world, floorToInt(p.pos.x), step.ny, p.team);
    // Its speed into the surface (its whole speed into a corner).
    const float into = blockX != blockY ? std::fabs(blockX ? p.vel.x : p.vel.y) : p.vel.length();
    if (p.bouncesLeft > 0 && into > kFuseBounceSpeed) {
        if (blockX) p.vel.x = -p.vel.x;
        if (blockY) p.vel.y = -p.vel.y;
        if (!blockX && !blockY) p.vel = -p.vel;
        p.vel *= 0.45f;
        --p.bouncesLeft;
        if (p.vel.length() > 60.0f) level.audio().playAt(Sfx::Land, p.pos, 0.3f, 1.5f);
        return StepOutcome::Stop;
    }
    // At rest nothing carries into the surface (so gravity's pull each tick neither bounces nor
    // shakes it), and friction slows the slide along it.
    if (blockY && !blockX) {
        p.vel.y = 0.0f;
        p.vel.x = approach(p.vel.x, 0.0f, step.friction);
        step.next.y = p.pos.y;
    } else if (blockX && !blockY) {
        p.vel.x = 0.0f;
        p.vel.y = approach(p.vel.y, 0.0f, step.friction);
        step.next.x = p.pos.x;
    } else {
        p.vel = {}; // wedged into a corner
        return StepOutcome::Stop;
    }
    return StepOutcome::Advance;
}

/// A shot with bounces left rebounds off the terrain it meets and stops for the tick; a bomb rebounds dully, with a thud.
StepOutcome bounceOffTerrain(Level& level, Projectile& p, const MarchStep& step) {
    const ProjectileDef& d = p.def;
    const World& world = level.world();
    bool blockX = shotBlocked(world, step.nx, floorToInt(p.pos.y), p.team);
    bool blockY = shotBlocked(world, floorToInt(p.pos.x), step.ny, p.team);
    if (blockX) p.vel.x = -p.vel.x;
    if (blockY) p.vel.y = -p.vel.y;
    if (!blockX && !blockY) p.vel = -p.vel;
    float damp = d.visual == ProjVisual::Bomb ? 0.45f : 0.9f;
    p.vel *= damp;
    --p.bouncesLeft;
    if (d.visual == ProjVisual::Bomb && p.vel.length() > 60.0f) level.audio().playAt(Sfx::Land, p.pos, 0.3f, 1.5f);
    return StepOutcome::Stop;
}

/// Solid matter met on the step: a saw blade rides it, a fused charge rests on it, a bouncing shot rebounds, others burst.
StepOutcome strikeTerrain(Level& level, Projectile& p, MarchStep& step) {
    const ProjectileDef& d = p.def;
    if (d.flags & kRolls) return rollAlongTerrain(level.world(), p, step);
    if (d.flags & kFused) return settleAgainstTerrain(level, p, step);
    if (p.bouncesLeft > 0) return bounceOffTerrain(level, p, step);
    // A thud as it strikes matter (blasts and diggers have their own sounds).
    if (d.explosionRadius <= 0.0f && !(d.flags & kDigs))
        level.audio().playAt(Sfx::Impact, p.pos, 0.22f, level.fxRng().range(0.85f, 1.15f));
    impact(level, p, p.pos);
    return StepOutcome::Gone;
}

/// Liquid at `next` slows the shot; it douses a fiery one (letting go what it carries) and a freezing one bursts on it.
StepOutcome wadeThroughLiquid(Level& level, Projectile& p, Vec2 next) {
    const ProjectileDef& d = p.def;
    World& world = level.world();
    // The matter moved into: a rolling saw's slide or climb keeps it on an open row.
    const MaterialDef& md = matDef(world.at(floorToInt(next.x), floorToInt(next.y)).mat);
    if (md.phase != Phase::Liquid) return StepOutcome::Advance;
    if ((d.damageType == DamageType::Fire || (d.flags & kIncendiary)) && (md.flags & matflag::kExtinguisher) &&
        d.explosionRadius < 12.0f) {
        world.placeIfEmpty(floorToInt(p.pos.x), floorToInt(p.pos.y) - 1, mat::Steam);
        level.particles().burst(p.pos, 8, 50.0f, {220, 230, 240, 200}, {0, 0, 0, 0}, 0.4f, level.rng(), -0.3f);
        level.audio().playAt(Sfx::Sizzle, p.pos, 0.4f);
        // Doused, its fire and blast are out, but what it carries is still let go (cosmetic sparkle, as at the world's edge).
        releaseCargo(level, p, p.pos, level.fxRng());
        p.dead = true;
        return StepOutcome::Gone;
    }
    if (d.flags & kFreezing) {
        impact(level, p, next);
        return StepOutcome::Gone;
    }
    p.vel *= 0.985f;
    return StepOutcome::Advance;
}

/// A creature at `next` takes the shot's hit: a saw cuts it once per cooldown, a piercing shot passes on, others burst.
StepOutcome strikeCreature(Level& level, Projectile& p, Vec2 next) {
    const ProjectileDef& d = p.def;
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
                return StepOutcome::Gone;
            }
        }
    }
    return StepOutcome::Advance;
}

/// Takes one sub-cell step: the world's edge first, then what the shot digs, and the terrain, liquid and creatures it meets.
StepOutcome takeStep(Level& level, Projectile& p, MarchStep& step) {
    const ProjectileDef& d = p.def;
    const World& world = level.world();
    if (!world.inBounds(step.nx, step.ny)) {
        // Gone past the world's edge: what it carries is let go at the last point inside (cosmetic sparkle, so
        // the payload's own cast is the only gameplay randomness drawn).
        releaseCargo(level, p, p.pos, level.fxRng());
        p.dead = true;
        return StepOutcome::Gone;
    }

    if ((d.flags & kDigs) && !(d.flags & kGravityWell)) {
        // A rolling saw cuts at and above its own row, never the floor it rides on (it would sink into soft ground).
        const int lowestRow = (d.flags & kRolls) ? floorToInt(p.pos.y) : std::numeric_limits<int>::max();
        level.dig(step.next, d.digRadius, d.digPower, true, lowestRow);
    }

    // The alchemist's spells pass through rime laid by their own trails: the shot laying it, its
    // siblings, and a trigger's payload released on the line. Other matter, natural ice included,
    // still stops them, and rime stops the creatures' shots. Out of bounds reads as bedrock.
    if (shotBlocked(world, step.nx, step.ny, p.team) && !(d.flags & kNoCollideWorld)) {
        const StepOutcome struck = strikeTerrain(level, p, step);
        if (struck != StepOutcome::Advance) return struck;
    }
    const StepOutcome waded = wadeThroughLiquid(level, p, step.next);
    if (waded != StepOutcome::Advance) return waded;
    return strikeCreature(level, p, step.next);
}

/// Moves the shot along this tick's path in sub-cell steps; false when it is gone on the way, so it lays no trail.
bool marchAlongPath(Level& level, Projectile& p, float dt) {
    // March along the path in sub-cell steps.
    Vec2 delta = p.vel * dt;
    const float dist = delta.length();
    const int steps = std::max(1, static_cast<int>(std::ceil(dist)));
    const Vec2 stride = delta / static_cast<float>(steps);
    const float friction = kFuseFriction * dt / static_cast<float>(steps);
    for (int s = 0; s < steps && !p.dead; ++s) {
        const Vec2 next = p.pos + stride;
        MarchStep step{next, floorToInt(next.x), floorToInt(next.y), friction};
        const StepOutcome outcome = takeStep(level, p, step);
        if (outcome == StepOutcome::Stop) break;
        if (outcome == StepOutcome::Gone) return false;
        p.pos = step.next;
    }
    return true;
}

} // namespace

void fireHitscan(Level& level, Projectile& p, float range) {
    World& world = level.world();
    const Vec2 dir = p.vel.lengthSq() > 1.0f ? p.vel.normalized() : Vec2{1.0f, 0.0f};
    Vec2 q = p.pos;
    for (float t = 0.0f; t < range; t += 1.0f) {
        Vec2 next = q + dir;
        // The alchemist's beams cross their rime as their bolts do (a Rime Trail trigger lets its payload go on it).
        if (shotBlocked(world, floorToInt(next.x), floorToInt(next.y), p.team)) break;
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
    const Vec2 from = p.pos;
    if (marchAlongPath(level, p, dt)) emitTrail(level, p);
    if (p.team == Team::Player) level.knockLanterns(from, p.pos, 2.5f + d.radius);
}

namespace {

constexpr Rgba kWhite{255, 255, 255, 255}; ///< the white-hot core most shots are drawn with

/// Where a shot's head is drawn this frame and in what colours, for the visuals stamped around it.
struct ShotLook {
    WorldRenderer& layer;
    Vec2 pos;   ///< the head, between its last two ticks' positions
    int x, y;   ///< the cell the head lies in
    Rgba color; ///< the shot's colour
    Rgba glow;  ///< the light it sheds
    /// Stamps one cell of the shot onto the world layer.
    void put(int px, int py, Rgba col, Rgba g) const { layer.stamp(px, py, col, g); }
};

/// A big orb: a disc of the shot's colour (radius 4 for a broad shot, else 2) around a white-hot core.
void drawBigOrb(const ShotLook& s, float radius) {
    const int r = radius > 4.0f ? 4 : 2;
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            if (dx * dx + dy * dy <= r * r) s.put(s.x + dx, s.y + dy, (dx * dx + dy * dy <= 1) ? kWhite : s.color, s.glow);
}

/// A bomb: a dark casing with a lighter corner, the spark on its fuse blinking above it.
void drawBomb(const ShotLook& s, uint32_t tick) {
    const Rgba body{40, 36, 40, 255}, rim{90, 80, 80, 255};
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) s.put(s.x + dx, s.y + dy, (dx == -1 && dy == -1) ? rim : body, {0, 0, 0, 0});
    bool blink = ((tick / 6) % 2) == 0;
    s.put(s.x, s.y - 2, blink ? kWhite : s.color, blink ? Rgba{255, 200, 80, 255} : Rgba{120, 60, 20, 255});
}

/// A saw blade: six teeth of the shot's colour spinning around a white hub.
void drawSaw(const ShotLook& s, uint32_t tick) {
    float a = static_cast<float>(tick) * 0.6f;
    for (int k = 0; k < 6; ++k) {
        Vec2 o = fromAngle(a + k * kTau / 6.0f, 2.2f);
        s.put(floorToInt(s.pos.x + o.x), floorToInt(s.pos.y + o.y), s.color, scaled(s.glow, 0.4f));
    }
    s.put(s.x, s.y, kWhite, scaled(s.glow, 0.4f));
}

/// A star: a white point twinkling between a plus and a cross of the shot's colour.
void drawStar(const ShotLook& s, uint32_t tick) {
    bool phase = (tick / 3) % 2 == 0;
    s.put(s.x, s.y, kWhite, s.glow);
    if (phase) {
        s.put(s.x + 1, s.y, s.color, s.glow);
        s.put(s.x - 1, s.y, s.color, s.glow);
        s.put(s.x, s.y + 1, s.color, s.glow);
        s.put(s.x, s.y - 1, s.color, s.glow);
    } else {
        s.put(s.x + 1, s.y + 1, s.color, s.glow);
        s.put(s.x - 1, s.y - 1, s.color, s.glow);
        s.put(s.x - 1, s.y + 1, s.color, s.glow);
        s.put(s.x + 1, s.y - 1, s.color, s.glow);
    }
}

/// A hitscan beam from `a` to `b`, its kinks reshaped every other tick.
void drawBeam(WorldRenderer& layer, Vec2 a, Vec2 b, uint32_t tick) {
    // Jagged lightning between start and end.
    float len = (b - a).length();
    Vec2 n = (b - a).normalized();
    Vec2 perp{-n.y, n.x};
    uint32_t seed = static_cast<uint32_t>(tick / 2) * 7919u;
    for (float t = 0.0f; t < len; t += 1.0f) {
        float jitter = (hashToUnit(hash2(static_cast<int>(t / 6.0f), 0, seed)) - 0.5f) * 6.0f;
        Vec2 q = a + n * t + perp * jitter * std::sin(t / len * kPi);
        layer.stamp(floorToInt(q.x), floorToInt(q.y), kWhite, {200, 220, 255, 255});
    }
}

} // namespace

void drawProjectile(const Projectile& p, DrawContext& ctx, uint32_t tick) {
    const ProjectileDef& d = p.def;
    WorldRenderer& layer = ctx.scene.worldLayer();
    const Vec2 pos = lerp(p.prevPos, p.pos, ctx.alpha);
    const Rgba c = hexRgba(d.color);
    const Rgba glow = scaled(c, 0.9f);
    const int x = floorToInt(pos.x), y = floorToInt(pos.y);
    const Vec2 dir = p.vel.lengthSq() > 1.0f ? p.vel.normalized() : Vec2{1.0f, 0.0f};
    auto put = [&](int px, int py, Rgba col, Rgba g) { layer.stamp(px, py, col, g); };
    const ShotLook look{layer, pos, x, y, c, glow};

    switch (d.visual) {
    case ProjVisual::Spark:
        put(x, y, kWhite, glow);
        put(floorToInt(pos.x - dir.x), floorToInt(pos.y - dir.y), c, glow);
        break;
    case ProjVisual::Bolt:
        for (int i = 0; i < 4; ++i)
            put(floorToInt(pos.x - dir.x * i), floorToInt(pos.y - dir.y * i), i == 0 ? kWhite : c, scaled(glow, 1.0f - i * 0.2f));
        break;
    case ProjVisual::Orb:
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) put(x + dx, y + dy, (dx == 0 && dy == 0) ? kWhite : c, glow);
        break;
    case ProjVisual::BigOrb: drawBigOrb(look, d.radius); break;
    case ProjVisual::Bomb: drawBomb(look, tick); break;
    case ProjVisual::Shard:
        put(x, y, kWhite, glow);
        put(floorToInt(pos.x - dir.x), floorToInt(pos.y - dir.y), c, glow);
        put(floorToInt(pos.x - dir.x * 2), floorToInt(pos.y - dir.y * 2), c, scaled(glow, 0.5f));
        put(floorToInt(pos.x - dir.y), floorToInt(pos.y + dir.x), c, scaled(glow, 0.5f));
        break;
    case ProjVisual::Saw: drawSaw(look, tick); break;
    case ProjVisual::Glob:
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (std::abs(dx) + std::abs(dy) <= 1) put(x + dx, y + dy, c, scaled(glow, 0.5f));
        break;
    case ProjVisual::Star: drawStar(look, tick); break;
    case ProjVisual::Beam: drawBeam(layer, p.pos, p.beamEnd, tick); break;
    }
}

} // namespace pm
