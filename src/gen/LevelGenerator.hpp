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

enum class SpawnKind : uint8_t { GroundEnemy, AirEnemy, Chest, Potion, Spell, Lantern, Altar };

struct SpawnPoint {
    SpawnKind kind;
    Vec2 pos;          ///< world cells; ground spawns sit on the floor surface
    float difficulty;  ///< 0 near the entrance, 1 near the exit
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
    int surfaceY = -1;                ///< > 0 when the level opens onto the sky
    std::vector<int> skyline;         ///< per column: first ground row (surface levels only)
};

struct GenOptions {
    int width = 832;
    int height = 1472;
    int presimTicks = 200;
};

LevelLayout generateLevel(BiomeId biome, int levelIndex, uint32_t seed, ThreadPool& pool,
                          const GenOptions& options = {});

/// Builds the hand-shaped sanctum between stages (shop, healing font, perk altar, exit).
LevelLayout generateSanctum(int levelIndex, uint32_t seed, ThreadPool& pool);

/// True if a player-sized body can travel from `from` to `to` through open space
/// (liquids count as open). Used by tests and as a generation safety net.
/// `reachedBlocks`, when given, receives the BFS visit mask over 4x4-cell blocks (debugging).
bool pathExists(const World& world, Vec2 from, Vec2 to, std::vector<uint8_t>* reachedBlocks = nullptr);

} // namespace pm
