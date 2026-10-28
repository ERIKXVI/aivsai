#pragma once
// Private to the level generator: the Generator class shared by its implementation files.
// LevelGenerator.cpp drives the pipeline; GenCarve.cpp shapes the open space, GenDress.cpp
// paints and furnishes it, and GenPopulate.cpp places spawns, transmuters and vaults.

#include <algorithm>
#include <array>
#include <vector>

#include "core/Rng.hpp"
#include "gen/LevelGenerator.hpp"

namespace pm {

class NavGrid;
class ThreadPool;
struct VaultPlan;

namespace gen_detail {

inline constexpr int kBorder = 4;
inline constexpr int kSkyRows = 200; ///< open sky added above the first stage
/// Route-clearing passes after presimulation (run() -> clearPath()): radii 7, 10, 13. Only the
/// final, widest pass re-carves rock outside the planned open space.
inline constexpr int kClearRadiusMin = 7, kClearRadiusStep = 3, kClearRadiusMax = 13;
static_assert((kClearRadiusMax - kClearRadiusMin) % kClearRadiusStep == 0, "the passes must reach kClearRadiusMax");

/// Picks from a small weighted table; entries with zero weight are skipped.
template <size_t N>
MatId pickWeighted(const std::array<WeightedMat, N>& table, Rng& rng) {
    float total = 0.0f;
    for (const auto& e : table) total += e.weight;
    float r = rng.uniform() * total;
    for (const auto& e : table) {
        if (e.weight <= 0.0f) continue;
        if (r < e.weight) return e.mat;
        r -= e.weight;
    }
    return table[0].mat;
}

class Generator {
public:
    Generator(BiomeId biome, int levelIndex, uint32_t seed, const GenOptions& opt);
    LevelLayout run(ThreadPool& pool);

private:
    size_t idx(int x, int y) const { return static_cast<size_t>(y) * w_ + x; }
    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }
    bool isOpen(int x, int y) const { return inBounds(x, y) && open_[idx(x, y)] != 0; }
    bool aboveGround(int x, int y) const { return surface_ && y < skyline_[static_cast<size_t>(x)]; }
    /// Vegetation density after the omen (an overgrown stage is far greener).
    float vegetation() const { return omen_ == Omen::Overgrown ? std::min(1.0f, b_.vegetation + 0.45f) : b_.vegetation; }

    // Shaping the open space (GenCarve.cpp).
    void carveCircle(float cx, float cy, float r, float roughness = 0.0f);
    void carveSegment(Vec2 a, Vec2 b, float baseRadius, float radiusVar, float wobble, uint32_t salt);
    void planSkyline();
    void flattenSkyline(int cx, int halfWidth, int height);
    void planPath();
    void carveNoiseCaves(ThreadPool& pool);
    void carvePath();
    void carveChambers();
    void carveBranches();
    void smooth();
    void sealBorders();
    void carvePockets();
    /// The final stage ends in a vast furnace hall where the Rebis waits above the portal.
    void carveArena();

    // Painting and furnishing (GenDress.cpp).
    void placeArenaPlatforms(World& world);
    void paintMaterials(World& world, ThreadPool& pool);
    bool isFloor(const World& w, int x, int y) const;
    void placeVegetation(World& world);
    void placeTrees(World& world, LevelLayout& out);
    void placeMushroom(World& world, int x, int y);
    void placeStructures(World& world);
    float distanceToPath(Vec2 p) const;
    void placePools(World& world);
    void placePowders(World& world);
    void placeExitPlatform(World& world);
    void presimulate(World& world, ThreadPool& pool);
    void clearPath(World& world, int radius);
    /// Clears loose powder and liquid from the pocket the alchemist arrives in (on a surface stage
    /// the start lies off the route that clearPath keeps open).
    void clearStart(World& world);

    // Spawns, transmuters and vaults (GenPopulate.cpp).
    /// A runestone basin that rewards the alchemist for pouring in a particular material.
    void placeTransmuter(World& world, LevelLayout& out);
    /// True if every cell of the rectangle is solid ground the vault may replace.
    bool solidRect(const World& world, int x0, int y0, int w, int h) const;
    /// Like solidRect, but up to `maxOpen` of the area may be cave (a lair's shell overwrites it).
    bool mostlySolidRect(const World& world, int x0, int y0, int w, int h, float maxOpen) const;
    /// Sizes `plan` for its kind and picks a random corner for it at or below row `top`; false if the level is too small.
    bool pickVaultSite(VaultPlan& plan, int top);
    /// True if a vault centred on `c` keeps clear of the entrance, exit, route, arena and the vaults `placed`, in sound rock.
    bool vaultSiteSuitable(const World& world, const VaultPlan& plan, Vec2 c, const std::vector<Rectf>& placed) const;
    /// Tries both walls, in random order, for a tunnel to a reachable cave; sets plan.doorSide, returns its end column or -1.
    int findDoorTunnel(const World& world, const NavGrid& nav, VaultPlan& plan, int door, const std::vector<Rectf>& keepOut);
    /// Builds the vault and its passage to `tunnelEnd`; if the room is then out of reach, undoes it all and returns false.
    bool stampVault(World& world, std::vector<SpawnPoint>& spawns, const VaultPlan& plan, int door, int tunnelEnd);
    void placeVaults(World& world, LevelLayout& out);
    bool spaceAbove(const World& w, int x, int y, int height, int halfWidth) const;
    /// A spawn's difficulty at row `y`: 0 at the entrance's depth, rising to 1 at the exit's.
    float difficultyAt(float y) const;
    /// Scatters ground and air enemies through the caves (more on a teeming stage), away from the entrance and exit.
    void scatterEnemies(const World& world, std::vector<SpawnPoint>& spawns);
    /// Places a `kind` spawn on open floor with headroom near `around` (within `spread`), if 80 tries find one.
    void placeOnFloor(const World& world, std::vector<SpawnPoint>& spawns, SpawnKind kind, Vec2 around, float spread);
    /// Scatters chests and potions through the chambers, spells and potions through the pockets, and chests on the route.
    void scatterTreasure(const World& world, std::vector<SpawnPoint>& spawns);
    /// Hangs lanterns from the ceiling at intervals along the main path.
    void hangLanterns(const World& world, std::vector<SpawnPoint>& spawns) const;
    void collectSpawns(const World& world, LevelLayout& out);

    int w_, h_;
    GenOptions opt_;
    BiomeId biome_;
    const BiomeDef& b_;
    int level_;
    uint32_t seed_;
    Rng rng_;
    std::vector<uint8_t> open_;
    std::vector<Vec2> path_;
    std::vector<Vec2> chambers_;
    std::vector<Vec2> pockets_;
    Vec2 start_, exit_;
    bool surface_ = false;
    Omen omen_ = Omen::None;
    int surfaceY_ = 0;
    Rectf arena_{0.0f, 0.0f, 0.0f, 0.0f}; ///< boss arena centre (x, y) and radii (w, h)
    std::vector<int> skyline_;
};

} // namespace gen_detail

} // namespace pm
