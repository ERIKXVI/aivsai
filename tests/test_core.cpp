// Core building blocks tested directly: the fork-join pool, integer and random helpers, the
// navigation grid behind route checks, and matter particles settling back into the grid.
#include <atomic>
#include <vector>

#include "app/InputLatch.hpp"
#include "core/Math.hpp"
#include "core/Rng.hpp"
#include "core/ThreadPool.hpp"
#include "gen/LevelGenerator.hpp"
#include "sim/Particles.hpp"
#include "sim/World.hpp"
#include "tests/TestFramework.hpp"
#include "tests/TestWorld.hpp"

using namespace pm;

using pm::test::buildBox;
using pm::test::countMat;

static_assert(floorDiv(-1, 4) == -1 && floorDiv(-4, 4) == -1 && floorDiv(-5, 4) == -2 && floorDiv(3, 4) == 0);
static_assert(positiveMod(-1, 4) == 3 && positiveMod(4, 4) == 0 && positiveMod(5, 4) == 1);

TEST_CASE("the thread pool runs every job exactly once") {
    ThreadPool pool(3);
    constexpr int kJobs = 257;
    std::vector<std::atomic<int>> hits(kJobs);
    for (int round = 0; round < 4; ++round) pool.parallelFor(kJobs, [&](int i) { hits[static_cast<size_t>(i)]++; });
    for (const auto& h : hits) CHECK(h.load() == 4);
    pool.parallelFor(0, [&](int) { CHECK(false); }); // an empty batch runs nothing
}

TEST_CASE("random streams repeat for a seed and stay in range") {
    Rng a(1234), b(1234), c(4321);
    bool differs = false;
    for (int i = 0; i < 64; ++i) {
        const uint32_t x = a.next();
        CHECK(x == b.next());
        differs = differs || x != c.next();
    }
    CHECK(differs);
    for (int i = 0; i < 2000; ++i) {
        const int v = a.rangeInt(-3, 5);
        CHECK(v >= -3 && v <= 5);
        const float u = a.uniform();
        CHECK(u >= 0.0f && u < 1.0f);
    }
    CHECK(!a.chance(0.0f));
    CHECK(a.chance(1.0f));
}

TEST_CASE("route checks see walls and open floors") {
    initMaterials();
    World w(192, 128, 9);
    buildBox(w);
    // A floor to walk on.
    for (int x = 1; x < w.width() - 1; ++x)
        for (int y = 100; y < w.height() - 1; ++y) w.place(x, y, mat::Rock);
    const Vec2 from{24.0f, 92.0f}, to{168.0f, 92.0f};
    CHECK(pathExists(w, from, to));
    // A wall from floor to ceiling cuts the cave in two.
    for (int y = 1; y < 100; ++y)
        for (int x = 94; x < 100; ++x) w.place(x, y, mat::Rock);
    CHECK(!pathExists(w, from, to));
}

TEST_CASE("matter particles settle back into the grid without loss") {
    initMaterials();
    World w(128, 128, 3);
    buildBox(w);
    ParticleSystem particles;
    Rng rng(7);
    constexpr int kGrains = 60;
    for (int i = 0; i < kGrains; ++i)
        particles.addMatter({20.0f + static_cast<float>(i), 30.0f}, {rng.range(-40.0f, 40.0f), rng.range(-60.0f, 0.0f)},
                            makeCell(mat::Sand, rng.next()), 30.0f);
    for (int tick = 0; tick < 600 && particles.count() > 0; ++tick) particles.update(w, 1.0f / 60.0f);
    CHECK(particles.count() == 0);
    CHECK(countMat(w, mat::Sand) == kGrains);
}

TEST_CASE("the mouse wheel steps one wand per notch, however it is sent") {
    // An ordinary wheel: one full delta per notch.
    input::WheelStepper notched;
    CHECK(notched.step(1.0f, 0.016f) == 1);
    CHECK(notched.step(0.0f, 0.2f) == 0);
    CHECK(notched.step(-1.0f, 0.016f) == -1);
    // A touchpad swipe: a notch's worth of small deltas over many frames is one step.
    input::WheelStepper pad;
    int steps = 0;
    for (int i = 0; i < 10; ++i) steps += pad.step(0.1f, 0.016f);
    CHECK(steps == 1);
    // A pause forgets a partial notch.
    input::WheelStepper partial;
    CHECK(partial.step(0.6f, 0.016f) == 0);
    CHECK(partial.step(0.0f, 0.3f) == 0);
    CHECK(partial.step(0.6f, 0.016f) == 0);
    // Notches faster than the cooldown are not lost, just spaced out.
    input::WheelStepper fast;
    CHECK(fast.step(1.0f, 0.016f) == 1);
    CHECK(fast.step(1.0f, 0.016f) == 0);
    CHECK(fast.step(0.0f, 0.15f) == 1);
}
