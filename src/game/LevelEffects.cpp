// Level effects API: casting, explosions, digging and the other ways gameplay reshapes the
// simulated world, plus props that come down once it is reshaped beneath them.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <optional>

#include "core/Timing.hpp"
#include "game/Level.hpp"

namespace pm {

namespace {

constexpr float kDegToRad = kPi / 180.0f;

/// A kill the world made rather than a spell: a label for the moment and its colour.
struct AlchemyKill {
    const char* label;
    uint32_t color;
};

/// Reads how a creature died from the last hit it took. A spell's own damage is an ordinary kill;
/// fire, lava, acid, blasts the alchemist did not cast, cave-ins and other creatures are not.
std::optional<AlchemyKill> alchemyOf(const Actor& victim) {
    const char* cause = victim.lastHitCause;
    if (cause == nullptr) return std::nullopt;
    for (int i = 1; i < kSpellCount; ++i)
        if (std::strcmp(cause, spellDef(static_cast<SpellId>(i)).name) == 0) return std::nullopt;
    for (int i = 0; i < kPerkCount; ++i) // the alchemist's own gifts (Thorns) are no accident of the world
        if (std::strcmp(cause, perkDef(static_cast<PerkId>(i)).name) == 0) return std::nullopt;
    if (enemyNamed(cause)) return AlchemyKill{"Friendly fire!", 0xffa0e0};
    if (std::strcmp(cause, "a cave-in") == 0) return AlchemyKill{"Crushed!", 0xd0b890};
    switch (victim.lastHitType) {
    case DamageType::Fire:
        return std::strstr(cause, "Lava") ? AlchemyKill{"Melted!", 0xff7a3a} : AlchemyKill{"Immolated!", 0xffa040};
    case DamageType::Explosion: return AlchemyKill{"Blasted!", 0xffd080};
    case DamageType::Electric: return AlchemyKill{"Electrocuted!", 0xa8d4ff};
    case DamageType::Acid: return AlchemyKill{"Dissolved!", 0xa8ff60};
    case DamageType::Toxic: return AlchemyKill{"Poisoned!", 0x90e060};
    case DamageType::Ice: return AlchemyKill{"Frozen!", 0xc0f0ff};
    default: return AlchemyKill{"Crushed!", 0xd0b890};
    }
}

constexpr uint32_t kChainText = 0xFFFFFFFFu; ///< FloatText owner of the chain-reaction counter
constexpr uint32_t kChainWindow = 30;        ///< ticks within which world kills join one chain
constexpr int kAlchemyBonusesPerStage = 12;  ///< bonus gold drops a stage pays for world kills

} // namespace

void Level::castShot(const CastShot& shot, Vec2 origin, float angle, Team team, uint32_t ownerId, float damageMul) {
    const float spread = std::max(0.0f, shot.spreadDeg) * kDegToRad;
    for (const ProjectileLaunch& l : shot.projectiles) {
        float a = angle + l.angleOffset;
        if (spread > 0.0f) a += rng_.range(-spread * 0.5f, spread * 0.5f);
        launch(l, origin, a, team, ownerId, damageMul);
    }
}

void Level::launch(const ProjectileLaunch& l, Vec2 origin, float angle, Team team, uint32_t ownerId, float damageMul) {
    Projectile p;
    p.spell = l.spell;
    p.def = l.def;
    p.pos = p.prevPos = origin;
    p.vel = fromAngle(angle, l.def.speed * rng_.range(0.96f, 1.04f));
    p.team = team;
    p.ownerId = ownerId;
    p.critChance = l.critChance;
    p.bouncesLeft = l.def.bounces;
    p.payload = l.payload;
    p.damageMul = damageMul;
    p.chainsLeft = l.def.chains;

    if (p.def.flags & projflag::kFromSky) {
        // Find the ceiling above a point ahead of the caster and drop from there.
        Vec2 target = origin + fromAngle(angle, 110.0f);
        Vec2 spawn = target;
        for (int i = 0; i < 160; ++i) {
            if (isSolidForCollision(world().matAt(floorToInt(spawn.x), floorToInt(spawn.y - 1.0f)))) break;
            spawn.y -= 1.0f;
        }
        p.pos = p.prevPos = spawn;
        p.vel = (target - spawn + Vec2{0.0f, 1.0f}).normalized() * l.def.speed;
    }
    if (p.def.flags & projflag::kHitscan) {
        // Hitscan beams have no speed of their own: the velocity only carries the aim (and the
        // knockback direction), since updateProjectile never moves them.
        p.vel = fromAngle(angle, 100.0f);
        fireHitscan(*this, p, 240.0f);
        audio_.playAt(Sfx::CastLightning, origin, 0.6f);
        addFlash(p.beamEnd, 0xc8dcff, 2.5f, 80.0f, 0.2f);
    }
    addProjectile(p);
}

void Level::explode(Vec2 c, const ExplosionSpec& s) {
    World& w = world();
    if (s.team == Team::Player) knockLanterns(c, c, s.radius); // the alchemist's blasts shake lanterns loose
    const int r = static_cast<int>(std::ceil(s.radius));
    const int cx = floorToInt(c.x), cy = floorToInt(c.y);
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            const float d2 = static_cast<float>(dx * dx + dy * dy);
            if (d2 > s.radius * s.radius) continue;
            const int x = cx + dx, y = cy + dy;
            if (!w.inBounds(x, y)) continue;
            Cell cell = w.at(x, y);
            if (cell.mat == mat::Empty) continue;
            const MaterialDef& md = matDef(cell.mat);
            if (md.hardness >= 255) continue;
            const float t = std::sqrt(d2) / std::max(1.0f, s.radius);
            // Jagged edges: power falls off towards the rim with some noise.
            const float effective = static_cast<float>(s.power) * (1.0f - t * 0.45f) * rng_.range(0.85f, 1.15f);
            if (static_cast<float>(md.hardness) > effective) {
                if (s.incendiary) w.ignite(x, y);
                continue;
            }
            if (md.phase == Phase::Gas || md.phase == Phase::Fire) {
                w.clear(x, y);
                continue;
            }
            if (cell.mat == mat::GoldOre) cell = makeCell(mat::GoldDust, rng_.next());
            w.clear(x, y);
            const bool fly = md.phase == Phase::Liquid ? rng_.chance(0.5f) : rng_.chance(t > 0.55f ? 0.45f : 0.12f);
            if (fly) {
                Vec2 dir = Vec2{static_cast<float>(dx), static_cast<float>(dy)}.normalized();
                float speed = rng_.range(80.0f, 240.0f) * (1.25f - t * 0.5f) * std::min(1.5f, s.radius / 12.0f + 0.5f);
                if (s.incendiary && md.flammability > 0 && rng_.chance(0.5f)) {
                    cell.flags |= cellflag::kBurning;
                    cell.life = md.burnTime;
                }
                particles_.addMatter({static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f},
                                     dir * speed + Vec2{0.0f, -60.0f}, cell, rng_.range(1.5f, 4.0f));
            } else if (s.incendiary && rng_.chance(0.2f)) {
                w.place(x, y, mat::Fire);
            }
        }
    }
    // Scorch: the rock left around the crater is charred, darkest at the rim. Colour only (a
    // cell's var picks its shade from the palette, darkest first), so the simulation is untouched.
    const float scorchR = s.radius * 1.5f;
    const int sr = static_cast<int>(std::ceil(scorchR));
    for (int dy = -sr; dy <= sr; ++dy) {
        for (int dx = -sr; dx <= sr; ++dx) {
            const int x = cx + dx, y = cy + dy;
            const float d = std::sqrt(static_cast<float>(dx * dx + dy * dy));
            if (d > scorchR || !w.inBounds(x, y)) continue;
            Cell& cell = w.at(x, y);
            const MaterialDef& md = matDef(cell.mat);
            // Gold ore and coal keep their flecks (the top of their palettes), so they still read.
            if (md.phase != Phase::Solid || md.hardness >= 255 || cell.mat == mat::GoldOre || cell.mat == mat::Coal) continue;
            const int burn = static_cast<int>(110.0f * (1.0f - d / scorchR));
            cell.var = static_cast<uint8_t>(std::max(0, static_cast<int>(cell.var) - burn));
        }
    }
    if (s.incendiary) {
        for (int i = 0; i < r * 4; ++i) {
            Vec2 o = rng_.inCircle(s.radius * 1.3f);
            w.placeIfEmpty(cx + floorToInt(o.x), cy + floorToInt(o.y), mat::Fire);
            w.ignite(cx + floorToInt(o.x * 1.1f), cy + floorToInt(o.y * 1.1f));
        }
    }
    for (int i = 0; i < r; ++i) {
        Vec2 o = rng_.inCircle(s.radius * 0.6f);
        w.placeIfEmpty(cx + floorToInt(o.x), cy + floorToInt(o.y), mat::Smoke);
    }
    w.wakeRect(cx - r - 2, cy - r - 2, cx + r + 2, cy + r + 2);

    particles_.burst(c, 16 + r * 2, 120.0f + s.radius * 8.0f, {255, 220, 140, 255}, {255, 160, 60, 255},
                     0.35f + s.radius * 0.01f, rng_, 0.4f, partflag::kFade | partflag::kDieOnHit | partflag::kStreak);
    addFlash(c, s.incendiary ? 0xff9a40 : 0xffd090, 3.5f, s.radius * 5.0f + 30.0f, 0.3f + s.radius * 0.008f);
    addShake(std::min(0.85f, 0.12f + s.radius / 40.0f));
    if (s.radius >= 8.0f && shockwaves_.size() < 6)
        shockwaves_.push_back({c, s.radius * 3.5f, std::min(2.5f, s.radius / 9.0f), 0.0f, 0.45f});
    if (s.radius >= 18.0f) addHitStop(3);
    audio_.playAt(s.radius >= 15.0f ? Sfx::ExplosionBig : Sfx::ExplosionSmall, c, std::min(1.0f, 0.5f + s.radius / 30.0f),
                  rng_.range(0.9f, 1.1f));
    damageRadius(c, s.radius * 1.25f, s.damage, DamageType::Explosion, s.team, s.knockback, s.hurtsOwner,
                 s.cause ? s.cause : "an explosion");
    if (s.incendiary) {
        for (auto& e : enemies_)
            if (e->alive() && (e->body.pos - c).length() < s.radius && e->status.wet <= 0.0f) e->status.burning = 3.0f;
    }
}

