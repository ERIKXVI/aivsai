// Simulation invariants: movement conserves matter, reactions behave, chunks go to sleep,
// and vines cut loose from the ceiling fall.
#include <vector>

#include "core/ThreadPool.hpp"
#include "sim/World.hpp"
#include "tests/TestFramework.hpp"

using namespace pm;

namespace {

int countMat(const World& w, MatId m) {
    int n = 0;
    for (int y = 0; y < w.height(); ++y)
        for (int x = 0; x < w.width(); ++x)
            if (w.at(x, y).mat == m) ++n;
    return n;
}

void buildBox(World& w) {
    for (int x = 0; x < w.width(); ++x) {
        w.place(x, w.height() - 1, mat::Bedrock);
        w.place(x, 0, mat::Bedrock);
    }
    for (int y = 0; y < w.height(); ++y) {
        w.place(0, y, mat::Bedrock);
        w.place(w.width() - 1, y, mat::Bedrock);
    }
}

} // namespace

TEST_CASE("sand falls and is conserved") {
    initMaterials();
    ThreadPool pool(3);
    World w(256, 256, 1);
    buildBox(w);
    for (int y = 20; y < 60; ++y)
        for (int x = 100; x < 160; ++x) w.place(x, y, mat::Sand);
    const int before = countMat(w, mat::Sand);
    for (int i = 0; i < 400; ++i) w.step(pool);
    CHECK(countMat(w, mat::Sand) == before);
    // Everything should have landed: nothing left in the original spawn rows.
    int high = 0;
    for (int y = 20; y < 60; ++y)
        for (int x = 1; x < 255; ++x)
            if (w.at(x, y).mat == mat::Sand) ++high;
    CHECK(high == 0);
}

TEST_CASE("water spreads, is conserved and settles to sleep") {
    initMaterials();
    ThreadPool pool(3);
    World w(256, 128, 2);
    buildBox(w);
    for (int y = 10; y < 70; ++y)
        for (int x = 20; x < 60; ++x) w.place(x, y, mat::Water);
    const int before = countMat(w, mat::Water);
    for (int i = 0; i < 1500; ++i) w.step(pool);
    // Stray lone droplets are allowed to dry up, but the bulk must be conserved.
    const int after = countMat(w, mat::Water);
    CHECK(after <= before);
    CHECK(after >= before * 97 / 100);
    int topRow = 128;
    for (int y = 0; y < 128 && topRow == 128; ++y)
        for (int x = 1; x < 255; ++x)
            if (w.at(x, y).mat == mat::Water) {
                topRow = y;
                break;
            }
    CHECK(topRow > 100);
    CHECK(w.activeChunkCount() <= 4);
}

TEST_CASE("water and lava react into obsidian and steam") {
    initMaterials();
    ThreadPool pool(3);
    World w(128, 128, 3);
    buildBox(w);
    for (int x = 1; x < 127; ++x)
        for (int y = 110; y < 127; ++y) w.place(x, y, mat::Lava);
    for (int x = 40; x < 80; ++x)
        for (int y = 60; y < 70; ++y) w.place(x, y, mat::Water);
    for (int i = 0; i < 300; ++i) w.step(pool);
    CHECK(countMat(w, mat::Obsidian) > 0);
}

TEST_CASE("wood burns away") {
    initMaterials();
    ThreadPool pool(3);
    World w(128, 128, 4);
    buildBox(w);
    for (int x = 30; x < 90; ++x)
        for (int y = 100; y < 104; ++y) w.place(x, y, mat::Wood);
    const int before = countMat(w, mat::Wood);
    w.ignite(30, 100);
    for (int i = 0; i < 2000; ++i) w.step(pool);
    CHECK(countMat(w, mat::Wood) < before / 4);
}

TEST_CASE("idle world sleeps") {
    initMaterials();
    ThreadPool pool(3);
    World w(512, 512, 5);
    buildBox(w);
    for (int i = 0; i < 10; ++i) w.step(pool);
    CHECK(w.activeChunkCount() == 0);
}

TEST_CASE("burning gunpowder merges into blasts") {
    initMaterials();
    ThreadPool pool;
    World w(128, 96, 5u);
    buildBox(w);
    // Two separate kegs of gunpowder, lit.
    for (int y = 70; y < 80; ++y)
        for (int x = 10; x < 18; ++x) w.place(x, y, mat::Gunpowder);
    for (int y = 70; y < 80; ++y)
        for (int x = 90; x < 98; ++x) w.place(x, y, mat::Gunpowder);
    w.wakeAll();
    w.ignite(14, 70);
    w.ignite(94, 70);
    std::vector<SimBlast> blasts;
    for (int i = 0; i < 240 && blasts.empty(); ++i) {
        w.step(pool);
        blasts = w.takeBlasts();
    }
    CHECK(!blasts.empty());
    CHECK(w.events().empty()); // taking the blasts consumes the events
    for (const SimBlast& b : blasts) {
        CHECK(b.grains > 0);
        CHECK(b.radius() >= 4.0f && b.radius() <= 26.0f);
        // Every blast sits on one of the two kegs.
        const bool left = b.center.x > 6.0f && b.center.x < 22.0f, right = b.center.x > 86.0f && b.center.x < 102.0f;
        CHECK(left || right);
    }
}

TEST_CASE("vines hang from rock and crumble when cut loose") {
    initMaterials();
    ThreadPool pool(3);
    World w(128, 128, 5);
    buildBox(w);
    for (int x = 30; x < 50; ++x) w.place(x, 20, mat::Rock);
    for (int y = 21; y < 41; ++y) {
        w.place(35, y, mat::Vine);
        w.place(44, y, mat::GlowVine);
    }
    for (int i = 0; i < 60; ++i) w.step(pool);
    CHECK(countMat(w, mat::Vine) == 20);
    CHECK(countMat(w, mat::GlowVine) == 20);

    // Blast the ceiling away: both strands crumble into falling litter and spores.
    for (int x = 30; x < 50; ++x) w.clear(x, 20);
    for (int i = 0; i < 200; ++i) w.step(pool);
    CHECK(countMat(w, mat::Vine) == 0);
    CHECK(countMat(w, mat::GlowVine) == 0);
    CHECK(countMat(w, mat::Litter) == 20);
    CHECK(countMat(w, mat::Spore) == 20);
}
