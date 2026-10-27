// The compass's route guide: it leads along the stage's route, never backwards, and brings a
// stray alchemist back to the route ahead of them.
#include <cmath>
#include <vector>

#include "core/ThreadPool.hpp"
#include "game/RouteGuide.hpp"
#include "gen/Biome.hpp"
#include "gen/LevelGenerator.hpp"
#include "sim/Material.hpp"
#include "sim/World.hpp"
#include "tests/TestFramework.hpp"

using namespace pm;

TEST_CASE("the route guide leads along the route and never backwards") {
    // Spawn on a ledge, walk sideways to a cave mouth, then down two legs to the exit.
    RouteGuide g;
    const Vec2 start{0.0f, 0.0f};
    g.reset(start, {{115.0f, 0.0f}, {0.0f, 250.0f}, {400.0f, 500.0f}}, {400.0f, 520.0f});

    // At the spawn the way on is sideways to the cave mouth, not down through the rock.
    const Vec2 first = g.target(start) - start;
    CHECK(first.x > 0.0f && std::fabs(first.x) > std::fabs(first.y));

    g.update({115.0f, 0.0f});
    g.update({60.0f, 130.0f});
    const float reached = g.progress();
    CHECK(reached > 115.0f);
    g.update(start); // walking back does not undo progress
    CHECK(g.progress() == reached);

    // Far from the route, it aims at the nearest stretch of route ahead of the progress made,
    // never back towards the cave mouth already passed.
    const Vec2 stray{-200.0f, 130.0f};
    const Vec2 rejoin = g.target(stray);
    CHECK(rejoin.y > 130.0f && rejoin.y < 250.0f);
    CHECK((rejoin - stray).length() < (Vec2{60.0f, 130.0f} - stray).length());

    // The last stretch aims at the exit itself.
    g.update({400.0f, 500.0f});
    const Vec2 last = g.target({400.0f, 500.0f});
    CHECK((last - Vec2{400.0f, 520.0f}).length() < 0.01f);

    // Without a route it points straight at the exit.
    RouteGuide bare;
    bare.reset({10.0f, 10.0f}, {}, {50.0f, 90.0f});
    const Vec2 t = bare.target({10.0f, 10.0f}) - Vec2{10.0f, 10.0f};
    const Vec2 exitDir{40.0f, 80.0f};
    CHECK(dot(t, exitDir) > 0.0f && std::fabs(t.x * exitDir.y - t.y * exitDir.x) < 0.01f);
}

TEST_CASE("on the first stage the compass first leads towards the cave mouth") {
    initMaterials();
    ThreadPool pool(2);
    for (uint32_t seed : {777u, 31u, 4242u}) {
        const LevelLayout layout = generateLevel(biomeForDepth(0), 0, seed, pool);
        REQUIRE(!layout.mainPath.empty());
        RouteGuide g;
        g.reset(layout.playerStart, layout.mainPath, layout.exitPos);
        g.update(layout.playerStart);
        const Vec2 to = g.target(layout.playerStart) - layout.playerStart;
        const float mouth = layout.mainPath.front().x - layout.playerStart.x;
        CHECK(std::fabs(to.x) > std::fabs(to.y));
        CHECK((to.x > 0.0f) == (mouth > 0.0f));
    }
}