void Level::dig(Vec2 pos, float radius, uint8_t power, bool debris) {
    World& w = world();
    const int r = static_cast<int>(std::ceil(radius));
    const int cx = floorToInt(pos.x), cy = floorToInt(pos.y);
    int removed = 0;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (static_cast<float>(dx * dx + dy * dy) > radius * radius) continue;
            const int x = cx + dx, y = cy + dy;
            if (!w.inBounds(x, y)) continue;
            Cell cell = w.at(x, y);
            if (cell.mat == mat::Empty) continue;
            const MaterialDef& md = matDef(cell.mat);
            if (md.phase == Phase::Liquid || md.phase == Phase::Gas || md.phase == Phase::Fire) continue;
            int effective = power + ((md.flags & matflag::kDiggable) ? 60 : 0);
            if (md.hardness >= 255 || md.hardness > effective) continue;
            if (cell.mat == mat::GoldOre) cell = makeCell(mat::GoldDust, rng_.next());
            w.clear(x, y);
            ++removed;
            if (cell.mat == mat::GoldDust || (debris && rng_.chance(0.12f))) {
                particles_.addMatter({static_cast<float>(x), static_cast<float>(y)},
                                     {rng_.range(-50.0f, 50.0f), rng_.range(-70.0f, -10.0f)}, cell);
            }
        }
    }
    if (removed > 0 && debris && rng_.chance(0.3f)) {
        particles_.addVisual(pos, {rng_.range(-30.0f, 30.0f), rng_.range(-40.0f, 0.0f)}, {200, 180, 140, 255},
                             {40, 30, 10, 255}, 0.2f, 0.5f);
    }
}

