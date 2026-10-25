// Level generation, step one: planning the route and carving the open space.

#include "gen/GeneratorImpl.hpp"

#include <algorithm>
#include <cmath>

#include "core/Noise.hpp"
#include "core/ThreadPool.hpp"

namespace pm::gen_detail {

void Generator::carveCircle(float cx, float cy, float r, float roughness) {
    int x0 = std::max(1, static_cast<int>(cx - r - 3)), x1 = std::min(w_ - 2, static_cast<int>(cx + r + 3));
    int y0 = std::max(1, static_cast<int>(cy - r - 3)), y1 = std::min(h_ - 2, static_cast<int>(cy + r + 3));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float dx = static_cast<float>(x) - cx, dy = static_cast<float>(y) - cy;
            float rr = r;
            if (roughness > 0.0f) rr += noise::perlin(x * 0.15f, y * 0.15f, seed_ ^ 0x77u) * roughness;
            if (dx * dx + dy * dy <= rr * rr) open_[idx(x, y)] = 1;
        }
    }
}

void Generator::carveSegment(Vec2 a, Vec2 b, float baseRadius, float radiusVar, float wobble, uint32_t salt) {
    Vec2 d = b - a;
    float len = d.length();
    if (len < 1.0f) return;
    Vec2 dir = d / len;
    Vec2 perp{-dir.y, dir.x};
    for (float t = 0.0f; t <= len; t += 2.0f) {
        float s = t * 0.012f;
        float off = noise::perlin(s, static_cast<float>(salt) * 0.37f, seed_ ^ salt) * wobble;
        // Taper the wobble at segment ends so consecutive segments stay connected.
        float taper = std::min(1.0f, std::min(t, len - t) / 40.0f);
        Vec2 p = a + dir * t + perp * (off * taper);
        float r = baseRadius + noise::perlin(s * 2.0f, 9.1f, seed_ ^ (salt * 7u)) * radiusVar;
        carveCircle(p.x, p.y, std::max(6.0f, r), 1.5f);
    }
}

void Generator::planSkyline() {
    surfaceY_ = kSkyRows - 40;
    skyline_.assign(static_cast<size_t>(w_), surfaceY_);
    for (int x = 0; x < w_; ++x) {
        float hills = noise::fbm(static_cast<float>(x) * 0.006f, 3.7f, seed_ ^ 0x51u, 3) * 34.0f;
        float bumps = noise::perlin(static_cast<float>(x) * 0.05f, 1.1f, seed_ ^ 0x52u) * 4.0f;
        skyline_[static_cast<size_t>(x)] = surfaceY_ + static_cast<int>(hills + bumps);
    }
}

void Generator::flattenSkyline(int cx, int halfWidth, int height) {
    for (int x = std::max(0, cx - halfWidth - 20); x <= std::min(w_ - 1, cx + halfWidth + 20); ++x) {
        int d = std::abs(x - cx);
        float t = d <= halfWidth ? 1.0f : 1.0f - static_cast<float>(d - halfWidth) / 20.0f;
        int& h = skyline_[static_cast<size_t>(x)];
        h = static_cast<int>(lerp(static_cast<float>(h), static_cast<float>(height), t));
    }
}

void Generator::planPath() {
    start_ = {static_cast<float>(w_) * rng_.range(0.3f, 0.7f), 56.0f};
    if (surface_) {
        planSkyline();
        // The alchemist arrives on the surface and walks to a cave mouth nearby.
        const int entranceX = static_cast<int>(static_cast<float>(w_) * rng_.range(0.4f, 0.6f));
        const int dir = rng_.chance(0.5f) ? 1 : -1;
        const int startX = entranceX - dir * 115;
        const int ground = skyline_[static_cast<size_t>(startX)];
        flattenSkyline(startX, 22, ground);
        start_ = {static_cast<float>(startX), static_cast<float>(ground - 1)};
        path_.push_back({static_cast<float>(entranceX), static_cast<float>(skyline_[static_cast<size_t>(entranceX)])});
    }
    exit_ = {static_cast<float>(w_) * rng_.range(0.2f, 0.8f), static_cast<float>(h_ - 64)};
    const int legs = 6 + std::min(level_, 2);
    if (path_.empty()) path_.push_back(start_);
    bool left = rng_.chance(0.5f);
    for (int i = 1; i < legs; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(legs);
        float y = lerp(path_.front().y, exit_.y, t) + rng_.range(-25.0f, 25.0f);
        float x = left ? rng_.range(0.12f, 0.42f) : rng_.range(0.58f, 0.88f);
        left = !left;
        path_.push_back({x * static_cast<float>(w_), y});
    }
    path_.push_back(exit_);
}

void Generator::carveNoiseCaves(ThreadPool& pool) {
    // Coarse field (1/4 resolution) for the big shapes, bilinear-upsampled, plus a fine octave.
    const int cs = 4;
    const int gw = w_ / cs + 2, gh = h_ / cs + 2;
    std::vector<float> coarse(static_cast<size_t>(gw) * gh);
    const float threshold = lerp(0.32f, 0.08f, b_.openness);
    const uint32_t s = seed_;
    pool.parallelFor(gh, [&](int gy) {
        for (int gx = 0; gx < gw; ++gx) {
            float x = static_cast<float>(gx * cs), y = static_cast<float>(gy * cs);
            // Horizontally stretched caves read better in a side view.
            float n = noise::warped(x * 0.0085f, y * 0.012f, s, 4, 1.4f);
            float ridges = noise::ridged(x * 0.006f, y * 0.006f, s ^ 0xBEEFu, 3);
            coarse[static_cast<size_t>(gy) * gw + gx] = n + (ridges - 0.55f) * 0.35f;
        }
    });
    pool.parallelFor(h_, [&](int y) {
        for (int x = 0; x < w_; ++x) {
            float fx = static_cast<float>(x) / cs, fy = static_cast<float>(y) / cs;
            int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
            float tx = fx - ix, ty = fy - iy;
            auto at = [&](int gx, int gy) { return coarse[static_cast<size_t>(gy) * gw + gx]; };
            float v = lerp(lerp(at(ix, iy), at(ix + 1, iy), tx), lerp(at(ix, iy + 1), at(ix + 1, iy + 1), tx), ty);
            v += noise::perlin(x * 0.06f, y * 0.06f, s ^ 0x1234u) * 0.06f;
            if (v > threshold || aboveGround(x, y)) open_[idx(x, y)] = 1;
        }
    });
}

