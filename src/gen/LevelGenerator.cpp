#include "gen/LevelGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "core/Rng.hpp"
#include "core/ThreadPool.hpp"
#include "gen/GeneratorImpl.hpp"
#include "gen/Navigation.hpp"
#include "sim/World.hpp"

namespace pm {

namespace gen_detail {

Generator::Generator(BiomeId biome, int levelIndex, uint32_t seed, const GenOptions& opt)
    : w_(opt.width), h_(opt.height), opt_(opt), biome_(biome), b_(biomeDef(biome)), level_(levelIndex),
      seed_(seed), rng_(seed), surface_(levelIndex == 0), omen_(omenFor(levelIndex, seed)) {
    // The first stage begins outdoors on the mountainside, so it gets extra sky above.
    if (surface_) h_ += kSkyRows;
    // The World rounds its size up to whole chunks; generate exactly that area so the bottom
    // rows are painted (and sealed) too.
    w_ = World::roundUpToChunks(w_);
    h_ = World::roundUpToChunks(h_);
}

LevelLayout Generator::run(ThreadPool& pool) {
    open_.assign(static_cast<size_t>(w_) * h_, 0);
    planPath();
    carveNoiseCaves(pool);
    carvePath();
    carveChambers();
    carveBranches();
    smooth();
    sealBorders();
    carvePockets();

    LevelLayout out;
    out.world = std::make_unique<World>(w_, h_, seed_);
    World& world = *out.world;
    paintMaterials(world, pool);
    placeVegetation(world);
    if (surface_) placeTrees(world, out);
    placeStructures(world);
    placePools(world);
    placePowders(world);
    placeExitPlatform(world);
    placeArenaPlatforms(world);
    presimulate(world, pool);
    // Guarantee the route: progressively wider clearing until a player-sized body fits.
    for (int radius = kClearRadiusMin; radius <= kClearRadiusMax; radius += kClearRadiusStep) {
        clearPath(world, radius);
        if (pathExists(world, start_, exit_)) break;
    }

    out.biome = biome_;
    out.levelIndex = level_;
    out.seed = seed_;
    out.playerStart = start_;
    out.exitPos = exit_;
    out.mainPath = path_;
    out.omen = omen_;
    if (surface_) {
        out.skyline = skyline_;
        out.surfaceY = surfaceY_;
    }
    collectSpawns(world, out);
    placeTransmuter(world, out);
    placeVaults(world, out);
    return out;
}

} // namespace gen_detail

using gen_detail::Generator;

const OmenDef& omenDef(Omen omen) {
    static constexpr OmenDef kOmens[] = {
        {"", ""},
        {"Flooded", "The waters have risen."},
        {"Overgrown", "Life runs riot in the dark."},
        {"Volatile", "The air smells of black powder."},
        {"Gilded", "Gold glitters in the walls."},
        {"Teeming", "Something stirs in every shadow, and guards its treasure."},
    };
    static_assert(std::size(kOmens) == static_cast<size_t>(Omen::Count));
    return kOmens[static_cast<size_t>(omen) < std::size(kOmens) ? static_cast<size_t>(omen) : 0];
}

Omen omenFor(int levelIndex, uint32_t levelSeed) {
    if (levelIndex >= kFinalStage) return Omen::None;
    const uint32_t h = hash32(levelSeed ^ 0x0E1Eu), pick = hashCombine(h, 1u);
    if (levelIndex <= 0) {
        // The first stage is a first impression: only gentle omens, and only sometimes.
        static constexpr Omen kGentle[] = {Omen::Flooded, Omen::Overgrown, Omen::Gilded};
        return hashToUnit(h) < 0.25f ? kGentle[pick % std::size(kGentle)] : Omen::None;
    }
    if (hashToUnit(h) >= 0.45f) return Omen::None;
    return static_cast<Omen>(1 + pick % (static_cast<uint32_t>(Omen::Count) - 1));
}

std::vector<uint32_t> pruneTree(const World& world, TreeShape& tree) {
    const auto width = static_cast<uint32_t>(world.width());
    auto isPlant = [&](uint32_t cell) {
        const MatId m = world.at(static_cast<int>(cell % width), static_cast<int>(cell / width)).mat;
        return m == mat::Wood || m == mat::Leaves;
    };
    auto indexOf = [&](uint32_t cell) -> long {
        auto it = std::lower_bound(tree.cells.begin(), tree.cells.end(), cell);
        return it != tree.cells.end() && *it == cell ? static_cast<long>(it - tree.cells.begin()) : -1;
    };
    // Flood fill from the root through the tree's surviving cells.
    std::vector<uint8_t> reached(tree.cells.size(), 0);
    std::vector<uint32_t> frontier;
    const auto root = static_cast<uint32_t>(tree.rootY) * width + static_cast<uint32_t>(tree.rootX);
    if (const long r = indexOf(root); r >= 0 && isPlant(root)) {
        reached[static_cast<size_t>(r)] = 1;
        frontier.push_back(root);
    }
    while (!frontier.empty()) {
        const uint32_t cell = frontier.back();
        frontier.pop_back();
        const int x = static_cast<int>(cell % width), y = static_cast<int>(cell / width);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if ((dx == 0 && dy == 0) || !world.inBounds(x + dx, y + dy)) continue;
                const auto next = static_cast<uint32_t>(y + dy) * width + static_cast<uint32_t>(x + dx);
                const long k = indexOf(next);
                if (k < 0 || reached[static_cast<size_t>(k)] || !isPlant(next)) continue;
                reached[static_cast<size_t>(k)] = 1;
                frontier.push_back(next);
            }
    }
    std::vector<uint32_t> kept, severed;
    for (size_t k = 0; k < tree.cells.size(); ++k) {
        if (!isPlant(tree.cells[k])) continue;
        (reached[k] ? kept : severed).push_back(tree.cells[k]);
    }
    tree.cells = std::move(kept);
    return severed;
}