void Level::spawnBlob(Vec2 pos, MatId m, int amount, Vec2 vel) {
    for (int i = 0; i < amount; ++i) {
        Cell c = makeCell(m, rng_.next());
        c.flags |= cellflag::kPlayerMade;
        Vec2 v = vel + Vec2{rng_.range(-70.0f, 70.0f), rng_.range(-90.0f, 20.0f)};
        particles_.addMatter(pos + rng_.inCircle(2.0f), v, c);
    }
    if (phaseOf(m) == Phase::Liquid) audio_.playAt(Sfx::Splash, pos, 0.5f);
}

void Level::electrify(Vec2 pos, float damage, Team team) {
    World& w = world();
    // Breadth-first flood over connected conductive matter. `charged` doubles as the queue
    // (charged[head..] is the frontier) and, once done, the list of every cell charged.
    std::vector<Vec2i> charged;
    const int cx = floorToInt(pos.x), cy = floorToInt(pos.y);
    for (int dy = -3; dy <= 3; ++dy)
        for (int dx = -3; dx <= 3; ++dx) {
            int x = cx + dx, y = cy + dy;
            if (w.inBounds(x, y) && hasFlag(w.at(x, y).mat, matflag::kConductive) && !(w.at(x, y).flags & cellflag::kElectric)) {
                w.at(x, y).flags |= cellflag::kElectric;
                charged.push_back({x, y});
            }
        }
    constexpr size_t kMaxSpread = 3500; // cells spread from; the frontier left over stays charged
    constexpr int kDx[4] = {1, -1, 0, 0}, kDy[4] = {0, 0, 1, -1};
    for (size_t head = 0; head < charged.size() && head < kMaxSpread; ++head) {
        const Vec2i c = charged[head]; // a copy: push_back below may reallocate
        for (int k = 0; k < 4; ++k) {
            int x = c.x + kDx[k], y = c.y + kDy[k];
            if (!w.inBounds(x, y)) continue;
            Cell& cell = w.at(x, y);
            if ((cell.flags & cellflag::kElectric) || !hasFlag(cell.mat, matflag::kConductive)) continue;
            cell.flags |= cellflag::kElectric;
            charged.push_back({x, y});
        }
    }
    // The charge decays on its own inside the cellular automaton (see ChunkUpdater).
    for (const Vec2i& c : charged) w.wake(c.x, c.y);

    // Creatures touching charged matter are shocked.
    auto shock = [&](Actor& a) {
        if (!a.alive()) return;
        Rectf b = a.bounds();
        bool touching = false;
        for (int y = floorToInt(b.y) - 1; y <= floorToInt(b.y + b.h) && !touching; ++y)
            for (int x = floorToInt(b.x) - 1; x <= floorToInt(b.x + b.w) && !touching; ++x)
                touching = w.inBounds(x, y) && (w.at(x, y).flags & cellflag::kElectric);
        if (!touching) return;
        float dmg = damage * (a.status.wet > 0.0f ? 1.5f : 1.0f);
        a.status.shocked = 0.5f;
        a.applyDamage(*this, {dmg, DamageType::Electric, {0.0f, -60.0f}, team, false, false, "electricity"});
    };
    if (!charged.empty()) {
        if (player_->alive()) shock(*player_);
        for (size_t i = 0; i < enemies_.size(); ++i) shock(*enemies_[i]);
        for (int i = 0; i < 40; ++i) {
            const Vec2i& c = charged[rng_.next() % charged.size()];
            particles_.addVisual({static_cast<float>(c.x), static_cast<float>(c.y)},
                                 {rng_.range(-40.0f, 40.0f), rng_.range(-60.0f, 10.0f)}, {220, 235, 255, 255},
                                 {160, 200, 255, 255}, 0.25f, 0.0f);
        }
        addFlash(pos, 0xb0c8ff, 2.0f, 90.0f, 0.25f);
    }
    damageRadius(pos, 6.0f, damage * 0.5f, DamageType::Electric, team, 40.0f, false, "electricity");
}

