#include "game/StageEvents.hpp"

#include <algorithm>
#include <cmath>

#include "core/Timing.hpp"
#include "game/Level.hpp"

namespace pm {

namespace {

constexpr int kMaxEvents = 3;         ///< per stage
constexpr int kRestlessMaxEvents = 5; ///< per stage under the Restless Mountain ordeal
constexpr float kWarnTime = 1.6f; ///< the telegraph before an event lands
// How much ceiling lets go (half-widths of the ellipse), and how far from a vault it must stay.
constexpr int kCaveInRadiusX = 12, kCaveInRadiusY = 6;
constexpr int kGoldFallRadiusX = 7, kGoldFallRadiusY = 4;
constexpr float kVaultMargin = 4.0f;

/// Row of a collapse ellipse's centre: its lower edge bites just into the ceiling's underside.
int collapseCentreY(int ceilingY, int radiusY) { return ceilingY - radiusY + 2; }

bool insideVault(const Level& level, Vec2 p, float margin) {
    for (const Rectf& v : level.layout().vaults)
        if (p.x > v.x - margin && p.x < v.x + v.w + margin && p.y > v.y - margin && p.y < v.y + v.h + margin) return true;
    return false;
}

bool isOpen(const World& w, int x, int y) { return w.inBounds(x, y) && isVacant(w.at(x, y).mat); }

/// What natural stone crumbles into when the mountain lets go of it; Empty for anything that
/// holds (worked brick, metal, runestone, wood, crystal) or is not stone at all.
MatId debrisOf(MatId m) {
    switch (m) {
    case mat::Rock:
    case mat::Soil:
    case mat::Basalt:
    case mat::Coal:
    case mat::Obsidian: return mat::Gravel;
    case mat::Ice: return mat::Snow;
    case mat::SaltRock: return mat::Salt;
    case mat::Sulphite: return mat::Sulphur;
    case mat::GoldOre: return mat::GoldDust;
    case mat::Moss:
    case mat::Fungus: return mat::Litter;
    default: return mat::Empty;
    }
}

/// Whether a stretch of ceiling can give way: the collapse ellipse must be mostly natural
/// stone (or the fall is a trickle), and neither it nor the cell around it may hold liquid - a
/// ceiling holding up a pool would pour it all down with only a rock-fall's warning.
bool sturdyCeiling(const World& w, int cx, int cy, int radiusX, int radiusY) {
    const int rx = radiusX + 1, ry = radiusY + 1;
    int cells = 0, stone = 0;
    for (int dy = -ry; dy <= ry; ++dy)
        for (int dx = -rx; dx <= rx; ++dx) {
            const float nx = static_cast<float>(dx) / static_cast<float>(rx), ny = static_cast<float>(dy) / static_cast<float>(ry);
            if (nx * nx + ny * ny > 1.0f) continue;
            const MatId m = w.matAt(cx + dx, cy + dy);
            if (matDef(m).phase == Phase::Liquid) return false;
            ++cells;
            stone += debrisOf(m) != mat::Empty ? 1 : 0;
        }
    return stone * 2 >= cells;
}

/// Natural ceiling above the alchemist with a long, clear fall beneath it, holding up no
/// pool, whose fall lands in the alchemist's view.
bool findCeiling(const Level& level, Rng& rng, Vec2& site) {
    const World& w = level.world();
    const Vec2 me = level.player().body.pos;
    const int top = floorToInt(me.y);
    for (int attempt = 0; attempt < 14; ++attempt) {
        const int x = floorToInt(me.x + rng.range(-34.0f, 34.0f));
        int y = top - 8;
        if (!isOpen(w, x, y)) continue;
        while (y > top - 80 && isOpen(w, x, y)) --y;
        const int drop = top - y;
        if (drop < 22 || drop >= 80) continue; // too low to fall far, or open sky
        if (debrisOf(w.matAt(x, y)) == mat::Empty || debrisOf(w.matAt(x, y - 4)) == mat::Empty) continue;
        if (insideVault(level, {static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f}, 2.0f * kVaultMargin)) continue;
        if (!sturdyCeiling(w, x, collapseCentreY(y, kCaveInRadiusY), kCaveInRadiusX, kCaveInRadiusY)) continue;
        // The rock must land where the alchemist can see it (not down a shaft behind a wall).
        int floor = y + 1;
        while (floor < y + 90 && isOpen(w, x, floor)) ++floor;
        if (!level.lineOfSight({static_cast<float>(x) + 0.5f, static_cast<float>(floor) - 4.0f}, me)) continue;
        site = {static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
        return true;
    }
    return false;
}

/// A natural wall off to one side, thick enough to have held something back. `dirX` is set to
/// point from the wall back towards the alchemist.
bool findWall(const Level& level, Rng& rng, Vec2& site, float& dirX) {
    const World& w = level.world();
    const Vec2 me = level.player().body.pos;
    for (int attempt = 0; attempt < 16; ++attempt) {
        const int side = rng.chance(0.5f) ? 1 : -1;
        const int y = floorToInt(me.y + rng.range(-24.0f, 4.0f));
        const int x0 = floorToInt(me.x);
        int x = x0;
        if (!isOpen(w, x, y)) continue;
        while (std::abs(x - x0) < 80 && isOpen(w, x, y)) x += side;
        if (std::abs(x - x0) < 24 || std::abs(x - x0) >= 80) continue; // not on top of them, but within reach
        bool thick = true;
        for (int k = 0; k < 5 && thick; ++k) thick = debrisOf(w.matAt(x + side * k, y)) != mat::Empty;
        const Vec2 at{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
        if (!thick || insideVault(level, at, 8.0f)) continue;
        site = at;
        dirX = static_cast<float>(-side);
        return true;
    }
    return false;
}

/// Open air in view of the alchemist but well away, for a swarm to come from.
bool findRoost(const Level& level, Rng& rng, Vec2& site) {
    const World& w = level.world();
    const Vec2 me = level.player().body.pos;
    for (int attempt = 0; attempt < 30; ++attempt) {
        const Vec2 at = me + fromAngle(rng.range(0.0f, kTau), rng.range(110.0f, 160.0f));
        bool clear = true;
        for (int dy = -5; dy <= 5 && clear; ++dy)
            for (int dx = -5; dx <= 5 && clear; ++dx) clear = isOpen(w, floorToInt(at.x) + dx, floorToInt(at.y) + dy);
        if (!clear || insideVault(level, at, 10.0f) || !level.lineOfSight(at, me)) continue;
        site = at;
        return true;
    }
    return false;
}

/// What a breach lets loose on each stage (a Flooded stage always floods).
MatId breachMatter(const Level& level, Rng& rng) {
    if (level.layout().omen == Omen::Flooded) return mat::Water;
    switch (level.biome()) {
    case BiomeId::Nigredo: return rng.chance(0.7f) ? mat::Water : mat::Oil;
    case BiomeId::Albedo: return mat::Brine;
    case BiomeId::Citrinitas: return rng.chance(0.6f) ? mat::Oil : mat::Toxic;
    case BiomeId::Rubedo: return mat::Lava;
    default: return mat::Water;
    }
}

} // namespace

void StageEvents::begin(const Level& level, Rng& rng) {
    *this = StageEvents{};
    enabled_ = !level.isSanctum() && level.biome() != BiomeId::Athanor;
    const bool restless = level.run().under(Ordeal::RestlessMountain);
    pace_ = restless ? 0.5f : 1.0f;
    maxEvents_ = restless ? kRestlessMaxEvents : kMaxEvents;
    countdown_ = rng.range(50.0f, 90.0f) * pace_;
}

StageEventKind StageEvents::pickKind(const Level& level, Rng& rng) const {
    // Weights per stage, in StageEventKind order: cave-in, breach, gas seep, swarm, gold fall.
    static constexpr float kWeights[4][static_cast<int>(StageEventKind::Count)] = {
        {3.0f, 2.0f, 2.0f, 1.5f, 1.5f}, // Nigredo: soft stone, black water and marsh gas
        {3.0f, 2.0f, 0.5f, 1.5f, 1.5f}, // Albedo: ice gives way, brine breaks through
        {2.0f, 1.5f, 3.0f, 1.5f, 2.0f}, // Citrinitas: the mines leak gas and hide gold
        {2.0f, 2.5f, 1.0f, 1.5f, 1.5f}, // Rubedo: the walls hold back lava
    };
    float w[static_cast<int>(StageEventKind::Count)];
    std::copy(std::begin(kWeights[clamp(static_cast<int>(level.biome()), 0, 3)]),
              std::end(kWeights[clamp(static_cast<int>(level.biome()), 0, 3)]), w);
    auto boost = [&](StageEventKind k) { w[static_cast<int>(k)] *= 2.0f; };
    switch (level.layout().omen) {
    case Omen::Flooded: boost(StageEventKind::Breach); break;
    case Omen::Volatile: boost(StageEventKind::GasSeep); break;
    case Omen::Gilded: boost(StageEventKind::GoldFall); break;
    case Omen::Teeming: boost(StageEventKind::Swarm); break;
    default: break;
    }
    float total = 0.0f;
    for (float v : w) total += v;
    float r = rng.uniform() * total;
    for (int i = 0; i < static_cast<int>(StageEventKind::Count); ++i) {
        if (r < w[i]) return static_cast<StageEventKind>(i);
        r -= w[i];
    }
    return StageEventKind::CaveIn;
}

void StageEvents::tick(Level& level) {
    if (phase_ == Phase::Warning) {
        timer_ -= kTickDt;
        warn(level);
        if (timer_ <= 0.0f) {
            phase_ = Phase::Unfolding;
            unfold(level);
        }
        return;
    }
    if (phase_ == Phase::Unfolding) {
        unfold(level);
        return;
    }
    if (!enabled_ || fired_ >= maxEvents_) return;
    countdown_ -= kTickDt;
    if (countdown_ > 0.0f) return;
    // Hold off while the alchemist has enough on: a guardian or the Rebis, a vault, death.
    const Player& pl = level.player();
    if (!pl.alive() || level.bossBarTarget() || insideVault(level, pl.body.pos, 0.0f)) {
        countdown_ = 5.0f;
        return;
    }
    Rng& rng = level.rng();
    for (int attempt = 0; attempt < 3; ++attempt)
        if (start(level, pickKind(level, rng))) return;
    countdown_ = 8.0f; // nowhere suitable here; try again further on
}

bool StageEvents::trigger(Level& level, StageEventKind kind) { return phase_ == Phase::Idle && start(level, kind); }

bool StageEvents::start(Level& level, StageEventKind kind) {
    Rng& rng = level.rng();
    bool found = false;
    switch (kind) {
    case StageEventKind::CaveIn:
    case StageEventKind::GoldFall: found = findCeiling(level, rng, site_); break;
    case StageEventKind::Breach:
    case StageEventKind::GasSeep: found = findWall(level, rng, site_, dirX_); break;
    case StageEventKind::Swarm: found = findRoost(level, rng, site_); break;
    case StageEventKind::Count: break;
    }
    if (!found) return false;
    kind_ = kind;
    phase_ = Phase::Warning;
    timer_ = kind == StageEventKind::Swarm ? 1.0f : kWarnTime;
    elapsed_ = 0.0f;
    released_ = false;
    ++fired_;
    countdown_ = rng.range(75.0f, 130.0f) * pace_;
    matter_ = mat::Empty;
    if (kind == StageEventKind::Breach) matter_ = breachMatter(level, rng);
    else if (kind == StageEventKind::GasSeep) matter_ = mat::Methane;

    Audio& audio = level.audio();
    switch (kind) {
    case StageEventKind::CaveIn:
        notice_ = {"The mountain groans overhead...", 0xd8c8b0};
        audio.playAt(Sfx::Rumble, site_, 1.0f);
        break;
    case StageEventKind::GoldFall:
        notice_ = {"Gold glitters in the rock above!", 0xffd060};
        audio.playAt(Sfx::Rumble, site_, 0.7f, 1.3f);
        break;
    case StageEventKind::Breach:
        if (matter_ == mat::Lava) notice_ = {"The rock glows red-hot...", 0xff8040};
        else if (matter_ == mat::Oil) notice_ = {"Oil weeps from the walls...", 0xc8a070};
        else if (matter_ == mat::Toxic) notice_ = {"Sludge weeps from the walls...", 0xa0e070};
        else notice_ = {"Water seeps through the cracks...", 0x80b8ff};
        audio.playAt(Sfx::Rumble, site_, 0.8f, 0.8f);
        break;
    case StageEventKind::GasSeep:
        notice_ = {"The air smells of marsh gas...", 0xb8d890};
        audio.playAt(Sfx::Sizzle, site_, 0.6f, 0.5f);
        break;
    case StageEventKind::Swarm:
        notice_ = {level.biome() == BiomeId::Rubedo || level.biome() == BiomeId::Citrinitas ? "Fiends gather in the dark..."
                                                                                          : "Wings stir in the dark...",
                   0xc8a0e0};
        break;
    case StageEventKind::Count: break;
    }
    if (kind == StageEventKind::Breach || kind == StageEventKind::GasSeep) {
        const Rgba c = materialColor(matter_);
        level.addFlash(site_, (static_cast<uint32_t>(c.r) << 16) | (static_cast<uint32_t>(c.g) << 8) | c.b, 1.2f, 30.0f, kWarnTime);
    }
    return true;
}

void StageEvents::warn(Level& level) {
    Rng& rng = level.rng();
    ParticleSystem& fx = level.particles();
    switch (kind_) {
    case StageEventKind::CaveIn:
    case StageEventKind::GoldFall: {
        // Grit trickles from the stretch of ceiling about to fall, marking exactly where.
        const bool gold = kind_ == StageEventKind::GoldFall;
        for (int i = 0; i < 2; ++i)
            fx.addVisual(site_ + Vec2{rng.range(-9.0f, 9.0f), 1.0f}, {rng.range(-4.0f, 4.0f), rng.range(20.0f, 60.0f)},
                         gold ? Rgba{255, 214, 90, 230} : Rgba{150, 140, 128, 200}, gold ? Rgba{255, 190, 60, 160} : Rgba{},
                         rng.range(0.8f, 1.4f), 0.6f);
        level.addShake(gold ? 0.002f : 0.006f);
        break;
    }
    case StageEventKind::Breach:
    case StageEventKind::GasSeep: {
        // The crack spits sparks of whatever is behind it.
        if (level.ticks() % 2 == 0) {
            const Rgba c = materialColor(matter_);
            fx.addVisual(site_ + Vec2{dirX_ * 1.5f, rng.range(-3.0f, 3.0f)}, {dirX_ * rng.range(10.0f, 40.0f), rng.range(-15.0f, 15.0f)},
                         Rgba{c.r, c.g, c.b, 230}, matter_ == mat::Lava ? Rgba{255, 120, 40, 200} : Rgba{}, rng.range(0.3f, 0.6f),
                         0.3f);
        }
        level.addShake(0.003f);
        break;
    }
    case StageEventKind::Swarm:
    case StageEventKind::Count: break;
    }
}

void StageEvents::release(Level& level) {
    released_ = true;
    Rng& rng = level.rng();
    World& world = level.world();
    switch (kind_) {
    case StageEventKind::CaveIn: {
        collapse(level, kCaveInRadiusX, kCaveInRadiusY, false);
        // Chips of stone burst from the ceiling ahead of the fall.
        for (int i = 0; i < 24; ++i)
            level.particles().addVisual(site_ + Vec2{rng.range(-10.0f, 10.0f), rng.range(-2.0f, 2.0f)},
                                        {rng.range(-50.0f, 50.0f), rng.range(-20.0f, 60.0f)}, Rgba{120, 112, 104, 255}, Rgba{},
                                        rng.range(0.5f, 1.0f), 1.0f);
        // The rock lands on whatever stands under it a moment later.
        int floor = floorToInt(site_.y) + 1;
        while (floor < floorToInt(site_.y) + 90 && isOpen(world, floorToInt(site_.x), floor)) ++floor;
        impact_ = {site_.x, static_cast<float>(floor) - 4.0f};
        strikeAt_ = clamp((impact_.y - site_.y) / 70.0f, 0.3f, 1.2f);
        timer_ = strikeAt_ + 0.1f;
        level.audio().playAt(Sfx::ExplosionSmall, site_, 0.8f, 0.5f);
        level.addShake(0.45f);
        break;
    }
    case StageEventKind::GoldFall:
        collapse(level, kGoldFallRadiusX, kGoldFallRadiusY, true);
        timer_ = 0.1f;
        level.addFlash(site_, 0xffd070, 1.6f, 50.0f, 0.6f);
        level.audio().playAt(Sfx::Coin, site_, 0.8f, 0.8f);
        break;
    case StageEventKind::Breach:
    case StageEventKind::GasSeep: {
        // Open the crack (natural stone only) and let it flow.
        const float r = kind_ == StageEventKind::Breach ? 2.5f : 1.5f;
        for (int dy = -3; dy <= 3; ++dy)
            for (int dx = -3; dx <= 3; ++dx) {
                const int x = floorToInt(site_.x) + dx, y = floorToInt(site_.y) + dy;
                if (static_cast<float>(dx * dx + dy * dy) <= r * r && debrisOf(world.matAt(x, y)) != mat::Empty) world.clear(x, y);
            }
        timer_ = kind_ == StageEventKind::GasSeep ? 5.0f : (matter_ == mat::Lava ? 3.5f : 4.5f);
        level.audio().playAt(kind_ == StageEventKind::Breach ? Sfx::Splash : Sfx::Sizzle, site_, 0.8f, 0.7f);
        break;
    }
    case StageEventKind::Swarm: {
        const int count = 3 + level.layout().levelIndex / 2;
        const Vec2 target = level.player().body.pos;
        for (int i = 0; i < count; ++i) {
            Enemy& e = level.spawnEnemy(pickEnemy(level.layout().levelIndex, true, 0.5f, rng), site_ + rng.inCircle(5.0f));
            e.alert(target);
        }
        timer_ = 0.0f;
        break;
    }
    case StageEventKind::Count: break;
    }
}

void StageEvents::unfold(Level& level) {
    if (!released_) release(level);
    elapsed_ += kTickDt;
    Rng& rng = level.rng();
    if (kind_ == StageEventKind::CaveIn && strikeAt_ >= 0.0f && elapsed_ >= strikeAt_) {
        const float damage = 16.0f * (1.0f + 0.15f * static_cast<float>(level.layout().levelIndex));
        level.damageRadius(impact_, 12.0f, damage, DamageType::Physical, Team::Monster, 120.0f, true, "a cave-in");
        level.particles().burst(impact_, 26, 70.0f, Rgba{150, 142, 132, 200}, Rgba{}, 0.8f, rng, 0.1f);
        level.audio().playAt(Sfx::ExplosionSmall, impact_, 0.5f, 0.4f);
        level.addShake(0.3f);
        strikeAt_ = -1.0f;
    }
    if (kind_ == StageEventKind::Breach || kind_ == StageEventKind::GasSeep) {
        const bool gas = kind_ == StageEventKind::GasSeep;
        const int perTick = gas ? 3 : (matter_ == mat::Lava ? 3 : 5);
        for (int i = 0; i < perTick; ++i) {
            const Vec2 vel = gas ? Vec2{dirX_ * rng.range(10.0f, 40.0f), rng.range(-30.0f, -5.0f)}
                                 : Vec2{dirX_ * rng.range(60.0f, 130.0f), rng.range(-30.0f, 10.0f)};
            level.particles().addMatter(site_ + Vec2{dirX_ * 1.5f, rng.range(-1.5f, 1.5f)}, vel, makeCell(matter_, rng.next()));
        }
        if (!gas && level.ticks() % 20 == 0) level.audio().playAt(Sfx::Splash, site_, 0.35f, 0.8f);
    }
    timer_ -= kTickDt;
    if (timer_ <= 0.0f) phase_ = Phase::Idle;
}

void StageEvents::collapse(Level& level, int radiusX, int radiusY, bool gold) {
    World& world = level.world();
    Rng& rng = level.rng();
    const int cx = floorToInt(site_.x), cy = collapseCentreY(floorToInt(site_.y), radiusY);
    for (int dy = -radiusY; dy <= radiusY; ++dy)
        for (int dx = -radiusX; dx <= radiusX; ++dx) {
            const float nx = static_cast<float>(dx) / static_cast<float>(radiusX), ny = static_cast<float>(dy) / static_cast<float>(radiusY);
            if (nx * nx + ny * ny > 1.0f) continue;
            const int x = cx + dx, y = cy + dy;
            MatId debris = debrisOf(world.matAt(x, y));
            // Vault shells (and a spring's basin just below one) are never breached.
            if (debris == mat::Empty || insideVault(level, {static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f}, kVaultMargin)) continue;
            if (gold && rng.chance(0.8f)) debris = mat::GoldDust;
            // A third of the stone crumbles to dust on the way down, so the heap is less apt to plug a shaft.
            if (!gold && rng.chance(0.35f)) world.clear(x, y);
            else world.place(x, y, debris);
        }
    world.wakeRect(cx - radiusX - 1, cy - radiusY - 1, cx + radiusX + 1, cy + radiusY + 1);
}

} // namespace pm
