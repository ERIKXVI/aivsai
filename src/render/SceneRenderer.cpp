#include "render/SceneRenderer.hpp"

#include <algorithm>
#include <cmath>

#include "EmbeddedShaders.hpp"
#include "rlgl.h"

namespace pm {

namespace {

/// Fade key of an emissive cluster: the world block (not the view-local one) holding its centre.
uint32_t emissiveKey(int blockX, int blockY) {
    return lightKey(LightKind::Emissive, (static_cast<uint32_t>(blockX) & 0x3FFFu) | ((static_cast<uint32_t>(blockY) & 0x3FFFu) << 14));
}

} // namespace

SceneRenderer::SceneRenderer() {
    lightShader_.load(embedded::light_fs, "light.fs");
    downShader_.load(embedded::down_fs, "down.fs");
    upShader_.load(embedded::up_fs, "up.fs");
    compositeShader_.load(embedded::composite_fs, "composite.fs");
    lights_.reserve(kMaxLights * 2);
}

SceneRenderer::~SceneRenderer() { releaseTargets(); }

void SceneRenderer::releaseTargets() {
    gpu::destroyTarget(albedo_);
    gpu::destroyTarget(emission_);
    gpu::destroyTarget(light_);
    for (auto& g : glow_) gpu::destroyTarget(g);
    allocW_ = allocH_ = 0;
}

void SceneRenderer::beginFrame(Vec2 cameraCenter, int screenW, int screenH, float zoom) {
    screenW = std::max(1, screenW);
    screenH = std::max(1, screenH);
    ViewInfo v;
    v.screenW = screenW;
    v.screenH = screenH;
    v.scale = static_cast<float>(screenH) / (kViewHeightCells / zoom);
    v.viewW = static_cast<float>(screenW) / v.scale;
    v.viewH = static_cast<float>(screenH) / v.scale;
    float left = cameraCenter.x - v.viewW * 0.5f;
    float top = cameraCenter.y - v.viewH * 0.5f;
    float fl = std::floor(left), ft = std::floor(top);
    v.fracX = left - fl;
    v.fracY = top - ft;
    v.originX = static_cast<int>(fl) - 1;
    v.originY = static_cast<int>(ft) - 1;
    v.texW = static_cast<int>(std::ceil(v.viewW)) + 3;
    v.texH = static_cast<int>(std::ceil(v.viewH)) + 3;
    view_ = v;
    lights_.clear();
    shockData_.clear();
    ensureTargets();
}

void SceneRenderer::ensureTargets() {
    if (allocW_ == view_.texW && allocH_ == view_.texH) return;
    releaseTargets();
    allocW_ = view_.texW;
    allocH_ = view_.texH;
    albedo_ = gpu::createTarget(allocW_, allocH_, false, true);
    emission_ = gpu::createTarget(allocW_, allocH_, false, true);
    light_ = gpu::createTarget(allocW_, allocH_, true, true);
    int gw = allocW_, gh = allocH_;
    for (auto& g : glow_) {
        gw = std::max(1, gw / 2);
        gh = std::max(1, gh / 2);
        g = gpu::createTarget(gw, gh, true, true);
    }
    world_.resize(allocW_, allocH_);
}

void SceneRenderer::beginAlbedo() { BeginTextureMode(albedo_); }

void SceneRenderer::drawWorldAlbedo() {
    gpu::BlendScope blend(gpu::Blend::Alpha);
    DrawTexture(world_.colorTexture(), 0, 0, WHITE);
}

void SceneRenderer::endAlbedo() { EndTextureMode(); }

void SceneRenderer::beginEmission() {
    BeginTextureMode(emission_);
    gpu::BlendScope blend(gpu::Blend::Replace);
    DrawTexture(world_.emissionTexture(), 0, 0, WHITE);
}

void SceneRenderer::endEmission() { EndTextureMode(); }

void SceneRenderer::addLight(const PointLight& light) {
    // Cull lights whose radius doesn't reach the view.
    float lx = light.pos.x - static_cast<float>(view_.originX);
    float ly = light.pos.y - static_cast<float>(view_.originY);
    if (lx + light.radius < 0 || ly + light.radius < 0 || lx - light.radius > view_.texW ||
        ly - light.radius > view_.texH)
        return;
    lights_.push_back(light);
}

void SceneRenderer::addShockwave(Vec2 world, float radius, float strength) {
    if (static_cast<int>(shockData_.size()) >= kMaxShockwaves * 4) return;
    shockData_.insert(shockData_.end(), {world.x - static_cast<float>(view_.originX), world.y - static_cast<float>(view_.originY),
                                         radius, strength});
}

void SceneRenderer::addEmissiveLights(int maxLights) {
    const std::vector<EmissiveBlock>& blocks = world_.emissiveBlocks();
    const int bw = world_.blocksWide(), bh = world_.blocksHigh();
    const int maxClusters = std::min(maxLights, kMaxEmissiveLights);
    // Bright blocks seed clusters, heaviest first, and each cluster takes the unclaimed blocks
    // around its seed: a lava lake becomes a few broad lights instead of one per block.
    std::vector<int> seeds;
    for (int i = 0; i < static_cast<int>(blocks.size()); ++i)
        if (blocks[static_cast<size_t>(i)].weight > 2500.0f) seeds.push_back(i);
    std::stable_sort(seeds.begin(), seeds.end(),
                     [&](int a, int b) { return blocks[static_cast<size_t>(a)].weight > blocks[static_cast<size_t>(b)].weight; });
    std::vector<uint8_t> used(blocks.size(), 0);
    std::vector<uint32_t> claimed;
    for (int seed : seeds) {
        if (static_cast<int>(claimed.size()) >= maxClusters) break;
        if (used[static_cast<size_t>(seed)]) continue;
        const int sx = seed % bw, sy = seed / bw;
        float x = 0, y = 0, r = 0, g = 0, b = 0, weight = 0;
        float blockLight = 0.0f; // what the blocks would have shone as separate lights
        for (int ny = std::max(0, sy - 1); ny <= std::min(bh - 1, sy + 1); ++ny) {
            for (int nx = std::max(0, sx - 1); nx <= std::min(bw - 1, sx + 1); ++nx) {
                const size_t j = static_cast<size_t>(ny) * bw + nx;
                const EmissiveBlock& e = blocks[j];
                if (used[j] || e.weight <= 0.0f) continue;
                used[j] = 1;
                blockLight += std::min(std::sqrt(e.weight) / 140.0f, 2.2f);
                x += e.x * e.weight;
                y += e.y * e.weight;
                r += e.r;
                g += e.g;
                b += e.b;
                weight += e.weight;
            }
        }
        x /= weight;
        y /= weight;

        // Keep the key of a cluster seen last frame within a block of this one, so a centre
        // wobbling across a block edge does not restart its fade.
        const int kx = floorDiv(floorToInt(x), 1 << WorldRenderer::kBlockShift);
        const int ky = floorDiv(floorToInt(y), 1 << WorldRenderer::kBlockShift);
        uint32_t key = 0;
        float bestFade = -1.0f;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const uint32_t k = emissiveKey(kx + dx, ky + dy);
                if (!selector_.tracks(k) || std::find(claimed.begin(), claimed.end(), k) != claimed.end()) continue;
                const float f = selector_.fade(k) + (dx == 0 && dy == 0 ? 0.01f : 0.0f);
                if (f > bestFade) {
                    bestFade = f;
                    key = k;
                }
            }
        }
        if (key == 0 && std::find(claimed.begin(), claimed.end(), emissiveKey(kx, ky)) == claimed.end()) key = emissiveKey(kx, ky);
        claimed.push_back(key);