void Level::freezeArea(Vec2 pos, float radius) {
    World& w = world();
    const int r = static_cast<int>(std::ceil(radius));
    const int cx = floorToInt(pos.x), cy = floorToInt(pos.y);
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy > r * r) continue;
            int x = cx + dx, y = cy + dy;
            if (!w.inBounds(x, y)) continue;
            Cell& c = w.at(x, y);
            switch (c.mat) {
            case mat::Water:
            case mat::Brine:
            case mat::Blood:
            case mat::Toxic:
            case mat::Slime: w.place(x, y, mat::Ice); break;
            case mat::Lava: w.place(x, y, mat::Obsidian); break;
            case mat::Fire:
            case mat::Plasma: w.clear(x, y); break;
            case mat::Steam: w.place(x, y, mat::Snow); break;
            default:
                if (c.flags & cellflag::kBurning) {
                    c.flags &= static_cast<uint8_t>(~cellflag::kBurning);
                    w.wake(x, y);
                }
                break;
            }
        }
    for (auto& e : enemies_) {
        if (!e->alive() || (e->body.pos - pos).length() > radius + 4.0f) continue;
        e->status.chilled = 3.0f;
        e->status.burning = 0.0f;
    }
    particles_.burst(pos, 14, 70.0f, {220, 245, 255, 255}, {120, 200, 255, 255}, 0.4f, rng_, 0.1f);
    audio_.playAt(Sfx::Freeze, pos, 0.5f);
}

void Level::transmuteArea(Vec2 pos, float radius) {
    World& w = world();
    const int r = static_cast<int>(std::ceil(radius));
    const int cx = floorToInt(pos.x), cy = floorToInt(pos.y);
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy > r * r) continue;
            int x = cx + dx, y = cy + dy;
            if (!w.inBounds(x, y)) continue;
            const MaterialDef& d = matDef(w.at(x, y).mat);
            if (d.phase == Phase::Solid && d.hardness < 255 && w.at(x, y).mat != mat::GoldOre && rng_.chance(0.7f))
                w.place(x, y, mat::GoldOre);
        }
    particles_.burst(pos, 30, 90.0f, {255, 230, 120, 255}, {255, 190, 60, 255}, 0.6f, rng_, 0.1f);
    addFlash(pos, 0xffd040, 2.5f, 70.0f, 0.4f);
}

void Level::damageRadius(Vec2 pos, float radius, float damage, DamageType type, Team team, float knockback,
                         bool hurtsOwnTeam, const char* cause) {
    auto hit = [&](Actor& a) {
        if (!a.alive()) return;
        Vec2 to = a.body.pos - pos;
        float d = to.length();
        if (d > radius + a.body.halfW) return;
        float f = 1.0f - 0.6f * clamp(d / std::max(1.0f, radius), 0.0f, 1.0f);
        float dmg = damage * f;
        if (team == Team::Player && a.team == Team::Player) dmg *= 0.35f; // self-inflicted blasts sting less
        Vec2 kb = (d > 0.1f ? to / d : Vec2{0.0f, -1.0f}) * knockback * f + Vec2{0.0f, -knockback * 0.35f * f};
        a.applyDamage(*this, {dmg, type, kb, team, false, false, cause});
    };
    if (player_->alive() && (team != Team::Player || hurtsOwnTeam)) hit(*player_);
    for (size_t i = 0; i < enemies_.size(); ++i) {
        Enemy& e = *enemies_[i];
        if (e.team != team || hurtsOwnTeam) hit(e);
    }
}

void Level::bleed(Vec2 pos, MatId blood, int amount, Vec2 dir) {
    if (blood == mat::Empty) return;
    Vec2 base = dir.lengthSq() > 1.0f ? dir.normalized() * 60.0f : Vec2{};
    for (int i = 0; i < amount; ++i) {
        Vec2 v = base + Vec2{rng_.range(-60.0f, 60.0f), rng_.range(-90.0f, 10.0f)};
        particles_.addMatter(pos + rng_.inCircle(2.0f), v, makeCell(blood, rng_.next()));
    }
}

const Actor* Level::nearestHostile(Vec2 pos, Team team, float range, uint32_t excludeId) const {
    const Actor* best = nullptr;
    float bestD2 = range * range;
    auto consider = [&](const Actor& a) {
        if (!a.alive() || a.team == team || a.id == excludeId) return;
        float d2 = (a.body.pos - pos).lengthSq();
        if (d2 < bestD2) {
            bestD2 = d2;
            best = &a;
        }
    };
    consider(*player_);
    // A mimic still posing as a chest is no target: homing shots, arcs and the homunculus would
    // give it away (a stray hit still wakes it).
    for (const auto& e : enemies_)
        if (!e->disguised()) consider(*e);
    return best;
}

