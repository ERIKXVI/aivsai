#include "core/Noise.hpp"

#include <cmath>

#include "core/Math.hpp"
#include "core/Rng.hpp"

namespace pm::noise {

namespace {

constexpr float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float gradDot(int ix, int iy, float dx, float dy, uint32_t seed) {
    // 8 evenly spread gradient directions picked by hash.
    static constexpr float kGx[8] = {1.0f, -1.0f, 0.0f, 0.0f, 0.7071f, -0.7071f, 0.7071f, -0.7071f};
    static constexpr float kGy[8] = {0.0f, 0.0f, 1.0f, -1.0f, 0.7071f, 0.7071f, -0.7071f, -0.7071f};
    uint32_t h = hash2(ix, iy, seed) & 7u;
    return kGx[h] * dx + kGy[h] * dy;
}

} // namespace

float perlin(float x, float y, uint32_t seed) {
    int x0 = floorToInt(x), y0 = floorToInt(y);
    float fx = x - static_cast<float>(x0), fy = y - static_cast<float>(y0);
    float u = fade(fx), v = fade(fy);
    float n00 = gradDot(x0, y0, fx, fy, seed);
    float n10 = gradDot(x0 + 1, y0, fx - 1.0f, fy, seed);
    float n01 = gradDot(x0, y0 + 1, fx, fy - 1.0f, seed);
    float n11 = gradDot(x0 + 1, y0 + 1, fx - 1.0f, fy - 1.0f, seed);
    return lerp(lerp(n00, n10, u), lerp(n01, n11, u), v) * 1.414f;
}

float fbm(float x, float y, uint32_t seed, int octaves, float lacunarity, float gain) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += perlin(x, y, seed + static_cast<uint32_t>(i) * 1013u) * amp;
        norm += amp;
        x *= lacunarity;
        y *= lacunarity;
        amp *= gain;
    }
    return sum / norm;
}

float ridged(float x, float y, uint32_t seed, int octaves) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        float n = 1.0f - std::fabs(perlin(x, y, seed + static_cast<uint32_t>(i) * 7919u));
        sum += n * n * amp;
        norm += amp;
        x *= 2.0f;
        y *= 2.0f;
        amp *= 0.5f;
    }
    return sum / norm;
}

float warped(float x, float y, uint32_t seed, int octaves, float warpStrength) {
    float wx = fbm(x + 5.2f, y + 1.3f, seed ^ 0xA5A5u, 3);
    float wy = fbm(x - 3.7f, y + 9.2f, seed ^ 0x5A5Au, 3);
    return fbm(x + wx * warpStrength, y + wy * warpStrength, seed, octaves);
}

float worley(float x, float y, uint32_t seed) {
    int cx = floorToInt(x), cy = floorToInt(y);
    float best = 8.0f;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            uint32_t h = hash2(cx + ox, cy + oy, seed);
            float px = static_cast<float>(cx + ox) + hashToUnit(h);
            float py = static_cast<float>(cy + oy) + hashToUnit(hash32(h));
            float dx = px - x, dy = py - y;
            best = std::fmin(best, dx * dx + dy * dy);
        }
    }
    return std::sqrt(best);
}

} // namespace pm::noise
