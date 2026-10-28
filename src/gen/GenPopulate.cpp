// Level generation, step three: spawns, transmutation circles and vaults.

#include "gen/GeneratorImpl.hpp"

#include <algorithm>
#include <cmath>

#include "gen/Navigation.hpp"
#include "gen/Vaults.hpp"
#include "sim/World.hpp"

namespace pm::gen_detail {

void Generator::placeTransmuter(World& world, LevelLayout& out) {
    std::vector<Vec2> candidates = chambers_;
    // The basin and its rim must never close the route (a chamber the route squeezes through).
    const bool routeOpen = pathExists(world, start_, exit_);
    std::vector<Cell> saved;
    for (int attempt = 0; attempt < 40 && !candidates.empty(); ++attempt) {
        const Vec2 c = candidates[static_cast<size_t>(rng_.rangeInt(0, static_cast<int>(candidates.size()) - 1))];
        int x = static_cast<int>(c.x) + rng_.rangeInt(-20, 20), y = static_cast<int>(c.y);
        // Drop to the chamber floor.
        while (y < h_ - 20 && world.inBounds(x, y + 1) && !isSolidForCollision(world.at(x, y + 1).mat)) ++y;
        if (y >= h_ - 20 || (Vec2{static_cast<float>(x), static_cast<float>(y)} - start_).length() < 120.0f) continue;
        // Not at the bottom of a natural pool (the drop sinks through liquid): it would pour back into
        // the basin, and as the circle counts only matter the alchemist brought, it could never fill.
        bool flooded = false;
        for (int dy = -30; dy <= 0 && !flooded; ++dy)
            for (int dx = -16; dx <= 16 && !flooded; ++dx)
                flooded = world.inBounds(x + dx, y + dy) && phaseOf(world.at(x + dx, y + dy).mat) == Phase::Liquid;
        if (flooded) continue;
        // Clear headroom and build the basin: a runestone tub with a raised rim.
        saved.clear();
        for (int dy = -14; dy <= 4; ++dy)
            for (int dx = -11; dx <= 11; ++dx) saved.push_back(world.inBounds(x + dx, y + dy) ? world.at(x + dx, y + dy) : Cell{});
        for (int dy = -14; dy <= 0; ++dy)
            for (int dx = -11; dx <= 11; ++dx)
                if (world.inBounds(x + dx, y + dy) && world.at(x + dx, y + dy).mat != mat::Bedrock) world.clear(x + dx, y + dy);
        for (int dx = -10; dx <= 10; ++dx) {
            for (int dy = 1; dy <= 4; ++dy) world.place(x + dx, y + dy, mat::Rune);
        }
        // The rim stands as tall as the 19 x 7 box Level::updateTransmuters counts, so the basin
        // holds all it asks for.
        for (int dy = -6; dy <= 0; ++dy) {
            world.place(x - 10, y + dy, mat::Rune);
            world.place(x + 10, y + dy, mat::Rune);
        }
        if (routeOpen && !pathExists(world, start_, exit_)) {
            size_t i = 0;
            for (int dy = -14; dy <= 4; ++dy)
                for (int dx = -11; dx <= 11; ++dx, ++i)
                    if (world.inBounds(x + dx, y + dy)) world.set(x + dx, y + dy, saved[i]);
            continue;
        }
        out.spawns.push_back({SpawnKind::Transmuter, {static_cast<float>(x), static_cast<float>(y)}, 0.5f});
        return;
    }
}

bool Generator::solidRect(const World& world, int x0, int y0, int w, int h) const {
    for (int y = y0; y < y0 + h; y += 2)
        for (int x = x0; x < x0 + w; x += 2) {
            if (!world.inBounds(x, y) || isOpen(x, y)) return false;
            const MatId m = world.at(x, y).mat;
            if (m == mat::Bedrock || m == mat::Rune || phaseOf(m) != Phase::Solid) return false;
        }
    return true;
}

bool Generator::mostlySolidRect(const World& world, int x0, int y0, int w, int h, float maxOpen) const {
    int open = 0, total = 0;
    for (int y = y0; y < y0 + h; y += 2)
        for (int x = x0; x < x0 + w; x += 2) {
            if (!world.inBounds(x, y)) return false;
            const MatId m = world.at(x, y).mat;
            if (m == mat::Bedrock || m == mat::Rune) return false;
            ++total;
            open += isOpen(x, y) || phaseOf(m) != Phase::Solid ? 1 : 0;
        }
    return total > 0 && static_cast<float>(open) <= maxOpen * static_cast<float>(total);
}

void Generator::placeVaults(World& world, LevelLayout& out) {
    NavGrid nav;
    nav.build(world);
    if (!nav.computeDistances(start_)) return;
    // Stages I-IV each hide a guardian's lair besides their ordinary vaults; the lair gets the
    // first pick of sites, as it is the hardest to fit.
    const bool lairWanted = level_ < kFinalStage;
    const int wanted = level_ >= kFinalStage ? 1 : (lairWanted ? 3 : 2);
    const int top = (surface_ ? kSkyRows : 0) + 70;
    constexpr uint32_t kLairBit = 1u << static_cast<unsigned>(VaultKind::Lair);
    uint32_t used = 0;
    std::vector<Rectf> placed;
    // Ground a doorway's passage must not bore into: transmutation basins, headroom to runestone
    // floor (it would cut the stone, or take the hollow for a cave), and, as they are placed, earlier
    // vaults with a margin.
    constexpr float kKeepClear = 3.0f;
    std::vector<Rectf> keepOut;
    for (const SpawnPoint& s : out.spawns)
        if (s.kind == SpawnKind::Transmuter) keepOut.push_back({s.pos.x - 12.0f, s.pos.y - 15.0f, 25.0f, 20.0f});
    for (int attempt = 0; attempt < 1500 && static_cast<int>(placed.size()) < wanted; ++attempt) {
        VaultPlan plan;
        const bool lairTurn = lairWanted && !(used & kLairBit) && attempt < 900;
        plan.kind = lairTurn ? VaultKind::Lair : pickVaultKind(biome_, rng_, used | kLairBit);
        vaultSize(plan.kind, plan.w, plan.h);
        if (w_ - 2 * kBorder - 40 < plan.w || h_ - top - 60 < plan.h) return;
        plan.x0 = rng_.rangeInt(kBorder + 20, w_ - kBorder - 20 - plan.w);
        plan.y0 = rng_.rangeInt(top, h_ - kBorder - 40 - plan.h);
        const Vec2 c{static_cast<float>(plan.x0) + plan.w * 0.5f, static_cast<float>(plan.y0) + plan.h * 0.5f};
        const float extent = static_cast<float>(std::max(plan.w, plan.h)) * 0.5f;
        if ((c - start_).length() < 170.0f || (c - exit_).length() < 120.0f || distanceToPath(c) < extent + 30.0f) continue;
        if (arena_.w > 0.0f && (c - Vec2{arena_.x, arena_.y}).length() < arena_.w + extent + 30.0f) continue;
        bool overlaps = false;
        for (const Rectf& r : placed)
            overlaps |= std::fabs(r.x - c.x) < (r.w + plan.w) * 0.5f + 30.0f && std::fabs(r.y - c.y) < (r.h + plan.h) * 0.5f + 30.0f;
        // A lair may swallow the odd cave pocket; other vaults need sound rock all round.
        const bool sound = plan.kind == VaultKind::Lair ? mostlySolidRect(world, plan.x0 - 4, plan.y0 - 4, plan.w + 8, plan.h + 8, 0.2f)
                                                        : solidRect(world, plan.x0 - 4, plan.y0 - 4, plan.w + 8, plan.h + 8);
        if (overlaps || !sound) continue;

        // The doorway must tunnel through to caves the player can actually reach.
        const int door = vaultDoorRow(plan);
        // The passage's rows, lintel to brick floor, as the carve below makes them.
        const float passTop = static_cast<float>(door - kVaultDoorHeight), passRows = static_cast<float>(kVaultDoorHeight + 2);
        int tunnelEnd = -1;
        const int firstSide = rng_.sign();
        for (int s = 0; s < 2 && tunnelEnd < 0; ++s) {
            plan.doorSide = s == 0 ? firstSide : -firstSide;
            const int startX = plan.doorSide > 0 ? plan.x0 + plan.w : plan.x0 - 1;
            for (int k = 0; k < 110; ++k) {
                const int x = startX + plan.doorSide * k, y = door - 4;
                if (!world.inBounds(x, y) || x < kBorder + 6 || x >= w_ - kBorder - 6) break;
                // Give this side up where the passage (its full height, and the 8 cells carved on into the
                // cave) would reach ground kept out; everything the carve below touches has passed this test.
                const Rectf reach{static_cast<float>(std::min(x, x + plan.doorSide * 8)), passTop, 9.0f, passRows};
                if (std::any_of(keepOut.begin(), keepOut.end(), [&](const Rectf& r) { return r.overlaps(reach); })) break;
                // Strict search: the cave must be reachable right where the passage meets it.
                if (world.at(x, y).mat == mat::Empty && nav.distanceAt({static_cast<float>(x), static_cast<float>(y)}, 2) >= 0) {
                    tunnelEnd = x;
                    break;
                }
            }
        }
        if (tunnelEnd < 0) continue;

        // A paved passage with timber lintels leads in from the caves.
        const int startX = plan.doorSide > 0 ? plan.x0 + plan.w : plan.x0 - 1;
        // Overshoot into the cave so no sliver of rock is left between passage and cave.
        const int far = tunnelEnd + plan.doorSide * 8;
        const int x0 = std::max(kBorder + 2, std::min(startX, far)), x1 = std::min(w_ - kBorder - 3, std::max(startX, far));

        // Keep what the stamp will cover, so a vault that cuts off its own way in (a lair may
        // swallow the cave its door relies on) can be undone.
        const int sx0 = std::max(0, std::min(plan.x0, x0) - 1), sx1 = std::min(w_ - 1, std::max(plan.x0 + plan.w, x1) + 1);
        const int sy0 = std::max(0, plan.y0 - 1), sy1 = std::min(h_ - 1, plan.y0 + plan.h + 1);
        std::vector<Cell> saved;
        saved.reserve(static_cast<size_t>((sx1 - sx0 + 1) * (sy1 - sy0 + 1)));
        for (int y = sy0; y <= sy1; ++y)
            for (int x = sx0; x <= sx1; ++x) saved.push_back(world.at(x, y));
        const size_t spawnMark = out.spawns.size();

        buildVault(world, plan, rng_, out.spawns);
        for (int x = x0; x <= x1; ++x) {
            for (int y = door - kVaultDoorHeight + 1; y <= door; ++y) {
                const Phase ph = phaseOf(world.at(x, y).mat);
                if (world.at(x, y).mat != mat::Bedrock && (ph == Phase::Solid || ph == Phase::Powder)) world.clear(x, y);
            }
            if (phaseOf(world.at(x, door + 1).mat) == Phase::Solid && world.at(x, door + 1).mat != mat::Bedrock)
                world.place(x, door + 1, mat::Brick);
            if (std::abs(x - startX) % 14 < 2 && phaseOf(world.at(x, door - kVaultDoorHeight).mat) == Phase::Solid)
                world.place(x, door - kVaultDoorHeight, mat::Wood);
        }
        const Rectf bounds{static_cast<float>(plan.x0), static_cast<float>(plan.y0), static_cast<float>(plan.w),
                           static_cast<float>(plan.h)};
        if (!pathExists(world, start_, vaultProbePoint(bounds))) {
            size_t i = 0;
            for (int y = sy0; y <= sy1; ++y)
                for (int x = sx0; x <= sx1; ++x) world.set(x, y, saved[i++]);
            out.spawns.resize(spawnMark);
            continue;
        }
        // Later doors must meet caves that are still reachable now this vault stands.
        nav.build(world);
        nav.computeDistances(start_);
        placed.push_back({c.x, c.y, static_cast<float>(plan.w), static_cast<float>(plan.h)});
        out.vaults.push_back(bounds);
        keepOut.push_back({bounds.x - kKeepClear, bounds.y - kKeepClear, bounds.w + 2.0f * kKeepClear,
                           bounds.h + 2.0f * kKeepClear});
        used |= 1u << static_cast<unsigned>(plan.kind);
    }
}

bool Generator::spaceAbove(const World& w, int x, int y, int height, int halfWidth) const {
    for (int dy = 0; dy < height; ++dy)
        for (int dx = -halfWidth; dx <= halfWidth; ++dx) {
            if (!w.inBounds(x + dx, y - dy)) return false;
            if (phaseOf(w.at(x + dx, y - dy).mat) != Phase::Empty) return false;
        }
    return true;
}

void Generator::collectSpawns(const World& world, LevelLayout& out) {
    auto difficultyAt = [&](float y) { return clamp((y - start_.y) / (exit_.y - start_.y), 0.0f, 1.0f); };
    auto farFromEnds = [&](Vec2 p, float minStart) {
        return (p - start_).length() > minStart && (p - exit_).length() > 50.0f;
    };

    int groundTarget = 16 + level_ * 4;
    int airTarget = 7 + level_ * 2;
    if (omen_ == Omen::Teeming) {
        groundTarget = groundTarget * 4 / 3;
        airTarget = airTarget * 4 / 3;
    }
    int tries = 0;
    int ground = 0, air = 0;
    while ((ground < groundTarget || air < airTarget) && tries++ < 6000) {
        int x = rng_.rangeInt(12, w_ - 12), y = rng_.rangeInt(40, h_ - 20);
        Vec2 p{static_cast<float>(x), static_cast<float>(y)};
        if (!farFromEnds(p, 150.0f)) continue;
        // Nothing lurks under the open sky; the first steps outside are peaceful.
        if (surface_ && y < skyline_[static_cast<size_t>(x)] + 40) continue;
        if (ground < groundTarget && isFloor(world, x, y) && spaceAbove(world, x, y, 14, 4)) {
            out.spawns.push_back({SpawnKind::GroundEnemy, p, difficultyAt(p.y)});
            ++ground;
        } else if (air < airTarget && farFromEnds(p, 230.0f) && spaceAbove(world, x, y, 24, 10)) {
            // Flyers close distance fast, so they start beyond sight of the entrance (imps see
            // 180 cells, bats 190): arriving on a stage is never an ambush.
            out.spawns.push_back({SpawnKind::AirEnemy, p, difficultyAt(p.y)});
            ++air;
        }
    }

    // Treasure: chests and potions favour chambers; free spells are rarer.
    auto placeOnFloor = [&](SpawnKind kind, Vec2 around, float spread) {
        for (int t = 0; t < 80; ++t) {
            int x = static_cast<int>(around.x + rng_.range(-spread, spread));
            int y = static_cast<int>(around.y + rng_.range(-spread * 0.5f, spread * 0.5f));
            for (int k = 0; k < 50 && world.inBounds(x, y); ++k, ++y) {
                if (isFloor(world, x, y)) {
                    if (spaceAbove(world, x, y, 10, 3)) {
                        out.spawns.push_back({kind, {static_cast<float>(x), static_cast<float>(y)}, difficultyAt(y)});
                        return true;
                    }
                    break;
                }
                if (phaseOf(world.at(x, y).mat) != Phase::Empty) break;
            }
        }
        return false;
    };
    for (const Vec2& c : chambers_) {
        placeOnFloor(rng_.chance(0.6f) ? SpawnKind::Chest : SpawnKind::Potion, c, 40.0f);
    }
    for (const Vec2& p : pockets_) placeOnFloor(rng_.chance(0.5f) ? SpawnKind::Spell : SpawnKind::Potion, p, 12.0f);
    placeOnFloor(SpawnKind::Chest, path_[path_.size() / 2], 60.0f);
    // Rich omens leave extra chests along the way.
    if (omen_ == Omen::Teeming || omen_ == Omen::Gilded) {
        placeOnFloor(SpawnKind::Chest, path_[path_.size() / 4], 60.0f);
        placeOnFloor(SpawnKind::Chest, path_[path_.size() * 3 / 4], 60.0f);
    }

    // Lanterns along the main path help orientation in the dark.
    for (size_t i = 0; i + 1 < path_.size(); ++i) {
        Vec2 a = path_[i], b = path_[i + 1];
        float len = (b - a).length();
        for (float t = 40.0f; t < len; t += 110.0f) {
            Vec2 p = lerp(a, b, t / len);
            int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
            // Hang from the ceiling above the corridor centre (gas pooled under it is no ceiling).
            int cy = y;
            while (cy > 5 && world.inBounds(x, cy) && phaseOf(world.at(x, cy).mat) != Phase::Solid && y - cy < 30) --cy;
            if (y - cy < 30 && y - cy > 6)
                out.spawns.push_back({SpawnKind::Lantern, {static_cast<float>(x), static_cast<float>(cy + 1)}, 0.0f});
        }
    }
}

} // namespace pm::gen_detail