bool Level::lineOfSight(Vec2 a, Vec2 b) const {
    Vec2 d = b - a;
    float len = d.length();
    if (len < 1.0f) return true;
    Vec2 step = d / len;
    Vec2 p = a;
    for (float t = 0.0f; t < len; t += 1.0f) {
        if (isSolidForCollision(world().matAt(floorToInt(p.x), floorToInt(p.y)))) return false;
        p += step;
    }
    return true;
}

void Level::dropLoot(Vec2 pos, int tier, bool chest) {
    const bool lucky = run_.hasPerk(PerkId::LuckyDay);
    int coins = chest ? rng_.rangeInt(3, 6) : 0;
    for (int i = 0; i < coins; ++i) {
        int value = (4 + rng_.rangeInt(0, 8)) * (tier + 1);
        pickups_.push_back(makeGold(pos, value, {rng_.range(-80.0f, 80.0f), rng_.range(-160.0f, -80.0f)}));
    }
    if (!chest) return;
    int rolls = lucky ? 2 : 1;
    const int lootTier = std::min(kMaxTier, tier + (lucky ? 1 : 0));
    for (int i = 0; i < rolls; ++i) {
        float r = rng_.uniform();
        Pickup p;
        if (r < 0.05f) {
            p = makeSpellPickup(pos, randomRareSpell(lootTier, rng_));
            p.rare = true;
        } else if (r < 0.45f) p = makeSpellPickup(pos, randomFind(lootTier, rng_));
        else if (r < 0.62f) p = makeWandPickup(pos, makeRandomWand(lootTier, rng_));
        else if (r < 0.88f) p = makePotion(pos, randomPotionMaterial(lootTier, rng_), 800);
        else p = makeHeart(pos);
        p.body.vel = {rng_.range(-60.0f, 60.0f), -140.0f};
        pickups_.push_back(std::move(p));
    }
}

void Level::shatter(const Enemy& e) {
    // The creature breaks into its own pixels, flung away from the alchemist; they bounce and
    // fade. Cosmetic randomness only, so seeded runs are untouched.
    constexpr size_t kMaxShards = 600;
    const Enemy::SpritePose pose = e.pose(time_);
    const Vec2 away = (e.body.pos - player_->body.pos).normalized();
    const Rgba tint{pose.tint.r, pose.tint.g, pose.tint.b, 255};
    // A worm's body breaks apart segment by segment along with its head, sharing the budget.
    const size_t budget = kMaxShards / (1 + e.segments.size());
    shatterPixels(spriteFrame(pose.sprite, pose.frame), pose.topLeft, static_cast<float>(pose.scale), pose.flip, e.body.pos, tint, away,
                  budget);
    const SpriteFrame& segment = spriteFrame(SpriteId::WormBody, 0);
    for (const Vec2& c : e.segments)
        shatterPixels(segment, c - Vec2{static_cast<float>(segment.w) * 0.5f, static_cast<float>(segment.h) * 0.5f}, 1.0f, false, c, tint,
                      away, budget);
}

void Level::shatterPixels(const SpriteFrame& frame, Vec2 topLeft, float scale, bool flip, Vec2 centre, Rgba tint, Vec2 away,
                          size_t budget) {
    if (frame.pixels.empty() || budget == 0) return;
    const size_t stride = frame.pixels.size() / budget + 1;
    for (size_t i = 0; i < frame.pixels.size(); i += stride) {
        const SpritePixel& px = frame.pixels[i];
        const float lx = static_cast<float>(flip ? frame.w - 1 - px.x : px.x);
        const Vec2 at = topLeft + Vec2{(lx + 0.5f) * scale, (static_cast<float>(px.y) + 0.5f) * scale};
        const Vec2 vel = away * fxRng_.range(40.0f, 110.0f) + (at - centre) * fxRng_.range(3.0f, 8.0f) +
                         Vec2{fxRng_.range(-25.0f, 25.0f), fxRng_.range(-90.0f, -30.0f)};
        const Rgba col{static_cast<uint8_t>(px.color.r * tint.r / 255), static_cast<uint8_t>(px.color.g * tint.g / 255),
                       static_cast<uint8_t>(px.color.b * tint.b / 255), 255};
        particles_.addVisual(at, vel, col, px.emissive ? col : Rgba{0, 0, 0, 0}, fxRng_.range(0.6f, 1.4f), 1.0f, partflag::kFade);
    }
}

void Level::creditAlchemy(const Enemy& e) {
    const std::optional<AlchemyKill> kind = alchemyOf(e);
    if (!kind) return;
    run_.stats.alchemicalKills++;
    // Kills close together run into one chain reaction with a single growing counter.
    alchemyChain_ = ticks_ - alchemyChainTick_ <= kChainWindow && alchemyChain_ > 0 ? alchemyChain_ + 1 : 1;
    alchemyChainTick_ = ticks_;
    if (alchemyChain_ >= 3) earnFeat(FeatId::ChainReaction);
    const Vec2 at = e.body.pos - Vec2{0.0f, e.body.halfH + 14.0f};
    if (alchemyChain_ == 1) {
        addFloatText(at, kind->label, kind->color, 1.1f);
    } else {
        FloatText& chain = taggedFloatText(kChainText, at);
        chain.text = "Chain reaction x" + std::to_string(alchemyChain_);
        chain.color = 0xffd870;
        chain.scale = 1.2f;
        chain.life = 1.4f;
        chain.pop = 1.0f;
        chain.pos = at;
    }
    audio_.playAt(Sfx::Coin, e.body.pos, 0.45f, 1.3f + 0.08f * static_cast<float>(std::min(alchemyChain_, 6)));
    // The world's work pays a little extra gold, within a limit per stage.
    if (alchemyBonuses_ < kAlchemyBonusesPerStage) {
        ++alchemyBonuses_;
        const int tier = std::min(kMaxTier, layout_.levelIndex);
        pickups_.push_back(makeGold(e.body.pos, 4 * (tier + 1), {fxRng_.range(-50.0f, 50.0f), fxRng_.range(-150.0f, -90.0f)}));
    }
}

