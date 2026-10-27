// Simulation invariants: movement conserves matter, reactions behave, chunks go to sleep,
// and vines cut loose from the ceiling fall.
#include <memory>
#include <vector>

#include "core/ThreadPool.hpp"
#include "sim/ChunkUpdater.hpp"
#include "sim/Particles.hpp"
#include "sim/World.hpp"
#include "tests/TestFramework.hpp"
#include "tests/TestWorld.hpp"

using namespace pm;

using pm::test::buildBox;
using pm::test::countMat;

TEST_CASE("the matter lens speaks only of notable matter") {
    initMaterials();
    CHECK(!matDescription(mat::Water).empty());
    CHECK(!matDescription(mat::Gunpowder).empty());
    CHECK(!matDescription(mat::ToxicGas).empty()); // harmful: read off its contact damage
    CHECK(matDescription(mat::Bedrock) == "Unbreakable.");
    CHECK(matDescription(mat::Empty).empty());
    CHECK(matDescription(mat::Rock).empty());
    CHECK(matDescription(mat::Leaves).empty());
    CHECK(matDescription(mat::Sand).empty());
}

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

TEST_CASE("a pool in a salt basin turns to brine and settles to sleep") {
    // Salt walls must not keep dissolving into an ever-growing pool: the stage II caverns are
    // lined with salt, and a pool that never settles keeps its chunks simulating forever.
    initMaterials();
    ThreadPool pool(3);
    World w(256, 128, 4);
    buildBox(w);
    for (int y = 60; y < 127; ++y)
        for (int x = 1; x < 255; ++x) w.place(x, y, mat::SaltRock);
    for (int y = 60; y < 110; ++y)
        for (int x = 40; x < 120; ++x) w.place(x, y, mat::Water);
    const int salt = countMat(w, mat::SaltRock);
    for (int i = 0; i < 4000; ++i) w.step(pool);
    CHECK(countMat(w, mat::SaltRock) == salt);
    CHECK(w.activeChunkCount() <= 4);
}

TEST_CASE("matter launched inside rock is pushed out, not lost") {
    // A flask poured with the wand tip buried in the floor: the cells around the landing point
    // are all rock, so the matter has to climb further to find room.
    initMaterials();
    World w(128, 128, 6);
    buildBox(w);
    for (int y = 60; y < 127; ++y)
        for (int x = 1; x < 127; ++x) w.place(x, y, mat::Rock);
    ParticleSystem particles;
    particles.addMatter({64.5f, 62.5f}, {200.0f, 40.0f}, makeCell(mat::Water, 1u)); // three cells deep
    for (int tick = 0; tick < 10 && particles.count() > 0; ++tick) particles.update(w, 1.0f / 60.0f);
    CHECK(particles.count() == 0);
    CHECK(countMat(w, mat::Water) == 1);
    CHECK(w.at(64, 59).mat == mat::Water); // the first open cell above it
}

TEST_CASE("matter that lands in a full pocket does not pass through the rock above") {
    // Spray straight up at a shelf three cells thick: once the open cells beside the landing point
    // have filled, later drops must not be written above the shelf.
    initMaterials();
    World w(128, 128, 6);
    buildBox(w);
    for (int y = 60; y < 63; ++y)
        for (int x = 1; x < 127; ++x) w.place(x, y, mat::Rock);
    ParticleSystem particles;
    for (int i = 0; i < 12; ++i) particles.addMatter({64.5f, 63.5f}, {0.0f, -200.0f}, makeCell(mat::Water, 1u + static_cast<uint32_t>(i)));
    particles.update(w, 1.0f / 60.0f);
    int above = 0;
    for (int y = 1; y < 60; ++y)
        for (int x = 1; x < 127; ++x) above += w.at(x, y).mat == mat::Water ? 1 : 0;
    CHECK(above == 0);
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

TEST_CASE("the simulation does not depend on the worker count") {
    // Chunks updated in one checkerboard phase run in any order on any worker; the result must
    // be the same, or a Daily Rite seed would play out differently on different machines.
    initMaterials();
    auto run = [](ThreadPool& pool) {
        auto w = std::make_unique<World>(256, 192, 7);
        buildBox(*w);
        for (int x = 20; x < 236; ++x) w->place(x, 120, mat::Rock);
        for (int x = 60; x < 200; x += 9) w->clear(x, 120); // drains through the shelf
        for (int y = 20; y < 60; ++y)
            for (int x = 20; x < 236; ++x) {
                const int band = (x / 24) % 6;
                const MatId m = band == 0 ? mat::Sand : band == 1 ? mat::Water : band == 2 ? mat::Oil
                              : band == 3 ? mat::Lava : band == 4 ? mat::Gunpowder : mat::Acid;
                w->place(x, y, m);
            }
        for (int x = 20; x < 236; x += 16) w->ignite(x, 59);
        for (int i = 0; i < 300; ++i) w->step(pool);
        return w;
    };
    ThreadPool one(1), many(4);
    const std::unique_ptr<World> a = run(one), b = run(many);
    CHECK(pm::test::sameCells(*a, *b));
}

TEST_CASE("liquids never flow further in a tick than the chunk updater may reach") {
    initMaterials();
    // A sideways flow moves up to `dispersion` cells; past kMaxReach, chunks updated in the
    // same checkerboard phase could race.
    for (int id = 0; id < kMaterialCount; ++id) CHECK(matDef(static_cast<MatId>(id)).dispersion <= ChunkUpdater::kMaxReach);
}