LevelLayout generateLevel(BiomeId biome, int levelIndex, uint32_t seed, ThreadPool& pool, const GenOptions& options) {
    Generator gen(biome, levelIndex, seed, options);
    LevelLayout layout = gen.run(pool);
    return layout;
}

bool pathExists(const World& world, Vec2 from, Vec2 to, std::vector<uint8_t>* reachedBlocks) {
    NavGrid nav;
    nav.build(world);
    const bool goalFits = nav.computeDistances(to); // distances are reset even when this fails
    if (reachedBlocks) {
        const std::vector<int>& dist = nav.distances();
        reachedBlocks->resize(dist.size());
        for (size_t i = 0; i < dist.size(); ++i) (*reachedBlocks)[i] = dist[i] != NavGrid::kUnreachable ? 1 : 0;
    }
    return goalFits && nav.distanceAt(from) != NavGrid::kUnreachable;
}

LevelLayout generateSanctum(int levelIndex, uint32_t seed, ThreadPool& pool) {
    constexpr int kW = 448, kH = 256;
    LevelLayout out;
    out.world = std::make_unique<World>(kW, kH, seed);
    out.biome = BiomeId::Sanctum;
    out.levelIndex = levelIndex;
    out.seed = seed;
    World& w = *out.world;

    const int floorY = 190, ceilY = 60, left = 30, right = kW - 30;
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            bool inside = x > left && x < right && y > ceilY && y < floorY;
            // Vaulted ceiling.
            float arch = 30.0f * std::sin(kPi * static_cast<float>(x - left) / static_cast<float>(right - left));
            if (y < ceilY + 1 + static_cast<int>(std::max(0.0f, 30.0f - arch))) inside = false;
            if (inside) continue;
            bool edge = x < 3 || y < 3 || x >= kW - 3 || y >= kH - 3;
            MatId m = edge ? mat::Bedrock : mat::Brick;
            Cell c = makeCell(m, hash2(x, y, seed));
            // Brick courses: darker mortar lines.
            bool mortar = (y % 6 == 0) || ((x + ((y / 6) % 2) * 6) % 12 == 0);
            c.var = static_cast<uint8_t>(mortar ? 20 : 120 + (hash2(x, y, 5u) & 63u));
            w.at(x, y) = c;
        }
    }
    // Rune trim along the floor.
    for (int x = left; x <= right; ++x) w.place(x, floorY, mat::Rune);
    // Healing font: a basin of Vitae.
    const int fontX = kW / 2;
    for (int x = fontX - 16; x <= fontX + 16; ++x) {
        for (int y = floorY - 7; y < floorY; ++y) {
            bool wall = x == fontX - 16 || x == fontX + 16 || y == floorY - 1;
            if (wall) w.place(x, y, mat::Rune);
            else if (y > floorY - 6) w.place(x, y, mat::Vitae);
        }
    }
    w.wakeAll();
    for (int i = 0; i < 30; ++i) w.step(pool);
    w.events().clear();

    out.playerStart = {static_cast<float>(left + 24), static_cast<float>(floorY - 1)};
    out.exitPos = {static_cast<float>(right - 24), static_cast<float>(floorY - 1)};
    // Shop pedestals (Spell), a perk altar (Altar) and a free potion.
    for (int i = 0; i < 4; ++i)
        out.spawns.push_back({SpawnKind::Spell, {static_cast<float>(left + 60 + i * 26), static_cast<float>(floorY - 1)}, 0.0f});
    for (int i = 0; i < 3; ++i)
        out.spawns.push_back({SpawnKind::Altar, {static_cast<float>(fontX + 50 + i * 28), static_cast<float>(floorY - 1)}, 0.0f});
    out.spawns.push_back({SpawnKind::Lantern, {static_cast<float>(left + 90), static_cast<float>(ceilY + 22)}, 0.0f});
    out.spawns.push_back({SpawnKind::Lantern, {static_cast<float>(right - 90), static_cast<float>(ceilY + 22)}, 0.0f});
    out.spawns.push_back({SpawnKind::Lantern, {static_cast<float>(fontX), static_cast<float>(ceilY + 2)}, 0.0f});
    return out;
}

} // namespace pm