        const float total = r + g + b;
        // One broad light stands in for several blocks: take the geometric mean of the merged
        // brightness and the blocks' summed brightness, so a pool keeps its glow without a hot
        // spot at its centre.
        const float intensity = std::max(0.2f, std::sqrt(std::sqrt(weight) / 140.0f * blockLight));
        const float k = intensity * 3.0f / total;
        const float radius = std::min(35.0f + std::sqrt(weight) * 0.22f, 120.0f);
        addLight(PointLight{{x, y}, r * k, g * k, b * k, radius, 2.5f, 3.0f, key});
    }
}

void SceneRenderer::runLightPass(bool highQuality, float dt) {
    // Every registered light competes for the budget; Low quality has fewer slots and shorter
    // shadow rays.
    lightCandidates_ = static_cast<int>(lights_.size());
    const float viewDiag = std::sqrt(view_.viewW * view_.viewW + view_.viewH * view_.viewH);
    selector_.select(lights_, highQuality ? kMaxLights : kLowQualityLights, viewDiag, dt);
    lightsSelected_ = static_cast<int>(lights_.size());
    lightPosData_.clear();
    lightColorData_.clear();
    for (const PointLight& l : lights_) {
        lightPosData_.insert(lightPosData_.end(), {l.pos.x - static_cast<float>(view_.originX),
                                                   l.pos.y - static_cast<float>(view_.originY), l.radius, l.bodySkip});
        lightColorData_.insert(lightColorData_.end(), {l.r, l.g, l.b, 1.0f});
    }
    BeginTextureMode(light_);
    {
        gpu::BlendScope blend(gpu::Blend::Replace);
        BeginShaderMode(lightShader_.raw());
        lightShader_.set("texSize", Vec2{static_cast<float>(allocW_), static_cast<float>(allocH_)});
        lightShader_.set("lightCount", static_cast<int>(lights_.size()));
        lightShader_.set("stepLimit", highQuality ? 40.0f : 16.0f);
        lightShader_.setVec4Array("lightPosRadius", lightPosData_.data(), static_cast<int>(lights_.size()));
        lightShader_.setVec4Array("lightColor", lightColorData_.data(), static_cast<int>(lights_.size()));
        gpu::drawFullTarget(emission_.texture, allocW_, allocH_);
        EndShaderMode();
    }
    EndTextureMode();
}

