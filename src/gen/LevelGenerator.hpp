#pragma once
// Procedural level generation. A level is carved from noise caves plus a guaranteed winding
// path from the entrance to the exit portal, dressed with biome materials, then
// pre-simulated so liquids and powders settle naturally before the player arrives.

#include <memory>
#include <vector>

#include "core/Math.hpp"
#include "gen/Biome.hpp"

namespace pm {

class World;
class ThreadPool;

/// A stage's omen: an occasional twist on its biome, announced on the title card.
enum class Omen : uint8_t { None, Flooded, Overgrown, Volatile, Gilded, Teeming, Count };

struct OmenDef {
    const char* name;
    const char* description;
};

const OmenDef& omenDef(Omen omen);
/// The omen of a stage, decided by its level seed. The first stage draws only gentle omens (as
/// often as deeper stages draw any); the Athanor never has one.
Omen omenFor(int levelIndex, uint32_t levelSeed);

enum class SpawnKind : uint8_t { GroundEnemy, AirEnemy, Chest, Potion, Spell, Lantern, Altar, Transmuter, Wand, Guardian, LairGuardian,
                               RareSpell };

struct SpawnPoint {
    SpawnKind kind;
    Vec2 pos;          ///< world cells; ground spawns sit on the floor surface
    float difficulty;  ///< 0 near the entrance, 1 near the exit
};

/// A tree on the surface: the cells it was grown from and the root of its trunk, so the
/// level can tell when part of it has been cut loose.
struct TreeShape {
    int rootX = 0, rootY = 0;     ///< bottom trunk cell, resting on the ground
    std::vector<uint32_t> cells;  ///< sorted world cell indices (y * width + x) of its wood and leaves
};

struct LevelLayout {
    std::unique_ptr<World> world;
    BiomeId biome = BiomeId::Nigredo;
    int levelIndex = 0;
    uint32_t seed = 0;
    Vec2 playerStart;
    Vec2 exitPos;
    std::vector<Vec2> mainPath;       ///< polyline of the guaranteed route
    std::vector<SpawnPoint> spawns;
    std::vector<Rectf> vaults;        ///< outer bounds of the set-piece rooms
    Omen omen = Omen::None;
    int surfaceY = -1;                ///< > 0 when the level opens onto the sky
    std::vector<int> skyline;         ///< per column: first ground row (surface levels only)
    std::vector<TreeShape> trees;
};

/// Height above a vault's outer bottom edge of the point generation guarantees reachable.
constexpr float kVaultProbeHeight = 10.0f;

/// The point inside a vault (outer bounds, walls included) that generation checks is
/// reachable from the entrance before keeping the vault: centred, just above its floor.
constexpr Vec2 vaultProbePoint(const Rectf& v) { return {v.x + v.w * 0.5f, v.y + v.h - kVaultProbeHeight}; }

struct GenOptions {
    int width = 832;
    int height = 1472;
    int presimTicks = 200;
};

LevelLayout generateLevel(BiomeId biome, int levelIndex, uint32_t seed, ThreadPool& pool,
                          const GenOptions& options = {});

/// Drops from `tree` every cell that is no longer wood or leaves or no longer joined to the
/// root (through its own cells, diagonals included), and returns the ones still standing but
/// cut loose, which should fall.
std::vector<uint32_t> pruneTree(const World& world, TreeShape& tree);

/// Builds the hand-shaped sanctum between stages (shop, healing font, perk altar, exit).
/// `leanFont` (the Lean Sanctums ordeal) fills the font with plain water instead of Vitae.
LevelLayout generateSanctum(int levelIndex, uint32_t seed, ThreadPool& pool, bool leanFont = false);

/// True if a player-sized body can travel from `from` to `to` through open space
/// (liquids count as open). Used by tests and as a generation safety net.
/// `reachedBlocks`, when given, always receives the BFS visit mask over NavGrid::kBlock-sized
/// blocks (row-major, (world.width() / kBlock) x (world.height() / kBlock); all zero when `to`
/// has no free space). Debugging only.
bool pathExists(const World& world, Vec2 from, Vec2 to, std::vector<uint8_t>* reachedBlocks = nullptr);

} // namespace pm