void Generator::carvePath() {
    for (size_t i = 0; i + 1 < path_.size(); ++i) {
        carveSegment(path_[i], path_[i + 1], 14.0f, 3.0f, 34.0f, static_cast<uint32_t>(i * 31 + 5));
    }
}

void Generator::carveChambers() {
    int count = rng_.rangeInt(5, 8);
    for (int i = 0; i < count; ++i) {
        const Vec2& anchor = path_[static_cast<size_t>(rng_.rangeInt(1, static_cast<int>(path_.size()) - 2))];
        float cx = clamp(anchor.x + rng_.range(-140.0f, 140.0f), 70.0f, static_cast<float>(w_ - 70));
        float cy = clamp(anchor.y + rng_.range(-60.0f, 60.0f), 90.0f, static_cast<float>(h_ - 90));
        float rx = rng_.range(30.0f, 70.0f), ry = rng_.range(18.0f, 36.0f);
        for (int y = static_cast<int>(cy - ry - 8); y <= static_cast<int>(cy + ry + 8); ++y) {
            for (int x = static_cast<int>(cx - rx - 8); x <= static_cast<int>(cx + rx + 8); ++x) {
                if (!inBounds(x, y)) continue;
                float dx = (x - cx) / rx, dy = (y - cy) / ry;
                float edge = 1.0f + noise::perlin(x * 0.07f, y * 0.07f, seed_ ^ 0x5151u) * 0.25f;
                if (dx * dx + dy * dy < edge) open_[idx(x, y)] = 1;
            }
        }
        chambers_.push_back({cx, cy});
        // Connect to the path so every chamber is reachable.
        carveSegment({cx, cy}, anchor, 8.0f, 2.0f, 12.0f, static_cast<uint32_t>(900 + i));
    }
}

void Generator::carveBranches() {
    int count = rng_.rangeInt(4, 7);
    for (int i = 0; i < count; ++i) {
        Vec2 p = path_[static_cast<size_t>(rng_.rangeInt(0, static_cast<int>(path_.size()) - 1))];
        float angle = rng_.chance(0.5f) ? rng_.range(-0.5f, 0.5f) : rng_.range(kPi - 0.5f, kPi + 0.5f);
        int steps = rng_.rangeInt(40, 110);
        float r = rng_.range(5.0f, 8.0f);
        for (int s = 0; s < steps; ++s) {
            angle += rng_.range(-0.25f, 0.25f);
            p += fromAngle(angle, 2.5f);
            p.x = clamp(p.x, 20.0f, static_cast<float>(w_ - 20));
            p.y = clamp(p.y, 30.0f, static_cast<float>(h_ - 30));
            carveCircle(p.x, p.y, r, 1.0f);
        }
        pockets_.push_back(p);
    }
}

void Generator::smooth() {
    std::vector<uint8_t> next = open_;
    for (int y = 1; y < h_ - 1; ++y) {
        for (int x = 1; x < w_ - 1; ++x) {
            int n = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) n += open_[idx(x + dx, y + dy)];
            next[idx(x, y)] = n >= 5 ? 1 : 0;
        }
    }
    open_.swap(next);
}

void Generator::sealBorders() {
    for (int y = 0; y < h_; ++y) {
        for (int x = 0; x < w_; ++x) {
            if (aboveGround(x, y)) {
                open_[idx(x, y)] = 1;
                continue;
            }
            if (x < kBorder || y < kBorder || x >= w_ - kBorder || y >= h_ - kBorder) open_[idx(x, y)] = 0;
        }
    }
}

void Generator::carvePockets() {
    if (!surface_) carveCircle(start_.x, start_.y, 20.0f, 2.0f);
    carveCircle(exit_.x, exit_.y - 10.0f, 30.0f, 2.0f);
    if (biome_ == BiomeId::Athanor) carveArena();
    // Reach the start pocket from above the first path segment's top.
    sealBorders();
}

void Generator::carveArena() {
    const float cx = exit_.x, cy = exit_.y - 70.0f;
    const float rx = std::min(200.0f, static_cast<float>(w_) * 0.5f - 30.0f), ry = 95.0f;
    for (int y = static_cast<int>(cy - ry - 6); y <= static_cast<int>(cy + ry + 6); ++y)
        for (int x = static_cast<int>(cx - rx - 6); x <= static_cast<int>(cx + rx + 6); ++x) {
            if (!inBounds(x, y)) continue;
            float dx = (static_cast<float>(x) - cx) / rx, dy = (static_cast<float>(y) - cy) / ry;
            float edge = 1.0f + noise::perlin(x * 0.05f, y * 0.05f, seed_ ^ 0xA7u) * 0.08f;
            if (dx * dx + dy * dy < edge) open_[idx(x, y)] = 1;
        }
    arena_ = {cx, cy, rx, ry};
}

} // namespace pm::gen_detail
