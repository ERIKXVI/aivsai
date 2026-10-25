// Level generation: every biome, several seeds, must have a traversable main route; omens
// follow their rules and stay traversable too.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "core/ThreadPool.hpp"
#include "gen/LevelGenerator.hpp"
#include "sim/World.hpp"
#include "tests/TestFramework.hpp"
#include "tests/TestWorld.hpp"

using namespace pm;

TEST_CASE("stages I-IV hide a guardian's lair") {
    initMaterials();
    ThreadPool pool(4);
    int lairs = 0, levels = 0;
    // Seed 19 once stamped a lair that cut off the cave its own door relied on.
    for (uint32_t seed : {3u, 5u, 9u, 19u})
        for (int index = 0; index < kFinalStage; ++index) {
            const LevelLayout layout = generateLevel(biomeForDepth(index), index, seed, pool);
            ++levels;
            for (const SpawnPoint& s : layout.spawns) lairs += s.kind == SpawnKind::LairGuardian ? 1 : 0;
            for (const Rectf& v : layout.vaults) CHECK(pathExists(*layout.world, layout.playerStart, vaultProbePoint(v)));
        }
    std::printf("    lairs placed: %d of %d stages\n", lairs, levels);
    CHECK(lairs >= levels * 3 / 4);
}

TEST_CASE("generated levels are traversable from entrance to exit") {
    initMaterials();
    ThreadPool pool;
    for (uint32_t seed : {11u, 4242u, 90001u}) {
        for (int level = 0; level < kStageCount; ++level) {
            LevelLayout layout = generateLevel(biomeForDepth(level), level, seed + static_cast<uint32_t>(level), pool);
            bool ok = pathExists(*layout.world, layout.playerStart, layout.exitPos);
            if (!ok) std::printf("    blocked: seed %u level %d\n", seed, level);
            CHECK(ok);
            CHECK(!layout.spawns.empty());
            // Vaults must be reachable through their doorways.
            for (const Rectf& v : layout.vaults) {
                const bool open = pathExists(*layout.world, layout.playerStart, vaultProbePoint(v));
                if (!open) std::printf("    sealed vault: seed %u level %d at %.0f,%.0f\n", seed, level, v.x, v.y);
                CHECK(open);
            }
        }
    }
}

TEST_CASE("generation is deterministic for a seed, whatever the worker count") {
    // Everyone gets the same Daily Rite seed, and the app's pool is sized from the hardware,
    // so a level must not depend on how many workers built it.
    initMaterials();
    ThreadPool one(1), many(4);
    auto sameSpawn = [](const SpawnPoint& p, const SpawnPoint& q) {
        return p.kind == q.kind && p.pos.x == q.pos.x && p.pos.y == q.pos.y && p.difficulty == q.difficulty;
    };
    auto sameRect = [](const Rectf& p, const Rectf& q) { return p.x == q.x && p.y == q.y && p.w == q.w && p.h == q.h; };
    for (int index : {0, 3}) {
        const LevelLayout a = generateLevel(biomeForDepth(index), index, 777u, one);
        const LevelLayout b = generateLevel(biomeForDepth(index), index, 777u, many);
        CHECK(pm::test::sameCells(*a.world, *b.world));
        CHECK(a.playerStart.x == b.playerStart.x && a.playerStart.y == b.playerStart.y);
        CHECK(a.exitPos.x == b.exitPos.x && a.exitPos.y == b.exitPos.y);
        CHECK(std::equal(a.spawns.begin(), a.spawns.end(), b.spawns.begin(), b.spawns.end(), sameSpawn));
        CHECK(std::equal(a.vaults.begin(), a.vaults.end(), b.vaults.begin(), b.vaults.end(), sameRect));
        CHECK(a.omen == b.omen && a.surfaceY == b.surfaceY && a.skyline == b.skyline);
        CHECK(a.trees.size() == b.trees.size());
    }
}

TEST_CASE("sanctum has start, exit and shop slots") {
    initMaterials();
    ThreadPool pool;
    LevelLayout s = generateSanctum(1, 5u, pool);
    CHECK(pathExists(*s.world, s.playerStart, s.exitPos));
    int shop = 0;
    for (const auto& sp : s.spawns)
        if (sp.kind == SpawnKind::Spell) ++shop;
    CHECK(shop >= 3);
}

TEST_CASE("omens follow their rules and keep the route open") {
    std::array<int, static_cast<size_t>(Omen::Count)> seen{};
    for (uint32_t seed = 1; seed <= 600; ++seed) {
        CHECK(omenFor(kFinalStage, seed) == Omen::None); // the Athanor is never touched
        const Omen first = omenFor(0, seed);
        CHECK(first == Omen::None || first == Omen::Flooded || first == Omen::Overgrown || first == Omen::Gilded);
        CHECK(omenFor(2, seed) == omenFor(2, seed));
        seen[static_cast<size_t>(omenFor(2, seed))]++;
    }
    for (int count : seen) CHECK(count > 0); // every omen (and none) occurs
    for (int i = 1; i < static_cast<int>(Omen::Count); ++i) CHECK(std::strlen(omenDef(static_cast<Omen>(i)).name) > 0);

    // One stage of each omen must still be traversable.
    initMaterials();
    ThreadPool pool;
    std::array<bool, static_cast<size_t>(Omen::Count)> generated{};
    generated[0] = true;
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        const Omen omen = omenFor(2, seed);
        if (generated[static_cast<size_t>(omen)]) continue;
        generated[static_cast<size_t>(omen)] = true;
        LevelLayout layout = generateLevel(biomeForDepth(2), 2, seed, pool);
        CHECK(layout.omen == omen);
        const bool ok = pathExists(*layout.world, layout.playerStart, layout.exitPos);
        if (!ok) std::printf("    blocked: %s stage, seed %u\n", omenDef(omen).name, seed);
        CHECK(ok);
    }
}

TEST_CASE("trees stay whole until cut, then shed what is cut loose") {
    initMaterials();
    ThreadPool pool;
    LevelLayout layout = generateLevel(biomeForDepth(0), 0, 777u, pool);
    CHECK(!layout.trees.empty());
    World& world = *layout.world;
    for (TreeShape& tree : layout.trees) {
        const size_t whole = tree.cells.size();
        CHECK(pruneTree(world, tree).empty()); // grown as one connected piece
        CHECK(tree.cells.size() == whole);
    }
    // Fell the first tree at the root: everything above comes loose.
    TreeShape& tree = layout.trees.front();
    const size_t whole = tree.cells.size();
    world.clear(tree.rootX, tree.rootY);
    world.clear(tree.rootX + 1, tree.rootY);
    const std::vector<uint32_t> loose = pruneTree(world, tree);
    CHECK(tree.cells.empty());
    CHECK(loose.size() + 2 >= whole - 1 && loose.size() < whole);
}
