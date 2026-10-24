#pragma once
// Vaults: set-piece rooms stamped into solid rock off the main route - an alchemist's
// laboratory, a powder magazine, a flooded crypt, a guarded treasury and a healing spring.
// The generator picks a site, opens a doorway towards reachable caves and asks this module
// to build the room; treasure and guardians are emitted as spawn points.

#include <cstdint>
#include <vector>

#include "gen/Biome.hpp"
#include "gen/LevelGenerator.hpp"

namespace pm {

class World;
class Rng;

enum class VaultKind : uint8_t { Laboratory, Magazine, Crypt, Treasury, Spring, Count };

struct VaultPlan {
    VaultKind kind = VaultKind::Laboratory;
    int x0 = 0, y0 = 0; ///< top-left corner of the outer shell
    int w = 0, h = 0;   ///< outer size, walls included
    int doorSide = 1;   ///< -1 = doorway in the left wall, +1 = right wall
};

constexpr int kVaultWall = 3;
/// Doorway and passage height: tall enough for the navigation grid's 3-block body test
/// at any alignment, so the bot (and the reachability check) agree with the player.
constexpr int kVaultDoorHeight = 16;

/// Outer dimensions of a vault kind.
void vaultSize(VaultKind kind, int& w, int& h);
/// Bottom row of the doorway (a cell row inside the room); see kVaultDoorHeight.
int vaultDoorRow(const VaultPlan& plan);
/// Picks a vault kind that suits the biome, avoiding kinds set in `usedMask`.
VaultKind pickVaultKind(BiomeId biome, Rng& rng, uint32_t usedMask);
/// Builds the room with its doorway open and appends its treasure and guardians.
void buildVault(World& world, const VaultPlan& plan, Rng& rng, std::vector<SpawnPoint>& spawns);

} // namespace pm
