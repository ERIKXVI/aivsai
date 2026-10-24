#include "sim/World.hpp"

#include "core/ThreadPool.hpp"
#include "sim/ChunkUpdater.hpp"

namespace pm {

World::World(int width, int height, uint32_t seed)
    : w_(((width + kChunkSize - 1) >> kChunkShift) << kChunkShift),
      h_(((height + kChunkSize - 1) >> kChunkShift) << kChunkShift),
      cw_(w_ >> kChunkShift),
      ch_(h_ >> kChunkShift),
      seed_(seed),
      cells_(static_cast<size_t>(w_) * h_),
      chunks_(static_cast<size_t>(cw_) * ch_),
      rng_(seed) {}

void World::set(int x, int y, const Cell& c) {
    if (!inBounds(x, y)) return;
    at(x, y) = c;
    wake(x, y);
}

void World::place(int x, int y, MatId m) { set(x, y, makeCell(m, rng_.next())); }

bool World::placeIfEmpty(int x, int y, MatId m) {
    if (!inBounds(x, y)) return false;
    Phase p = phaseOf(at(x, y).mat);
    if (p != Phase::Empty && p != Phase::Gas) return false;
    place(x, y, m);
    return true;
}

void World::ignite(int x, int y) {
    if (!inBounds(x, y)) return;
    Cell& c = at(x, y);
    const MaterialDef& d = matDef(c.mat);
    if (d.flammability == 0 || c.burning()) return;
    c.flags |= cellflag::kBurning;
    c.life = static_cast<uint8_t>(std::max(1, d.burnTime - static_cast<int>(rng_.next() % (d.burnTime / 4 + 1))));
    wake(x, y);
}

void World::atomicMin(std::atomic<int>& a, int v) {
    int cur = a.load(std::memory_order_relaxed);
    while (v < cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}

void World::atomicMax(std::atomic<int>& a, int v) {
    int cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}

void World::expandNext(Chunk& c, int x0, int y0, int x1, int y1) {
    atomicMin(c.nextMinX, x0);
    atomicMin(c.nextMinY, y0);
    atomicMax(c.nextMaxX, x1);
    atomicMax(c.nextMaxY, y1);
}

void World::wakeRect(int x0, int y0, int x1, int y1) {
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, w_ - 1);
    y1 = std::min(y1, h_ - 1);
    if (x0 > x1 || y0 > y1) return;
    int cx0 = x0 >> kChunkShift, cx1 = x1 >> kChunkShift;
    int cy0 = y0 >> kChunkShift, cy1 = y1 >> kChunkShift;
    for (int cy = cy0; cy <= cy1; ++cy) {
        int by0 = std::max(y0, cy << kChunkShift);
        int by1 = std::min(y1, ((cy + 1) << kChunkShift) - 1);
        for (int cx = cx0; cx <= cx1; ++cx) {
            int bx0 = std::max(x0, cx << kChunkShift);
            int bx1 = std::min(x1, ((cx + 1) << kChunkShift) - 1);
            expandNext(chunk(cx, cy), bx0, by0, bx1, by1);
        }
    }
}

void World::pushEvent(const SimEvent& e) {
    std::lock_guard lock(eventMutex_);
    events_.push_back(e);
}

void World::step(ThreadPool& pool) {
    ++tick_;

    // Promote the regions woken during the previous tick (and by gameplay) to this tick.
    activeChunks_ = 0;
    for (auto& c : chunks_) {
        c.current.minX = c.nextMinX.exchange(1 << 30, std::memory_order_relaxed);
        c.current.minY = c.nextMinY.exchange(1 << 30, std::memory_order_relaxed);
        c.current.maxX = c.nextMaxX.exchange(-1, std::memory_order_relaxed);
        c.current.maxY = c.nextMaxY.exchange(-1, std::memory_order_relaxed);
        if (!c.current.empty()) ++activeChunks_;
    }
    if (activeChunks_ == 0) return;

    // Four checkerboard phases. Within a phase every active chunk is at least one chunk
    // away from any other, so their bounded writes into neighbours never overlap.
    // Alternating the phase order each tick removes directional bias at chunk seams.
    static constexpr int kPhaseOrder[2][4] = {{2, 3, 0, 1}, {3, 2, 1, 0}};
    for (int p = 0; p < 4; ++p) {
        int phase = kPhaseOrder[tick_ & 1][p];
        int px = phase & 1, py = phase >> 1;
        phaseJobs_.clear();
        for (int cy = ch_ - 1 - ((ch_ - 1 - py) & 1); cy >= 0; cy -= 2) {
            for (int cx = px; cx < cw_; cx += 2) {
                if (!chunk(cx, cy).current.empty()) phaseJobs_.push_back(cy * cw_ + cx);
            }
        }
        pool.parallelFor(static_cast<int>(phaseJobs_.size()), [this](int i) {
            int idx = phaseJobs_[static_cast<size_t>(i)];
            ChunkUpdater updater(*this, idx % cw_, idx / cw_);
            updater.run();
        });
    }
}

} // namespace pm
