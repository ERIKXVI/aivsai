#include "render/Sky.hpp"

#include <algorithm>
#include <cmath>

#include "core/Math.hpp"
#include "core/Noise.hpp"
#include "core/Rng.hpp"
#include "raylib.h"

namespace pm {

namespace {

constexpr float kStarParallax = 0.04f;
constexpr float kFarParallax = 0.12f;
constexpr float kNearParallax = 0.28f;
constexpr int kStarCount = 260;
constexpr float kStarField = 2400.0f;

Color skyColor(float worldY, int surfaceY) {
    float t = saturate(worldY / static_cast<float>(std::max(1, surfaceY)));
    // Deep indigo overhead, dusky violet towards the horizon, a warm haze right above it.
    Color top{6, 8, 26, 255}, mid{34, 26, 66, 255}, low{86, 52, 84, 255};
    auto mix = [](Color a, Color b, float k) {
        return Color{static_cast<unsigned char>(lerp(a.r, b.r, k)), static_cast<unsigned char>(lerp(a.g, b.g, k)),
                     static_cast<unsigned char>(lerp(a.b, b.b, k)), 255};
    };
    return t < 0.7f ? mix(top, mid, t / 0.7f) : mix(mid, low, (t - 0.7f) / 0.3f);
}

Color scaled(Color c, float k) {
    return Color{static_cast<unsigned char>(c.r * k), static_cast<unsigned char>(c.g * k), static_cast<unsigned char>(c.b * k), c.a};
}

float mountainHeight(float x, uint32_t seed, float amplitude) {
    float ridge = noise::ridged(x * 0.004f, 0.5f, seed, 4);
    return amplitude * (0.35f + ridge);
}

} // namespace

void Sky::draw(const std::vector<int>& skyline, int surfaceY, int originX, int originY, int texW, int texH, float time,
               bool emissionPass) const {
    if (skyline.empty()) return;
    const int worldW = static_cast<int>(skyline.size());

    // Sky gradient, column by column down to the terrain. Only the moon and stars emit
    // light; an emissive sky would flood the glow pass.
    for (int tx = 0; tx < texW && !emissionPass; ++tx) {
        const int wx = clamp(originX + tx, 0, worldW - 1);
        const int bottom = std::min(texH, skyline[static_cast<size_t>(wx)] + 6 - originY);
        if (bottom <= 0) continue;
        DrawRectangleGradientV(tx, 0, 1, bottom, skyColor(static_cast<float>(originY), surfaceY),
                               skyColor(static_cast<float>(originY + bottom), surfaceY));
    }

    // Stars on a slowly drifting field.
    const float starShift = static_cast<float>(originX) * kStarParallax;
    for (int i = 0; i < kStarCount; ++i) {
        uint32_t h = hash32(static_cast<uint32_t>(i) * 2654435761u + 17u);
        float sx = hashToUnit(h) * kStarField;
        float sy = hashToUnit(hash32(h)) * static_cast<float>(surfaceY) * 0.85f;
        float lx = std::fmod(sx - starShift, kStarField);
        if (lx < 0.0f) lx += kStarField;
        int x = static_cast<int>(lx), y = static_cast<int>(sy) - originY;
        if (x >= texW || y < 0 || y >= texH) continue;
        const int wx = clamp(originX + x, 0, worldW - 1);
        if (static_cast<int>(sy) >= skyline[static_cast<size_t>(wx)]) continue;
        float twinkle = 0.6f + 0.4f * std::sin(time * (1.5f + hashToUnit(h >> 3) * 3.0f) + static_cast<float>(i));
        float bright = (0.4f + 0.6f * hashToUnit(h >> 7)) * twinkle;
        unsigned char v = static_cast<unsigned char>(255.0f * bright * (emissionPass ? 0.8f : 1.0f));
        DrawPixel(x, y, Color{v, v, static_cast<unsigned char>(std::min(255, v + 20)), 255});
    }

    // The moon, with a few darker maria.
    {
        // Anchored to the view with a slight drift so it is always in the sky.
        const int x = static_cast<int>(static_cast<float>(texW) * 0.74f - starShift);
        const int y = static_cast<int>(static_cast<float>(surfaceY) * 0.3f) - originY;
        if (x > -20 && x < texW + 20 && y > -20 && y < texH + 20) {
            Color face = emissionPass ? Color{200, 196, 170, 255} : Color{236, 232, 210, 255};
            DrawCircle(x, y, 10.0f, face);
            if (!emissionPass) {
                DrawCircle(x - 3, y - 2, 2.5f, Color{196, 190, 170, 255});
                DrawCircle(x + 4, y + 3, 3.0f, Color{200, 194, 174, 255});
                DrawCircle(x + 1, y - 5, 1.5f, Color{204, 198, 178, 255});
            }
        }
    }

    // Two mountain ranges; the nearer one is darker and moves faster.
    struct Range {
        float parallax;
        float amplitude;
        uint32_t seed;
        Color color;
    };
    const Range ranges[] = {{kFarParallax, 70.0f, 0x2311u, Color{30, 26, 52, 255}},
                            {kNearParallax, 46.0f, 0x7177u, Color{18, 16, 30, 255}}};
    for (const Range& r : ranges) {
        if (emissionPass) break;
        const float shift = static_cast<float>(originX) * r.parallax;
        for (int tx = 0; tx < texW; ++tx) {
            const int wx = clamp(originX + tx, 0, worldW - 1);
            const float top = static_cast<float>(surfaceY) + 10.0f - mountainHeight(shift + static_cast<float>(tx), r.seed, r.amplitude);
            const int y0 = static_cast<int>(top) - originY;
            const int y1 = std::min(texH, skyline[static_cast<size_t>(wx)] + 6 - originY);
            if (y1 <= std::max(0, y0)) continue;
            DrawRectangle(tx, std::max(0, y0), 1, y1 - std::max(0, y0), emissionPass ? scaled(r.color, 0.3f) : r.color);
        }
    }
}

} // namespace pm
