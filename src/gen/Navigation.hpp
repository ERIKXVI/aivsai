#pragma once
// Coarse navigation grid over the cell world. The world is split into 4x4-cell blocks; a
// block is walkable when a player-sized body (2x3 blocks) fits there. A breadth-first
// distance field towards a goal lets AI (and tests) find routes through the caves.

#include <cstdint>
#include <vector>

#include "core/Math.hpp"

namespace pm {

class World;

class NavGrid {
public:
    static constexpr int kBlock = 4;
    static constexpr int kUnreachable = -1;

    /// Samples the world's current collision state.
    void build(const World& world);
    /// Breadth-first distances (in blocks) from every block to the block nearest `goal`.
    /// Returns false if the goal has no free space around it.
    bool computeDistances(Vec2 goal);

    int width() const { return bw_; }
    int height() const { return bh_; }
    /// Distance from the nearest block (within `searchRadius` blocks of `p`) where a body
    /// fits, or kUnreachable.
    int distanceAt(Vec2 p, int searchRadius = 6) const;
    /// World-space centre of the neighbouring block that is one step closer to the goal.
    /// Returns false when `p` is not on a reachable block.
    bool nextStep(Vec2 p, Vec2& out) const;
    /// True if a body fits with its feet in block (bx, by).
    bool fits(int bx, int by) const;
    const std::vector<int>& distances() const { return dist_; }

private:
    bool blockFree(int bx, int by) const;
    bool findFit(Vec2 p, int& bx, int& by, int searchRadius = 6) const;

    int bw_ = 0, bh_ = 0;
    std::vector<uint8_t> free_;
    std::vector<int> dist_;
};

} // namespace pm
