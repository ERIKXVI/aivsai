#include "gen/Navigation.hpp"

#include <queue>

#include "sim/World.hpp"

namespace pm {

void NavGrid::build(const World& world) {
    bw_ = world.width() / kBlock;
    bh_ = world.height() / kBlock;
    free_.assign(static_cast<size_t>(bw_) * bh_, 0);
    for (int by = 0; by < bh_; ++by) {
        for (int bx = 0; bx < bw_; ++bx) {
            int solid = 0;
            for (int y = 0; y < kBlock; ++y)
                for (int x = 0; x < kBlock; ++x)
                    if (isSolidForCollision(world.at(bx * kBlock + x, by * kBlock + y).mat)) ++solid;
            free_[static_cast<size_t>(by) * bw_ + bx] = solid <= 2 ? 1 : 0;
        }
    }
    dist_.assign(free_.size(), kUnreachable);
}

bool NavGrid::blockFree(int bx, int by) const {
    if (bx < 0 || by < 0 || bx >= bw_ || by >= bh_) return false;
    return free_[static_cast<size_t>(by) * bw_ + bx] != 0;
}

bool NavGrid::fits(int bx, int by) const {
    for (int y = by - 2; y <= by; ++y)
        for (int x = bx; x <= bx + 1; ++x)
            if (!blockFree(x, y)) return false;
    return true;
}

bool NavGrid::findFit(Vec2 p, int& bx, int& by, int searchRadius) const {
    const int cx = static_cast<int>(p.x) / kBlock, cy = static_cast<int>(p.y) / kBlock;
    for (int r = 0; r < searchRadius; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (fits(cx + dx, cy + dy)) {
                    bx = cx + dx;
                    by = cy + dy;
                    return true;
                }
    return false;
}

bool NavGrid::computeDistances(Vec2 goal) {
    dist_.assign(free_.size(), kUnreachable);
    int gx = 0, gy = 0;
    if (!findFit(goal, gx, gy)) return false;
    std::queue<Vec2i> open;
    dist_[static_cast<size_t>(gy) * bw_ + gx] = 0;
    open.push({gx, gy});
    static constexpr int kDx[4] = {1, -1, 0, 0}, kDy[4] = {0, 0, 1, -1};
    while (!open.empty()) {
        Vec2i c = open.front();
        open.pop();
        const int d = dist_[static_cast<size_t>(c.y) * bw_ + c.x];
        for (int k = 0; k < 4; ++k) {
            const int nx = c.x + kDx[k], ny = c.y + kDy[k];
            if (nx < 0 || ny < 0 || nx >= bw_ || ny >= bh_) continue;
            int& nd = dist_[static_cast<size_t>(ny) * bw_ + nx];
            if (nd != kUnreachable || !fits(nx, ny)) continue;
            nd = d + 1;
            open.push({nx, ny});
        }
    }
    return true;
}

int NavGrid::distanceAt(Vec2 p, int searchRadius) const {
    int bx = 0, by = 0;
    if (!findFit(p, bx, by, searchRadius)) return kUnreachable;
    return dist_[static_cast<size_t>(by) * bw_ + bx];
}

bool NavGrid::nextStep(Vec2 p, Vec2& out) const {
    int bx = 0, by = 0;
    if (!findFit(p, bx, by)) return false;
    int best = dist_[static_cast<size_t>(by) * bw_ + bx];
    if (best == kUnreachable) return false;
    int tx = bx, ty = by;
    // Look a few blocks ahead along the gradient for a smoother target.
    for (int step = 0; step < 3; ++step) {
        int nbx = tx, nby = ty;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = tx + dx, ny = ty + dy;
                if (nx < 0 || ny < 0 || nx >= bw_ || ny >= bh_) continue;
                const int d = dist_[static_cast<size_t>(ny) * bw_ + nx];
                if (d != kUnreachable && d < best) {
                    best = d;
                    nbx = nx;
                    nby = ny;
                }
            }
        tx = nbx;
        ty = nby;
    }
    out = {static_cast<float>(tx * kBlock + kBlock), static_cast<float>(ty * kBlock) - 2.0f};
    return true;
}

} // namespace pm
