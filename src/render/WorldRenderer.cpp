#include "render/WorldRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/Rng.hpp"
#include "sim/World.hpp"

namespace pm {

namespace {

/// 256-entry sine table in [-1, 1] used for cheap animated shimmer.
const std::array<float, 256>& sineTable() {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) t[i] = std::sin(static_cast<float>(i) / 256.0f * kTau);
        return t;
    }();
    return table;
}

constexpr Rgba kFireDark{170, 40, 10, 255};
constexpr Rgba kFireBright{255, 220, 90, 255};

inline Rgba mix(Rgba a, Rgba b, int t256) {
    auto m = [t256](uint8_t x, uint8_t y) {
        return static_cast<uint8_t>(x + (((static_cast<int>(y) - x) * t256) >> 8));
    };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}

inline Rgba scaleRgb(Rgba c, int s256) {
    return {static_cast<uint8_t>((c.r * s256) >> 8), static_cast<uint8_t>((c.g * s256) >> 8),
            static_cast<uint8_t>((c.b * s256) >> 8), c.a};
}

} // namespace

Rgba cellColor(const Cell& c, int x, int y, uint32_t tick) {
    const MaterialTables& mt = materials();
    const MaterialDef& d = mt.defs[c.mat];
    const auto& pal = mt.palette[c.mat];
    Rgba col;
    switch (d.phase) {
    case Phase::Empty: return Rgba{0, 0, 0, 0};
    case Phase::Liquid: {
        // Slow travelling waves keep still liquids alive without per-pixel noise.
        const auto& sn = sineTable();
        float wave = sn[(x * 5 + y * 3 + tick * 2) & 255] + sn[(x * 2 - y * 7 + tick * 3) & 255];
        int idx = clamp(static_cast<int>(c.var) / 2 + 64 + static_cast<int>(wave * 30.0f), 0, 255);
        col = pal[idx];
        break;
    }
    case Phase::Fire: col = pal[std::min(255, c.life * 14 + (c.var & 31))]; break;
    case Phase::Gas: {
        col = pal[c.var];
        col.a = static_cast<uint8_t>(std::min<int>(col.a, c.life * 3 + 20));
        break;
    }
    default: col = pal[c.var]; break;
    }
    if (c.flags & cellflag::kBurning) {
        int f = static_cast<int>(hash3(x, y, static_cast<int>(tick >> 1)) & 255u);
        col = mix(col, mix(kFireDark, kFireBright, f), 190);
        col.a = 255;
    }
    if (c.flags & cellflag::kElectric) col = Rgba{220, 235, 255, 255};
    return col;
}

Rgba cellEmission(const Cell& c, int x, int y, uint32_t tick) {
    const MaterialDef& d = materials().defs[c.mat];
    Rgba e{0, 0, 0, d.opacity};
    if (d.glow.a > 0) {
        int intensity = d.glow.a;
        if (d.phase == Phase::Fire) intensity = std::min(255, 60 + c.life * 12);
        else if (c.mat == mat::Lava) intensity = 170 + static_cast<int>(sineTable()[(x * 3 + y * 2 + tick) & 255] * 60.0f);
        Rgba g = scaleRgb(d.glow, intensity);
        e.r = g.r;
        e.g = g.g;
        e.b = g.b;
    }
    if (c.flags & cellflag::kBurning) {
        int f = 150 + static_cast<int>(hash3(x, y, static_cast<int>(tick >> 2)) & 63u);
        e.r = static_cast<uint8_t>(std::min(255, e.r + f));
        e.g = static_cast<uint8_t>(std::min(255, e.g + f / 3));
        e.b = static_cast<uint8_t>(std::min(255, e.b + f / 12));
    }
    if (c.flags & cellflag::kElectric) e = Rgba{160, 200, 255, e.a};
    return e;
}

WorldRenderer::~WorldRenderer() { release(); }

void WorldRenderer::release() {
    if (colorTex_.id) UnloadTexture(colorTex_);
    if (emitTex_.id) UnloadTexture(emitTex_);
    colorTex_ = {};
    emitTex_ = {};
}