void Level::onEnemyKilled(Enemy& e) {
    const EnemyDef& d = e.def();
    if (e.team == Team::Monster) {
        creditAlchemy(e);
        run_.stats.kills++;
        run_.killsByType[static_cast<size_t>(e.type())]++;
        earnFeat(FeatId::FirstBlood);
        if (e.elite()) earnFeat(FeatId::EliteHunter);
        switch (e.type()) {
        case EnemyType::BlackToad: earnFeat(FeatId::BlackToad); break;
        case EnemyType::WhiteWraith: earnFeat(FeatId::WhiteWraith); break;
        case EnemyType::MineKing: earnFeat(FeatId::MineKing); break;
        case EnemyType::RedColossus: earnFeat(FeatId::RedColossus); break;
        case EnemyType::Mimic:
            // The treasure it posed as is real.
            earnFeat(FeatId::Mimic);
            dropLoot(e.body.pos - Vec2{0.0f, 4.0f}, std::min(kMaxTier, layout_.levelIndex), true);
            break;
        default: break;
        }
        // Three kills inside a second.
        recentKills_[killRing_] = time_;
        killRing_ = (killRing_ + 1) % recentKills_.size();
        if (time_ - recentKills_[killRing_] <= 1.0f) earnFeat(FeatId::Carnage);
        if (run_.hasPerk(PerkId::Vampirism) && player_->alive()) {
            player_->hp = std::min(player_->maxHp, player_->hp + 4.0f * run_.perkCount(PerkId::Vampirism));
            run_.hp = player_->hp;
        }
        int total = static_cast<int>(static_cast<float>(d.gold) * (1.0f + 0.3f * static_cast<float>(layout_.levelIndex)));
        if (e.affix() == Affix::Gilded) total *= 3;
        int pieces = std::min(4, 1 + total / 15);
        for (int i = 0; i < pieces && total > 0; ++i) {
            int v = i == pieces - 1 ? total : total / pieces;
            total -= v;
            pickups_.push_back(makeGold(e.body.pos, v, {rng_.range(-70.0f, 70.0f), rng_.range(-150.0f, -70.0f)}));
        }
        if (rng_.chance(0.06f)) pickups_.push_back(makeHeart(e.body.pos));
        if (d.guardian) {
            // A guardian's essence is the alchemist's, if they lived to claim it: a choice of two
            // perks. Gathered from all four keepers in one run, the essences make the Quintessence.
            if (player_->alive()) {
                offerEssence(e);
                if (const int stage = guardianStage(e.type()); stage >= 0) run_.essences |= static_cast<uint8_t>(1u << stage);
                const int held = std::popcount(static_cast<unsigned>(run_.essences));
                addFloatText(e.body.pos - Vec2{0.0f, e.body.halfH + 18.0f},
                             run_.quintessence() ? std::string("The Quintessence is yours!")
                                                     : "Essence " + std::to_string(held) + " of " + std::to_string(kGuardianCount), 0xfff0c0,
                             1.2f);
            }
            addFloatText(e.body.pos - Vec2{0.0f, e.body.halfH + 8.0f}, std::string(d.name) + " falls!", 0xffd27a, 1.4f);
            addFlash(e.body.pos, 0xffe0a0, 2.5f, 120.0f, 0.4f);
            audio_.play(Sfx::Roar, 0.9f, 0.6f);
        }
        if (e.elite()) {
            // Elites carry treasure: a spell or a potion, plus extra gold. A Burning elite
            // always carries fire.
            const int tier = std::min(kMaxTier, layout_.levelIndex + 1);
            static constexpr SpellId kFireSpells[] = {SpellId::FireBolt, SpellId::Fireball, SpellId::Incendiary, SpellId::FireTrail};
            Pickup loot = e.affix() == Affix::Burning
                              ? makeSpellPickup(e.body.pos, rng_.pick(kFireSpells))
                          : rng_.chance(0.6f) ? makeSpellPickup(e.body.pos, randomSpell(tier, rng_))
                                              : makePotion(e.body.pos, randomPotionMaterial(tier, rng_), 700);
            loot.body.vel = {rng_.range(-50.0f, 50.0f), -150.0f};
            pickups_.push_back(std::move(loot));
            pickups_.push_back(makeGold(e.body.pos, d.gold * 2, {rng_.range(-60.0f, 60.0f), -120.0f}));
            addFloatText(e.body.pos - Vec2{0.0f, 10.0f}, std::string(affixName(e.affix())) + " " + d.name + " slain!", 0xffc850, 1.2f);
        }
        if (run_.hasPerk(PerkId::VolatileFoes)) {
            ExplosionSpec s;
            s.radius = 10.0f;
            s.damage = 25.0f;
            s.team = Team::Player;
            s.hurtsOwner = false;
            s.cause = "a volatile corpse";
            explode(e.body.pos, s);
        }
    }
    bleed(e.body.pos, d.blood, 25 + static_cast<int>(d.hp / 4.0f), {0.0f, -40.0f});
    for (int i = 0; i < 6; ++i)
        particles_.addMatter(e.body.pos + rng_.inCircle(3.0f), {rng_.range(-90.0f, 90.0f), rng_.range(-140.0f, -40.0f)},
                             makeCell(d.blood == mat::Ichor ? mat::Ash : mat::Flesh, rng_.next()));
    particles_.burst(e.body.pos, 18, 110.0f, hexRgba(d.glow ? d.glow : 0xffffff), hexRgba(d.glow ? d.glow : 0x606060), 0.5f, rng_);
    shatter(e);
    audio_.playAt(Sfx::EnemyDie, e.body.pos, 0.8f, rng_.range(0.85f, 1.15f));
    // Weighty kills land with a beat of hit-stop; small fry do not, so a sweep of kills
    // never stutters.
    if (e.team == Team::Monster && (e.elite() || e.maxHp >= 30.0f)) {
        addHitStop(d.guardian ? 4 : (e.elite() ? 2 : 1));
        addShake(d.guardian ? 0.7f : 0.14f);
    } else {
        addShake(0.08f);
    }
}

