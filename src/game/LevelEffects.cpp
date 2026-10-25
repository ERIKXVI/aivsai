// Level effects API: casting, explosions, digging and the other ways gameplay reshapes the
// simulated world, plus props that come down once it is reshaped beneath them.

#include <algorithm>
#include <cmath>
#include <queue>

#include "core/Timing.hpp"
#include "game/Level.hpp"

namespace pm {

namespace {

constexpr float kDegToRad = kPi / 180.0f;

Rgba hexRgba(uint32_t hex, uint8_t a = 255) {
    return {static_cast<uint8_t>((hex >> 16) & 0xFF), static_cast<uint8_t>((hex >> 8) & 0xFF),
            static_cast<uint8_t>(hex & 0xFF), a};
}

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
    if (l.spell == SpellId::ArcBolt) p.chainsLeft = 3;

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
                     0.35f + s.radius * 0.01f, rng_, 0.4f);
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
    std::queue<Vec2i> open;
    std::vector<Vec2i> visited;
    const int cx = floorToInt(pos.x), cy = floorToInt(pos.y);
    for (int dy = -3; dy <= 3; ++dy)
        for (int dx = -3; dx <= 3; ++dx) {
            int x = cx + dx, y = cy + dy;
            if (w.inBounds(x, y) && hasFlag(w.at(x, y).mat, matflag::kConductive) && !(w.at(x, y).flags & cellflag::kElectric)) {
                w.at(x, y).flags |= cellflag::kElectric;
                open.push({x, y});
            }
        }
    constexpr int kMaxCells = 3500;
    constexpr int kDx[4] = {1, -1, 0, 0}, kDy[4] = {0, 0, 1, -1};
    while (!open.empty() && static_cast<int>(visited.size()) < kMaxCells) {
        Vec2i c = open.front();
        open.pop();
        visited.push_back(c);
        for (int k = 0; k < 4; ++k) {
            int x = c.x + kDx[k], y = c.y + kDy[k];
            if (!w.inBounds(x, y)) continue;
            Cell& cell = w.at(x, y);
            if ((cell.flags & cellflag::kElectric) || !hasFlag(cell.mat, matflag::kConductive)) continue;
            cell.flags |= cellflag::kElectric;
            open.push({x, y});
        }
    }
    while (!open.empty()) {
        visited.push_back(open.front());
        open.pop();
    }
    // The charge decays on its own inside the cellular automaton (see ChunkUpdater).
    for (const Vec2i& c : visited) w.wake(c.x, c.y);

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
    if (!visited.empty()) {
        if (player_->alive()) shock(*player_);
        for (size_t i = 0; i < enemies_.size(); ++i) shock(*enemies_[i]);
        for (int i = 0; i < 40; ++i) {
            const Vec2i& c = visited[rng_.next() % visited.size()];
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

Actor* Level::nearestHostile(Vec2 pos, Team team, float range, uint32_t excludeId) {
    Actor* best = nullptr;
    float bestD2 = range * range;
    auto consider = [&](Actor& a) {
        if (!a.alive() || a.team == team || a.id == excludeId) return;
        float d2 = (a.body.pos - pos).lengthSq();
        if (d2 < bestD2) {
            bestD2 = d2;
            best = &a;
        }
    };
    consider(*player_);
    for (auto& e : enemies_) consider(*e);
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
    const int lootTier = std::min(4, tier + (lucky ? 1 : 0));
    for (int i = 0; i < rolls; ++i) {
        float r = rng_.uniform();
        Pickup p;
        if (r < 0.45f) p = makeSpellPickup(pos, randomSpell(lootTier, rng_));
        else if (r < 0.62f) p = makeWandPickup(pos, makeRandomWand(lootTier, rng_));
        else if (r < 0.88f) p = makePotion(pos, randomPotionMaterial(lootTier, rng_), 800);
        else p = makeHeart(pos);
        p.body.vel = {rng_.range(-60.0f, 60.0f), -140.0f};
        pickups_.push_back(std::move(p));
    }
}

void Level::onEnemyKilled(Enemy& e) {
    const EnemyDef& d = e.def();
    if (e.team == Team::Monster) {
        run_.stats.kills++;
        run_.killsByType[static_cast<size_t>(e.type())]++;
        if (run_.hasPerk(PerkId::Vampirism) && player_->alive()) {
            player_->hp = std::min(player_->maxHp, player_->hp + 4.0f * run_.perkCount(PerkId::Vampirism));
            run_.hp = player_->hp;
        }
        int total = static_cast<int>(static_cast<float>(d.gold) * (1.0f + 0.3f * static_cast<float>(layout_.levelIndex)));
        int pieces = std::min(4, 1 + total / 15);
        for (int i = 0; i < pieces && total > 0; ++i) {
            int v = i == pieces - 1 ? total : total / pieces;
            total -= v;
            pickups_.push_back(makeGold(e.body.pos, v, {rng_.range(-70.0f, 70.0f), rng_.range(-150.0f, -70.0f)}));
        }
        if (rng_.chance(0.06f)) pickups_.push_back(makeHeart(e.body.pos));
        if (e.elite()) {
            // Elites carry treasure: a spell or a potion, plus extra gold.
            const int tier = std::min(4, layout_.levelIndex + 1);
            Pickup loot = rng_.chance(0.6f) ? makeSpellPickup(e.body.pos, randomSpell(tier, rng_))
                                            : makePotion(e.body.pos, randomPotionMaterial(tier, rng_), 700);
            loot.body.vel = {rng_.range(-50.0f, 50.0f), -150.0f};
            pickups_.push_back(std::move(loot));
            pickups_.push_back(makeGold(e.body.pos, d.gold * 2, {rng_.range(-60.0f, 60.0f), -120.0f}));
            addFloatText(e.body.pos - Vec2{0.0f, 10.0f}, "Elite slain!", 0xffc850, 1.2f);
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
    audio_.playAt(Sfx::EnemyDie, e.body.pos, 0.8f, rng_.range(0.85f, 1.15f));
    addShake(0.08f);
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
