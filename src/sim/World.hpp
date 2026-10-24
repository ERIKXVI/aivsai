#pragma once
// The simulated world: a dense grid of cells split into 64x64 chunks. Each chunk tracks a
// dirty rectangle so only regions where something can change are simulated. A tick runs
// the cellular automaton over all dirty chunks in four checkerboard phases on a thread
// pool; cells never move more than a few pixels per tick, so chunks in the same phase can
// never touch the same memory.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "core/Math.hpp"
#include "core/Rng.hpp"
#include "sim/Cell.hpp"
#include "sim/Material.hpp"

namespace pm {

class ThreadPool;

struct DirtyRect {
    int minX = 1 << 30, minY = 1 << 30, maxX = -1, maxY = -1;

    bool empty() const { return maxX < minX || maxY < minY; }
    void include(int x0, int y0, int x1, int y1) {
        minX = std::min(minX, x0);
        minY = std::min(minY, y0);
        maxX = std::max(maxX, x1);
        maxY = std::max(maxY, y1);
    }
    void reset() { *this = DirtyRect{}; }
};

/// Something the simulation wants the game layer to handle after the tick.
struct SimEvent {
    enum class Type : uint8_t { Explosion, Fizz };
    Type type = Type::Explosion;
    int x = 0, y = 0;
    float power = 0.0f;
};

class World {
public:
    static constexpr int kChunkShift = 6;
    static constexpr int kChunkSize = 1 << kChunkShift;

    /// Width and height are rounded up to whole chunks.
    World(int width, int height, uint32_t seed);

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    int width() const { return w_; }
    int height() const { return h_; }
    int chunksX() const { return cw_; }
    int chunksY() const { return ch_; }
    uint32_t tick() const { return tick_; }
    uint32_t seed() const { return seed_; }

    bool inBounds(int x, int y) const {
        return static_cast<unsigned>(x) < static_cast<unsigned>(w_) &&
               static_cast<unsigned>(y) < static_cast<unsigned>(h_);
    }
    Cell& at(int x, int y) { return cells_[static_cast<size_t>(y) * w_ + x]; }
    const Cell& at(int x, int y) const { return cells_[static_cast<size_t>(y) * w_ + x]; }
    /// Bounds-checked material lookup; outside the world reads as bedrock.
    MatId matAt(int x, int y) const { return inBounds(x, y) ? at(x, y).mat : static_cast<MatId>(mat::Bedrock); }

    /// Writes a cell and wakes its neighbourhood. Out-of-bounds writes are ignored.
    void set(int x, int y, const Cell& c);
    /// Places a freshly randomized cell of `m`.
    void place(int x, int y, MatId m);
    void clear(int x, int y) { set(x, y, Cell{}); }
    /// Places `m` only where the cell is currently empty (or replaceable gas).
    bool placeIfEmpty(int x, int y, MatId m);
    void ignite(int x, int y);

    /// Marks the 3x3 neighbourhood of (x, y) for simulation next tick. Thread-safe.
    void wake(int x, int y) { wakeRect(x - 1, y - 1, x + 1, y + 1); }
    /// Marks an inclusive rectangle for simulation next tick. Thread-safe.
    void wakeRect(int x0, int y0, int x1, int y1);
    void wakeAll() { wakeRect(0, 0, w_ - 1, h_ - 1); }

    /// Advances the cellular automaton by one tick.
    void step(ThreadPool& pool);

    /// Events raised during the last step(s). The game layer clears this after handling.
    std::vector<SimEvent>& events() { return events_; }
    void pushEvent(const SimEvent& e);

    /// Rectangle (in cells) that was simulated last tick for chunk (cx, cy); for debugging.
    const DirtyRect& chunkActiveRect(int cx, int cy) const { return chunks_[cy * cw_ + cx].current; }
    int activeChunkCount() const { return activeChunks_; }

    uint32_t randomBits() { return rng_.next(); }
    Rng& rng() { return rng_; }

private:
    friend class ChunkUpdater;

    struct Chunk {
        DirtyRect current; ///< region simulated this tick (main thread writes between ticks)
        std::atomic<int> nextMinX{1 << 30}, nextMinY{1 << 30}, nextMaxX{-1}, nextMaxY{-1};
    };

    Chunk& chunk(int cx, int cy) { return chunks_[cy * cw_ + cx]; }
    static void atomicMin(std::atomic<int>& a, int v);
    static void atomicMax(std::atomic<int>& a, int v);
    void expandNext(Chunk& c, int x0, int y0, int x1, int y1);

    int w_, h_, cw_, ch_;
    uint32_t seed_;
    uint32_t tick_ = 0;
    int activeChunks_ = 0;
    std::vector<Cell> cells_;
    std::vector<Chunk> chunks_;
    std::vector<int> phaseJobs_;
    std::vector<SimEvent> events_;
    std::mutex eventMutex_;
    Rng rng_;
};

} // namespace pm