void Level::spawnAmbience() {
    struct Ambience {
        float rate;        ///< particles per tick
        Rgba color, glow;
        float life, gravity, drag;
        Vec2 velMin, velMax;
    };
    static constexpr Ambience kDust{0.8f, {150, 140, 125, 80}, {0, 0, 0, 0}, 4.0f, 0.004f, 1.5f, {-5, -4}, {5, 3}};
    static constexpr Ambience kGlitter{1.2f, {225, 238, 255, 170}, {30, 45, 70, 0}, 5.0f, 0.04f, 2.5f, {-8, 2}, {8, 8}};
    static constexpr Ambience kSpores{0.9f, {230, 210, 90, 140}, {70, 60, 10, 0}, 5.0f, -0.02f, 1.5f, {-6, -6}, {6, 0}};
    static constexpr Ambience kEmbers{1.6f, {255, 150, 60, 230}, {255, 110, 30, 0}, 3.0f, -0.05f, 1.0f, {-12, -25}, {12, -8}};
    static constexpr Ambience kGold{1.2f, {255, 210, 120, 220}, {220, 160, 50, 0}, 3.0f, -0.04f, 1.0f, {-10, -20}, {10, -6}};
    static constexpr Ambience kSparkles{0.5f, {205, 170, 255, 200}, {120, 80, 210, 0}, 2.5f, -0.02f, 1.2f, {-6, -8}, {6, 2}};
    static constexpr Ambience kFireflies{0.15f, {210, 255, 130, 220}, {120, 210, 60, 0}, 4.0f, 0.0f, 0.8f, {-10, -6}, {10, 6}};

    const Ambience* a = nullptr;
    switch (layout_.biome) {
    case BiomeId::Nigredo: a = &kDust; break;
    case BiomeId::Albedo: a = &kGlitter; break;
    case BiomeId::Citrinitas: a = &kSpores; break;
    case BiomeId::Rubedo: a = &kEmbers; break;
    case BiomeId::Athanor: a = fxRng_.chance(0.5f) ? &kEmbers : &kGold; break;
    case BiomeId::Sanctum: a = &kSparkles; break;
    default: return;
    }
    const World& w = world();
    const Vec2 around = player_->body.pos;
    auto emit = [&](const Ambience& amb, Vec2 p) {
        const Vec2 v{fxRng_.range(amb.velMin.x, amb.velMax.x), fxRng_.range(amb.velMin.y, amb.velMax.y)};
        particles_.addVisual(p, v, amb.color, amb.glow, amb.life * fxRng_.range(0.7f, 1.2f), amb.gravity,
                             partflag::kFade | partflag::kDieOnHit);
    };
    // Spread over roughly the visible area, only into open air.
    for (float budget = a->rate; budget > 0.0f; budget -= 1.0f) {
        if (budget < 1.0f && !fxRng_.chance(budget)) break;
        const Vec2 p{around.x + fxRng_.range(-250.0f, 250.0f), around.y + fxRng_.range(-150.0f, 130.0f)};
        const int x = floorToInt(p.x), y = floorToInt(p.y);
        if (!w.inBounds(x, y) || w.at(x, y).mat != mat::Empty) continue;
        if (layout_.surfaceY > 0 && y < layout_.skyline[static_cast<size_t>(x)]) continue; // the sky gets fireflies instead
        emit(*a, p);
    }
    // Fireflies over the mountainside on the first stage.
    if (layout_.surfaceY > 0 && fxRng_.chance(kFireflies.rate)) {
        const int x = floorToInt(around.x + fxRng_.range(-250.0f, 250.0f));
        if (x > 0 && x < w.width()) {
            const int ground = layout_.skyline[static_cast<size_t>(x)];
            const int y = ground - fxRng_.rangeInt(4, 50);
            if (w.inBounds(x, y) && w.at(x, y).mat == mat::Empty) emit(kFireflies, {static_cast<float>(x), static_cast<float>(y)});
        }
    }
}

