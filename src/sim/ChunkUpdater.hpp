#pragma once
// The cellular automaton rules. A ChunkUpdater simulates the dirty rectangle of one chunk
// for one tick. It may read and write cells up to kMaxReach cells outside its chunk, which
// the checkerboard schedule in World::step() guarantees is race-free.

#include <cstdint>

#include "core/Rng.hpp"
#include "sim/Codex.hpp"
#include "sim/World.hpp"

namespace pm {

class ChunkUpdater {
public:
    /// Maximum distance (in cells) a single update may read or write outside its chunk.
    /// Checked at compile time against the fall speed and by a test against liquid dispersion.
    static constexpr int kMaxReach = 12;

    ChunkUpdater(World& world, int cx, int cy);
    void run();

private:
    Cell& at(int x, int y) { return cells_[static_cast<size_t>(y) * w_ + x]; }
    bool inBounds(int x, int y) const {
        return static_cast<unsigned>(x) < static_cast<unsigned>(w_) &&
               static_cast<unsigned>(y) < static_cast<unsigned>(h_);
    }
    MatId matAt(int x, int y) const {
        return inBounds(x, y) ? cells_[static_cast<size_t>(y) * w_ + x].mat : static_cast<MatId>(mat::Bedrock);
    }
    const MaterialDef& def(MatId m) const { return defs_[m]; }

    void wake(int x, int y);
    void swap(int x0, int y0, int x1, int y1);
    /// Turns the cell into a fresh cell of `m` that keeps the cellflag bits in `carried` (air keeps none).
    void replace(int x, int y, MatId m, uint8_t carried = 0);
    /// Sets a flammable cell alight with chance flammability/256; true if it caught.
    bool igniteCell(int x, int y);
    /// Notes a transmutation that fired in this chunk this tick. Observation only: it draws no random
    /// numbers and writes no cells, so the simulation is the same with or without it.
    void witness(CodexEntry e) { codexSeen_ |= codexBit(e); }
    /// Ends a gas or fire cell's life: it becomes `d.decaysInto` with chance decayChance/256,
    /// otherwise it vanishes.
    void expire(int x, int y, const MaterialDef& d);
    /// Index into the 4-neighbour offsets of the first neighbour that puts out flames, or -1.
    int extinguisherDir(int x, int y) const;

    void updateCell(int x, int y);
    bool updateBurning(int x, int y);
    bool updateReactions(int x, int y);
    void updatePowder(int x, int y);
    void updateLiquid(int x, int y);
    void updateGas(int x, int y);
    void updateFire(int x, int y);
    /// Vines hang from the static matter above them; cut loose, they crumble and fall.
    void updateHanging(int x, int y);

    /// Can a falling cell with density `density` move into a cell holding `target`?
    bool canSink(uint16_t density, MatId target) const;
    /// Can a rising gas with density `density` move into a cell holding `target`?
    bool canRise(uint16_t density, MatId target) const;
    /// Moves a falling cell downwards using its accumulated velocity. Returns true if it moved.
    bool fall(int x, int y);

    World& world_;
    World::Chunk& chunk_; ///< the chunk this updater simulates
    Cell* cells_;
    const MaterialDef* defs_;
    int w_, h_;
    int x0_, y0_, x1_, y1_; ///< chunk bounds, inclusive
    DirtyRect local_;       ///< wakes landing inside this chunk (merged atomically at the end)
    uint32_t codexSeen_ = 0; ///< codexBit()s witnessed this tick, merged into this chunk's own record at the end
    FastRng rng_;
    bool leftToRight_; ///< row scan direction this tick (alternates to cancel directional bias)
};

} // namespace pm