void WorldRenderer::resize(int width, int height) {
    if (width == w_ && height == h_ && colorTex_.id) return;
    release();
    w_ = width;
    h_ = height;
    color_.assign(static_cast<size_t>(w_) * h_, Rgba{});
    emission_.assign(static_cast<size_t>(w_) * h_, Rgba{});
    Image img{color_.data(), w_, h_, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    colorTex_ = LoadTextureFromImage(img);
    emitTex_ = LoadTextureFromImage(img);
    SetTextureFilter(colorTex_, TEXTURE_FILTER_POINT);
    SetTextureFilter(emitTex_, TEXTURE_FILTER_POINT);
    SetTextureWrap(colorTex_, TEXTURE_WRAP_CLAMP);
    SetTextureWrap(emitTex_, TEXTURE_WRAP_CLAMP);
}

void WorldRenderer::build(const World& world, int originX, int originY, uint32_t tick) {
    ox_ = originX;
    oy_ = originY;
    blocksW_ = (w_ >> kBlockShift) + 1;
    blocksH_ = (h_ >> kBlockShift) + 1;
    blocks_.assign(static_cast<size_t>(blocksW_) * blocksH_, EmissiveBlock{});
    const Rgba outside{0, 0, 0, 255};
    for (int ty = 0; ty < h_; ++ty) {
        const int wy = oy_ + ty;
        Rgba* crow = color_.data() + static_cast<size_t>(ty) * w_;
        Rgba* erow = emission_.data() + static_cast<size_t>(ty) * w_;
        if (wy < 0 || wy >= world.height()) {
            std::fill(crow, crow + w_, Rgba{12, 10, 16, 255});
            std::fill(erow, erow + w_, outside);
            continue;
        }
        for (int tx = 0; tx < w_; ++tx) {
            const int wx = ox_ + tx;
            if (wx < 0 || wx >= world.width()) {
                crow[tx] = Rgba{12, 10, 16, 255};
                erow[tx] = outside;
                continue;
            }
            const Cell& c = world.at(wx, wy);
            if (c.mat == mat::Empty) {
                crow[tx] = Rgba{0, 0, 0, 0};
                erow[tx] = Rgba{0, 0, 0, 0};
            } else {
                crow[tx] = cellColor(c, wx, wy, tick);
                const Rgba e = cellEmission(c, wx, wy, tick);
                erow[tx] = e;
                const int intensity = e.r + e.g + e.b;
                if (intensity > 60) {
                    EmissiveBlock& b = blocks_[static_cast<size_t>(ty >> kBlockShift) * blocksW_ + (tx >> kBlockShift)];
                    const float k = static_cast<float>(intensity);
                    b.x += static_cast<float>(wx) * k;
                    b.y += static_cast<float>(wy) * k;
                    b.r += e.r;
                    b.g += e.g;
                    b.b += e.b;
                    b.weight += k;
                }
            }
        }
    }
}

void WorldRenderer::stamp(int worldX, int worldY, Rgba color, Rgba emission) {
    int tx = worldX - ox_, ty = worldY - oy_;
    if (tx < 0 || ty < 0 || tx >= w_ || ty >= h_) return;
    size_t i = static_cast<size_t>(ty) * w_ + tx;
    color_[i] = color;
    Rgba& e = emission_[i];
    e.r = std::max(e.r, emission.r);
    e.g = std::max(e.g, emission.g);
    e.b = std::max(e.b, emission.b);
}

void WorldRenderer::stampEmission(int worldX, int worldY, Rgba emission) {
    int tx = worldX - ox_, ty = worldY - oy_;
    if (tx < 0 || ty < 0 || tx >= w_ || ty >= h_) return;
    Rgba& e = emission_[static_cast<size_t>(ty) * w_ + tx];
    e.r = static_cast<uint8_t>(std::min(255, e.r + emission.r));
    e.g = static_cast<uint8_t>(std::min(255, e.g + emission.g));
    e.b = static_cast<uint8_t>(std::min(255, e.b + emission.b));
}

void WorldRenderer::upload() {
    if (!colorTex_.id) return;
    UpdateTexture(colorTex_, color_.data());
    UpdateTexture(emitTex_, emission_.data());
    // Finish the emissive block centroids.
    for (EmissiveBlock& b : blocks_) {
        if (b.weight <= 0.0f) continue;
        b.x /= b.weight;
        b.y /= b.weight;
    }
}

} // namespace pm