void Level::updateLanterns(float dt) {
    // A lantern whose ceiling is blasted away drops, and shatters into burning oil.
    World& w = world();
    for (Lantern& l : lanterns_) {
        // Only matter that can bear weight holds a lantern up or stops its fall: not smoke or fire.
        auto bearing = [&](int cx, int cy) {
            const Phase p = phaseOf(w.at(cx, cy).mat);
            return p == Phase::Solid || p == Phase::Powder || p == Phase::Liquid;
        };
        const int x = floorToInt(l.pos.x);
        if (l.fallSpeed <= 0.0f) {
            if (ticks_ % 10 != 0) continue;
            const int ay = floorToInt(l.pos.y) - 5;
            if (!w.inBounds(x, ay) || bearing(x, ay)) continue;
        }
        const int fromY = floorToInt(l.pos.y) + 3;
        l.fallSpeed = std::min(l.fallSpeed + kGravity * dt, 420.0f);
        l.pos.y += l.fallSpeed * dt;
        // Sweep every row passed this tick so a fast lantern cannot slip through a thin floor.
        int y = floorToInt(l.pos.y) + 3;
        bool landed = false;
        for (int ry = fromY + 1; ry <= y; ++ry) {
            if (!w.inBounds(x, ry)) break;
            if (bearing(x, ry)) {
                y = ry;
                l.pos.y = static_cast<float>(ry - 3);
                landed = true;
                break;
            }
        }
        if (!w.inBounds(x, y + 1)) {
            l.shattered = true;
            continue;
        }
        if (!landed) continue;
        l.shattered = true;
        const Vec2 at{l.pos.x, l.pos.y + 1.0f};
        spawnBlob(at, mat::Oil, 10, {0.0f, -30.0f});
        for (int i = 0; i < 8; ++i) w.placeIfEmpty(x + rng_.rangeInt(-3, 3), y - rng_.rangeInt(1, 4), mat::Fire);
        particles_.burst(at, 10, 70.0f, {255, 220, 150, 255}, {255, 160, 60, 200}, 0.5f, rng_);
        addFlash(at, 0xffb050, 1.4f, 60.0f, 0.25f);
        audio_.playAt(Sfx::Hit, at, 0.5f, 1.7f);
    }
    lanterns_.erase(std::remove_if(lanterns_.begin(), lanterns_.end(), [](const Lantern& l) { return l.shattered; }),
                    lanterns_.end());
}

void Level::knockLanterns(Vec2 a, Vec2 b, float radius) {
    const Vec2 ab = b - a;
    const float lenSq = std::max(ab.lengthSq(), 1e-6f);
    for (Lantern& l : lanterns_) {
        if (l.fallSpeed > 0.0f || l.shattered) continue;
        // Test the top, middle and bottom of the lantern's body against the segment.
        for (const float dy : {-1.0f, 2.0f, 5.0f}) {
            const Vec2 q = l.pos + Vec2{0.0f, dy};
            const float t = clamp(dot(q - a, ab) / lenSq, 0.0f, 1.0f);
            if ((a + ab * t - q).lengthSq() > radius * radius) continue;
            l.fallSpeed = 1.0f;
            audio_.playAt(Sfx::Hit, q, 0.4f, 2.1f);
            earnFeat(FeatId::Lanternfall);
            break;
        }
    }
}

void Level::updateTrees() {
    // A tree keeps only what is still joined to its root; the rest crumbles into falling
    // litter. Trees are recounted every 15 ticks and pruned only when they have lost cells.
    if (layout_.trees.empty() || ticks_ % 15 != 7) return;
    World& w = world();
    const auto width = static_cast<uint32_t>(w.width());
    if (treeCellCounts_.size() != layout_.trees.size()) {
        treeCellCounts_.clear();
        for (const TreeShape& tree : layout_.trees) treeCellCounts_.push_back(tree.cells.size());
    }
    for (size_t t = 0; t < layout_.trees.size(); ++t) {
        TreeShape& tree = layout_.trees[t];
        const auto alive = static_cast<size_t>(std::count_if(tree.cells.begin(), tree.cells.end(), [&](uint32_t cell) {
            const MatId m = w.at(static_cast<int>(cell % width), static_cast<int>(cell / width)).mat;
            return m == mat::Wood || m == mat::Leaves;
        }));
        if (alive == treeCellCounts_[t]) continue;
        const std::vector<uint32_t> loose = pruneTree(w, tree);
        for (const uint32_t cell : loose) w.place(static_cast<int>(cell % width), static_cast<int>(cell / width), mat::Litter);
        // A whole crown coming down is heard, not just seen.
        if (loose.size() > 40)
            audio_.playAt(Sfx::Land, {static_cast<float>(tree.rootX), static_cast<float>(tree.rootY) - 20.0f}, 0.8f, 0.55f);
        treeCellCounts_[t] = tree.cells.size();
    }
}

} // namespace pm
