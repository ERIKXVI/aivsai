// Level generation: every biome, several seeds, must have a traversable main route; omens
// follow their rules and stay traversable too.
#include <array>
#include <cstring>

#include "core/ThreadPool.hpp"
#include "gen/LevelGenerator.hpp"
#include "sim/World.hpp"
#include "tests/TestFramework.hpp"

using namespace pm;

TEST_CASE("generated levels are traversable from entrance to exit") {
    initMaterials();
    ThreadPool pool;
    for (uint32_t seed : {11u, 4242u, 90001u}) {
        for (int level = 0; level < 5; ++level) {
            LevelLayout layout = generateLevel(biomeForDepth(level), level, seed + static_cast<uint32_t>(level), pool);
            bool ok = pathExists(*layout.world, layout.playerStart, layout.exitPos);
            if (!ok) std::printf("    blocked: seed %u level %d\n", seed, level);
            CHECK(ok);
            CHECK(!layout.spawns.empty());
            // Vaults must be reachable through their doorways.
            for (const Rectf& v : layout.vaults) {
                const Vec2 inside{v.x + v.w * 0.5f, v.y + v.h - 10.0f};
                const bool open = pathExists(*layout.world, layout.playerStart, inside);
                if (!open) std::printf("    sealed vault: seed %u level %d at %.0f,%.0f\n", seed, level, v.x, v.y);
                CHECK(open);
            }
        }
    }
}

TEST_CASE("generation is deterministic for a seed") {
    initMaterials();
    ThreadPool pool;
    LevelLayout a = generateLevel(BiomeId::Nigredo, 0, 777u, pool);
    LevelLayout b = generateLevel(BiomeId::Nigredo, 0, 777u, pool);
    CHECK(a.playerStart.x == b.playerStart.x);
    CHECK(a.exitPos.y == b.exitPos.y);
    CHECK(a.spawns.size() == b.spawns.size());
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
        CHECK(omenFor(4, seed) == Omen::None); // the Athanor is never touched
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
