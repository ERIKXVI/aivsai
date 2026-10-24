#include "sim/ChunkUpdater.hpp"

#include <algorithm>
#include <utility>

namespace pm {

namespace {

constexpr int kDx4[4] = {0, 1, 0, -1};
constexpr int kDy4[4] = {-1, 0, 1, 0};
constexpr int kDx8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDy8[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

constexpr int kGravity = 3;   // 1/8 cells per tick^2
constexpr int kMaxFallVy = 64; // 8 cells per tick
constexpr int kRestless = 90;  // sideways moves before a lone liquid pixel dries up

using namespace cellflag;
using namespace matflag;

} // namespace

ChunkUpdater::ChunkUpdater(World& world, int cx, int cy)
    : world_(world),
      cells_(world.cells_.data()),
      defs_(materials().defs.data()),
      w_(world.w_),
      h_(world.h_),
      x0_(cx << World::kChunkShift),
      y0_(cy << World::kChunkShift),
      x1_(x0_ + World::kChunkSize - 1),
      y1_(y0_ + World::kChunkSize - 1),
      rng_(hash3(cx, cy, static_cast<int>(world.tick_), world.seed_)),
      clock_(static_cast<uint8_t>(world.tick_ & 1u)),
      tick_(world.tick_) {}

void ChunkUpdater::run() {
    const DirtyRect r = world_.chunk(x0_ >> World::kChunkShift, y0_ >> World::kChunkShift).current;
    const bool leftToRight = (tick_ & 1u) != 0;

    auto visit = [this](int x, int y, Cell& c) {
        if (c.mat == mat::Empty) return;
        if (c.flags & kMoved) {
            // Arrived here earlier this tick; it gets its turn next tick.
            c.flags &= static_cast<uint8_t>(~kMoved);
            wake(x, y);
            return;
        }
        if (defs_[c.mat].phase == Phase::Solid && !(c.flags & (kBurning | kElectric))) return;
        updateCell(x, y);
    };

    for (int y = r.maxY; y >= r.minY; --y) {
        Cell* row = cells_ + static_cast<size_t>(y) * w_;
        if (leftToRight) {
            for (int x = r.minX; x <= r.maxX; ++x) visit(x, y, row[x]);
        } else {
            for (int x = r.maxX; x >= r.minX; --x) visit(x, y, row[x]);
        }
    }

    if (!local_.empty()) {
        World::Chunk& self = world_.chunk(x0_ >> World::kChunkShift, y0_ >> World::kChunkShift);
        world_.expandNext(self, local_.minX, local_.minY, local_.maxX, local_.maxY);
    }
}

void ChunkUpdater::wake(int x, int y) {
    if (x - 1 >= x0_ && x + 1 <= x1_ && y - 1 >= y0_ && y + 1 <= y1_) {
        local_.include(x - 1, y - 1, x + 1, y + 1);
    } else {
        world_.wakeRect(x - 1, y - 1, x + 1, y + 1);
    }
}

void ChunkUpdater::swap(int x0, int y0, int x1, int y1) {
    // The cell at (x0, y0) is the one being updated and moves to (x1, y1).
    Cell& a = at(x0, y0);
    Cell& b = at(x1, y1);
    std::swap(a, b);
    const bool leftToRight = (tick_ & 1u) != 0;
    const bool ahead = y1 < y0 || (y1 == y0 && (leftToRight ? x1 > x0 : x1 < x0));
    if (ahead) b.flags |= kMoved;
    a.flags &= static_cast<uint8_t>(~kMoved);
    wake(x0, y0);
    wake(x1, y1);
}

void ChunkUpdater::replace(int x, int y, MatId m) {
    Cell& c = at(x, y);
    c = (m == mat::Empty) ? Cell{} : makeCell(m, rng_.next());
    wake(x, y);
}

void ChunkUpdater::igniteCell(int x, int y) {
    Cell& o = at(x, y);
    const MaterialDef& od = def(o.mat);
    if (od.flammability == 0 || (o.flags & kBurning)) return;
    if (!rng_.chance256(od.flammability)) return;
    o.flags |= kBurning;
    o.life = static_cast<uint8_t>(std::max(1, od.burnTime - static_cast<int>(rng_.next() % (od.burnTime / 4u + 1u))));
    wake(x, y);
}

bool ChunkUpdater::canSink(uint16_t density, MatId target) const {
    if (target == mat::Empty) return true;
    const MaterialDef& t = def(target);
    switch (t.phase) {
    case Phase::Gas:
    case Phase::Fire: return true;
    case Phase::Liquid: return t.density < density;
    default: return false;
    }
}

bool ChunkUpdater::canRise(uint16_t density, MatId target) const {
    if (target == mat::Empty) return true;
    const MaterialDef& t = def(target);
    switch (t.phase) {
    case Phase::Gas: return t.density > density;
    case Phase::Liquid: return true; // bubbles rise through liquids
    default: return false;
    }
}

void ChunkUpdater::updateCell(int x, int y) {
    Cell& self = at(x, y);
    if (self.flags & kElectric) {
        // Electric charge lingers for a few ticks, then dissipates.
        if (rng_.chance256(70)) self.flags &= static_cast<uint8_t>(~kElectric);
        wake(x, y);
    }
    if (self.flags & kBurning) {
        if (updateBurning(x, y)) return;
    }
    if (updateReactions(x, y)) return;
    switch (def(at(x, y).mat).phase) {
    case Phase::Powder: updatePowder(x, y); break;
    case Phase::Liquid: updateLiquid(x, y); break;
    case Phase::Gas: updateGas(x, y); break;
    case Phase::Fire: updateFire(x, y); break;
    default: break;
    }
}

bool ChunkUpdater::updateBurning(int x, int y) {
    Cell& c = at(x, y);
    const MaterialDef& d = def(c.mat);

    for (int i = 0; i < 4; ++i) {
        int nx = x + kDx4[i], ny = y + kDy4[i];
        if (!inBounds(nx, ny)) continue;
        if (def(at(nx, ny).mat).flags & kExtinguisher) {
            c.flags &= static_cast<uint8_t>(~kBurning);
            if (rng_.chance256(60)) replace(nx, ny, mat::Steam);
            wake(x, y);
            return false;
        }
    }

    if (c.life == 0) {
        if (d.flags & kExplosive) world_.pushEvent({SimEvent::Type::Explosion, x, y, 1.0f});
        MatId into = d.burnsInto;
        if (into == mat::Ash && rng_.chance256(150)) into = mat::Empty;
        replace(x, y, into);
        return true;
    }
    --c.life;

    // Throw flames into open air around the burning cell, mostly upwards.
    if (rng_.chance256(110)) {
        static constexpr int kFx[5] = {0, -1, 1, -1, 1};
        static constexpr int kFy[5] = {-1, -1, -1, 0, 0};
        int r = static_cast<int>(rng_.next() % 5u);
        int fx = x + kFx[r], fy = y + kFy[r];
        if (inBounds(fx, fy) && at(fx, fy).mat == mat::Empty) replace(fx, fy, mat::Fire);
    }
    // Spread to a random neighbour.
    {
        int r = static_cast<int>(rng_.next() & 7u);
        int nx = x + kDx8[r], ny = y + kDy8[r];
        if (inBounds(nx, ny)) igniteCell(nx, ny);
    }
    wake(x, y);
    return false;
}

bool ChunkUpdater::updateReactions(int x, int y) {
    const MatId self = at(x, y).mat;
    const MaterialDef& d = def(self);
    const MaterialTables& mt = materials();
    const int reactionCount = mt.reactionCount[self];
    constexpr uint32_t kActive = kCorrosive | kIgniter | kTransmuter | kHot;
    if (reactionCount == 0 && !(d.flags & kActive)) return false;

    bool pending = false;
    const int start = static_cast<int>(rng_.next() & 3u);
    for (int k = 0; k < 4; ++k) {
        int i = (start + k) & 3;
        int nx = x + kDx4[i], ny = y + kDy4[i];
        if (!inBounds(nx, ny)) continue;
        Cell& o = at(nx, ny);
        if (o.mat == mat::Empty) continue;
        const MaterialDef& od = def(o.mat);

        for (int r = 0; r < reactionCount; ++r) {
            const Reaction& re = mt.reactions[self][r];
            if (re.other != o.mat) continue;
            pending = true;
            if (!rng_.chance256(re.chance)) continue;
            if (re.otherInto != kKeep) replace(nx, ny, re.otherInto);
            if (re.selfInto != kKeep) {
                replace(x, y, re.selfInto);
                return true;
            }
            wake(x, y);
            return false;
        }

        if ((d.flags & kCorrosive) && !(od.flags & kAcidProof) && od.hardness < 255 &&
            (od.phase == Phase::Solid || od.phase == Phase::Powder)) {
            pending = true;
            int chance = (od.flags & kOrganic) ? 70 : std::max(4, 48 - od.hardness / 4);
            if (rng_.chance256(static_cast<uint32_t>(chance))) {
                replace(nx, ny, rng_.chance256(24) ? mat::ToxicGas : mat::Empty);
                if (rng_.chance256(46)) {
                    replace(x, y, rng_.chance256(60) ? mat::ToxicGas : mat::Empty);
                    return true;
                }
            }
        }

        if (d.flags & (kIgniter | kHot)) {
            if (od.flammability > 0 && !(o.flags & kBurning)) {
                pending = true;
                igniteCell(nx, ny);
            }
            if (od.flags & kMeltable) {
                pending = true;
                if (rng_.chance256(24)) replace(nx, ny, mat::Water);
            }
        }

        if ((d.flags & kTransmuter) && od.phase == Phase::Solid && o.mat != mat::GoldOre && od.hardness < 255) {
            pending = true;
            if (rng_.chance256(40)) {
                replace(nx, ny, mat::GoldOre);
                if (rng_.chance256(60)) {
                    replace(x, y, mat::Empty);
                    return true;
                }
            }
        }
    }
    if (pending) wake(x, y);
    return false;
}

bool ChunkUpdater::fall(int x, int y) {
    Cell& c = at(x, y);
    const MaterialDef& d = def(c.mat);
    MatId below = matAt(x, y + 1);
    if (!canSink(d.density, below)) return false;

    if (def(below).phase == Phase::Liquid) {
        // Sinking through a lighter liquid is slow and kills momentum.
        c.vy = 0;
        if (rng_.chance256(120)) swap(x, y, x, y + 1);
        else wake(x, y);
        return true;
    }

    c.vy = static_cast<int8_t>(std::min(c.vy + kGravity, kMaxFallVy));
    if (d.phase == Phase::Liquid) c.vx = 0; // falling resets liquid restlessness
    const int steps = std::max(1, c.vy >> 3);
    int ny = y;
    for (int i = 0; i < steps; ++i) {
        Phase p = def(matAt(x, ny + 1)).phase;
        if (p != Phase::Empty && p != Phase::Gas && p != Phase::Fire) break;
        ++ny;
    }
    swap(x, y, x, ny);
    return true;
}

void ChunkUpdater::updatePowder(int x, int y) {
    if (fall(x, y)) return;
    Cell& c = at(x, y);
    const uint16_t density = def(c.mat).density;

    // Landing converts part of the impact into sideways momentum, spreading piles.
    if (c.vy > 24) c.vx = static_cast<int8_t>(rng_.dir() * std::min(4, c.vy / 20));
    c.vy = 0;

    int dir = rng_.dir();
    for (int k = 0; k < 2; ++k, dir = -dir) {
        int nx = x + dir;
        if (canSink(density, matAt(nx, y + 1)) && canSink(density, matAt(nx, y))) {
            swap(x, y, nx, y + 1);
            return;
        }
    }

    if (c.vx != 0) {
        int sx = c.vx > 0 ? 1 : -1;
        c.vx = static_cast<int8_t>(c.vx - sx);
        if (canSink(density, matAt(x + sx, y))) {
            swap(x, y, x + sx, y);
            return;
        }
        c.vx = 0;
    }
}

void ChunkUpdater::updateLiquid(int x, int y) {
    if (fall(x, y)) return;
    Cell& c = at(x, y);
    const MaterialDef& d = def(c.mat);
    c.vy = 0;

    auto canFlow = [&](MatId t) {
        if (t == mat::Empty) return true;
        const MaterialDef& td = def(t);
        if (td.phase == Phase::Gas || td.phase == Phase::Fire) return true;
        return td.phase == Phase::Liquid && td.density < d.density && rng_.chance256(64);
    };

    int dir = (c.flags & kFlowRight) ? 1 : -1;
    for (int k = 0; k < 2; ++k) {
        int nx = x + (k == 0 ? dir : -dir);
        if (canSink(d.density, matAt(nx, y + 1)) && canFlow(matAt(nx, y))) {
            c.vx = 0;
            swap(x, y, nx, y + 1);
            return;
        }
    }

    // A lone pixel that has wandered for too long dries up (or cools, for lava) so that
    // stray droplets don't keep chunks awake forever.
    if (c.vx >= kRestless) {
        bool lonely = matAt(x - 1, y) != c.mat && matAt(x + 1, y) != c.mat && matAt(x, y - 1) != c.mat;
        if (lonely && rng_.chance256(6)) {
            if (c.mat == mat::Lava) replace(x, y, mat::Basalt);
            else if (!(d.flags & (kTransmuter | kHealing))) replace(x, y, mat::Empty);
            return;
        }
        if (lonely) {
            wake(x, y);
            return;
        }
    }

    for (int k = 0; k < 2; ++k) {
        const int sdir = k == 0 ? dir : -dir;
        int best = 0;
        for (int i = 1; i <= d.dispersion; ++i) {
            if (!canFlow(matAt(x + sdir * i, y))) break;
            best = i;
            if (canSink(d.density, matAt(x + sdir * i, y + 1))) break;
        }
        if (best > 0) {
            if (k == 1) c.flags ^= kFlowRight;
            if (c.vx < 127) ++c.vx;
            swap(x, y, x + sdir * best, y);
            return;
        }
    }
}

void ChunkUpdater::updateGas(int x, int y) {
    Cell& c = at(x, y);
    const MaterialDef& d = def(c.mat);
    if (c.life == 0) {
        if (d.decayChance > 0 && rng_.chance256(d.decayChance)) replace(x, y, d.decaysInto);
        else replace(x, y, mat::Empty);
        return;
    }
    if (rng_.chance256(170)) --c.life;
    wake(x, y);

    uint32_t r = rng_.next();
    int dx = static_cast<int>(r % 3u) - 1;
    if (((r >> 8) & 0xFFu) < 190u) {
        if (canRise(d.density, matAt(x + dx, y - 1))) {
            swap(x, y, x + dx, y - 1);
            return;
        }
        if (dx != 0 && canRise(d.density, matAt(x, y - 1))) {
            swap(x, y, x, y - 1);
            return;
        }
    }
    int sx = dx != 0 ? dx : rng_.dir();
    MatId side = matAt(x + sx, y);
    if (side == mat::Empty || (def(side).phase == Phase::Gas && side != c.mat)) swap(x, y, x + sx, y);
}

void ChunkUpdater::updateFire(int x, int y) {
    Cell& c = at(x, y);
    const MaterialDef& d = def(c.mat);
    if (c.life == 0) {
        if (d.decayChance > 0 && rng_.chance256(d.decayChance)) replace(x, y, d.decaysInto);
        else replace(x, y, mat::Empty);
        return;
    }
    --c.life;
    wake(x, y);

    for (int i = 0; i < 4; ++i) {
        int nx = x + kDx4[i], ny = y + kDy4[i];
        if (!inBounds(nx, ny)) continue;
        if (def(at(nx, ny).mat).flags & kExtinguisher) {
            if (rng_.chance256(50)) replace(nx, ny, mat::Steam);
            replace(x, y, mat::Empty);
            return;
        }
    }

    uint32_t r = rng_.next();
    int dx = static_cast<int>(r % 3u) - 1;
    if (((r >> 8) & 0xFFu) < 150u) {
        MatId t = matAt(x + dx, y - 1);
        if (t == mat::Empty || def(t).phase == Phase::Gas) swap(x, y, x + dx, y - 1);
    }
}

} // namespace pm
