#include "render/Background.hpp"

#include <cmath>
#include <vector>

#include "core/Noise.hpp"

namespace pm {

namespace {

/// Seamlessly tiling value via a bilinear cross-fade of four offset samples.
template <typename F>
float tiled(F&& f, float x, float y, float size) {
    float a = f(x, y), b = f(x - size, y), c = f(x, y - size), d = f(x - size, y - size);
    float tx = x / size, ty = y / size;
    return lerp(lerp(a, b, tx), lerp(c, d, tx), ty);
}

Color mixColor(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int shift) {
        float va = static_cast<float>((a >> shift) & 0xFF), vb = static_cast<float>((b >> shift) & 0xFF);
        return static_cast<unsigned char>(clamp(lerp(va, vb, t), 0.0f, 255.0f));
    };
    return Color{ch(16), ch(8), ch(0), 255};
}

} // namespace

Background::~Background() {
    if (tex_.id && IsWindowReady()) UnloadTexture(tex_);
}

void Background::setBiome(BiomeId biome, uint32_t seed) {
    if (biome == biome_ && tex_.id) return;
    biome_ = biome;
    if (tex_.id) UnloadTexture(tex_);
    const BiomeDef& b = biomeDef(biome);
    const float size = static_cast<float>(kSize);
    std::vector<Color> px(static_cast<size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            float fx = static_cast<float>(x), fy = static_cast<float>(y);
            float body = tiled([&](float u, float v) { return noise::fbm(u * 0.025f, v * 0.035f, seed, 4); }, fx, fy, size);
            float cracks = tiled([&](float u, float v) { return noise::ridged(u * 0.02f, v * 0.02f, seed ^ 0x77u, 3); }, fx, fy, size);
            float grain = noise::perlin(fx * 0.4f, fy * 0.4f, seed ^ 0x13u) * 0.05f;
            // Stones: Worley cells give the wall a blocky, masonry-like rock structure.
            float stones = tiled([&](float u, float v) { return noise::worley(u * 0.045f, v * 0.06f, seed ^ 0x5Au); }, fx, fy, size);
            float t = clamp(0.45f + body * 0.8f + grain - stones * 0.35f, 0.0f, 1.0f);
            Color c = mixColor(b.backgroundB, b.backgroundA, t);
            // Dark fissures between rock plates.
            float fissure = smoothstep(0.80f, 0.95f, cracks);
            float k = 1.0f - fissure * 0.75f;
            // Horizontal strata catch a little extra light.
            float strata = 0.92f + 0.08f * std::sin(fy * 0.25f + body * 3.0f);
            k *= strata;
            px[static_cast<size_t>(y) * kSize + x] = Color{static_cast<unsigned char>(c.r * k), static_cast<unsigned char>(c.g * k),
                                                         static_cast<unsigned char>(c.b * k), 255};
        }
    }
    Image img{px.data(), kSize, kSize, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    tex_ = LoadTextureFromImage(img);
    SetTextureFilter(tex_, TEXTURE_FILTER_POINT);
    SetTextureWrap(tex_, TEXTURE_WRAP_REPEAT);
}

void Background::draw(int originX, int originY, int layerW, int layerH) const {
    if (!tex_.id) return;
    float ox = std::fmod(static_cast<float>(originX) * kParallax, static_cast<float>(kSize));
    float oy = std::fmod(static_cast<float>(originY) * kParallax, static_cast<float>(kSize));
    if (ox < 0.0f) ox += kSize;
    if (oy < 0.0f) oy += kSize;
    Rectangle src{std::floor(ox), std::floor(oy), static_cast<float>(layerW), static_cast<float>(layerH)};
    DrawTextureRec(tex_, src, Vector2{0.0f, 0.0f}, WHITE);
}

} // namespace pm