void SceneRenderer::runGlowPass() {
    // Downsample chain.
    const Texture2D* src = &emission_.texture;
    for (int i = 0; i < kGlowLevels; ++i) {
        BeginTextureMode(glow_[i]);
        gpu::BlendScope blend(gpu::Blend::Replace);
        BeginShaderMode(downShader_.raw());
        downShader_.set("srcTexel", Vec2{1.0f / static_cast<float>(src->width), 1.0f / static_cast<float>(src->height)});
        downShader_.set("prefilter", i == 0 ? 1.0f : 0.0f);
        gpu::drawFullTarget(*src, glow_[i].texture.width, glow_[i].texture.height);
        EndShaderMode();
        EndTextureMode();
        src = &glow_[i].texture;
    }
    // Upsample and accumulate back up to level 0.
    for (int i = kGlowLevels - 2; i >= 0; --i) {
        const Texture2D& coarse = glow_[i + 1].texture;
        BeginTextureMode(glow_[i]);
        gpu::BlendScope blend(gpu::Blend::Additive);
        BeginShaderMode(upShader_.raw());
        upShader_.set("srcTexel", Vec2{1.0f / static_cast<float>(coarse.width), 1.0f / static_cast<float>(coarse.height)});
        upShader_.set("weight", 1.0f);
        gpu::drawFullTarget(coarse, glow_[i].texture.width, glow_[i].texture.height);
        EndShaderMode();
        EndTextureMode();
    }
}

void SceneRenderer::present(const LightingParams& p, float time) {
    // Light fades follow the caller's clock; a stalled or restarted clock just holds them.
    const float dt = lastTime_ < 0.0f ? 0.0f : clamp(time - lastTime_, 0.0f, 0.25f);
    lastTime_ = time;
    runLightPass(p.highQuality, dt);
    runGlowPass();

    const float w = static_cast<float>(allocW_), h = static_cast<float>(allocH_);
    const float u0 = (1.0f + view_.fracX) / w;
    const float u1 = (1.0f + view_.fracX + view_.viewW) / w;
    const float v0 = 1.0f - (1.0f + view_.fracY) / h;
    const float v1 = 1.0f - (1.0f + view_.fracY + view_.viewH) / h;

    gpu::BlendScope blend(gpu::Blend::Replace);
    BeginShaderMode(compositeShader_.raw());
    compositeShader_.setTexture("lightTex", light_.texture);
    compositeShader_.setTexture("glowTex", glow_[0].texture);
    compositeShader_.setTexture("emissionTex", emission_.texture);
    compositeShader_.set("texSize", Vec2{w, h});
    compositeShader_.set("pixelScale", view_.scale);
    compositeShader_.set("ambient", p.ambientR, p.ambientG, p.ambientB);
    compositeShader_.set("glowLight", p.glowLight);
    compositeShader_.set("bloom", p.bloom);
    compositeShader_.set("exposure", p.exposure);
    compositeShader_.set("time", time);
    compositeShader_.set("screenSize", Vec2{static_cast<float>(view_.screenW), static_cast<float>(view_.screenH)});
    compositeShader_.set("damageFlash", p.damageFlash);
    compositeShader_.set("saturation", p.saturation);
    compositeShader_.set("tint", p.tintR, p.tintG, p.tintB);
    compositeShader_.set("aberration", p.aberration);
    const int shocks = static_cast<int>(shockData_.size() / 4);
    compositeShader_.set("shockCount", shocks);
    if (shocks > 0) compositeShader_.setVec4Array("shockwaves", shockData_.data(), shocks);
    gpu::drawQuad(albedo_.texture, 0.0f, 0.0f, static_cast<float>(view_.screenW), static_cast<float>(view_.screenH), u0, v0,
                  u1, v1);
    EndShaderMode();
}

Vec2 SceneRenderer::screenToWorld(Vector2 screen) const {
    return {static_cast<float>(view_.originX) + 1.0f + view_.fracX + screen.x / view_.scale,
            static_cast<float>(view_.originY) + 1.0f + view_.fracY + screen.y / view_.scale};
}

Vector2 SceneRenderer::worldToScreen(Vec2 world) const {
    return {(world.x - static_cast<float>(view_.originX) - 1.0f - view_.fracX) * view_.scale,
            (world.y - static_cast<float>(view_.originY) - 1.0f - view_.fracY) * view_.scale};
}

} // namespace pm
