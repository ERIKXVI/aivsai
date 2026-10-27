#include "render/SceneRenderer.hpp"

#include <algorithm>
#include <cmath>

#include "EmbeddedShaders.hpp"
#include "rlgl.h"

namespace pm {

namespace {

constexpr float kSqrtWeightPerLight = 140.0f; ///< emissive weight to light intensity, in the sqrt domain
constexpr float kMaxSoloBlockLight = 2.2f;    ///< brightest one emissive block may be as a light

/// Brightness of one emissive block of summed intensity `weight`, as a light of its own.
float soloBlockLight(float weight) { return std::min(std::sqrt(weight) / kSqrtWeightPerLight, kMaxSoloBlockLight); }

/// Fade key of an emissive cluster: its world super-block coordinates.
uint32_t emissiveKey(int superX, int superY) {
    return lightKey(LightKind::Emissive, (static_cast<uint32_t>(superX) & 0x3FFFu) | ((static_cast<uint32_t>(superY) & 0x3FFFu) << 14));
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
    if (lightReaches(light, layerArea())) lights_.push_back(light);
}

Rectf SceneRenderer::layerArea() const {
    return Rectf{static_cast<float>(view_.originX), static_cast<float>(view_.originY), static_cast<float>(view_.texW),
                 static_cast<float>(view_.texH)};
}

void SceneRenderer::addShockwave(Vec2 world, float radius, float strength) {
    if (static_cast<int>(shockData_.size()) >= kMaxShockwaves * 4) return;
    shockData_.insert(shockData_.end(), {world.x - static_cast<float>(view_.originX), world.y - static_cast<float>(view_.originY),
                                         radius, strength});
}

void SceneRenderer::addEmissiveLights(int maxLights) {
    // The world-aligned 16-cell emissive blocks pool into a fixed grid of world-aligned
    // super-blocks of 3x3 blocks, so a lava lake becomes a few broad lights. Every block lies
    // inside one super-block, so a light's place and key do not shift as the camera pans or
    // shakes, only as the lava shimmers or the view edge crosses it. The heaviest super-blocks
    // are lit; one lit last frame gets a head start, so two of similar weight do not trade
    // places every frame.
    constexpr int kSuper = 3 << WorldRenderer::kBlockShift; // super-block size in cells
    constexpr float kSeedWeight = 2500.0f; // a super-block needs one block at least this bright
    constexpr float kStayBonus = 1.3f;
    const int gx0 = floorDiv(view_.originX, kSuper), gy0 = floorDiv(view_.originY, kSuper);
    const int gw = floorDiv(view_.originX + view_.texW - 1, kSuper) - gx0 + 1;
    const int gh = floorDiv(view_.originY + view_.texH - 1, kSuper) - gy0 + 1;
    superBlocks_.assign(static_cast<size_t>(gw) * gh, SuperBlock{});
    for (const EmissiveBlock& e : world_.emissiveBlocks()) {
        if (e.weight <= 0.0f) continue;
        const int sx = clamp(floorDiv(floorToInt(e.x), kSuper) - gx0, 0, gw - 1);
        const int sy = clamp(floorDiv(floorToInt(e.y), kSuper) - gy0, 0, gh - 1);
        SuperBlock& s = superBlocks_[static_cast<size_t>(sy) * gw + sx];
        s.x += e.x * e.weight;
        s.y += e.y * e.weight;
        s.r += e.r;
        s.g += e.g;
        s.b += e.b;
        s.weight += e.weight;
        s.blockLight += soloBlockLight(e.weight);
        s.peak = std::max(s.peak, e.weight);
    }

    clusterOrder_.clear();
    for (size_t i = 0; i < superBlocks_.size(); ++i) {
        SuperBlock& s = superBlocks_[i];
        if (s.weight <= 0.0f) continue;
        s.key = emissiveKey(gx0 + static_cast<int>(i % gw), gy0 + static_cast<int>(i / gw));
        const bool wasLit = std::find(litClusters_.begin(), litClusters_.end(), s.key) != litClusters_.end();
        const float bonus = wasLit ? kStayBonus : 1.0f;
        if (s.peak * bonus <= kSeedWeight) continue;
        s.score = s.weight * bonus;
        clusterOrder_.push_back(static_cast<uint32_t>(i));
    }
    const size_t count = std::min(clusterOrder_.size(), static_cast<size_t>(std::max(0, std::min(maxLights, kMaxEmissiveLights))));
    std::partial_sort(clusterOrder_.begin(), clusterOrder_.begin() + static_cast<long>(count), clusterOrder_.end(), [&](uint32_t a, uint32_t b) {
        return superBlocks_[a].score > superBlocks_[b].score || (superBlocks_[a].score == superBlocks_[b].score && a < b);
    });

    litClusters_.clear();
    for (size_t i = 0; i < count; ++i) {
        const SuperBlock& s = superBlocks_[clusterOrder_[i]];
        litClusters_.push_back(s.key);
        const float total = s.r + s.g + s.b;
        // One broad light stands in for several blocks: take the geometric mean of the merged
        // brightness and the blocks' summed brightness, so a pool keeps its glow, but never more
        // than a few times its brightest block, so a dense pool has no blown-out centre.
        const float peakLight = soloBlockLight(s.peak);
        const float intensity = clamp(std::sqrt(std::sqrt(s.weight) / kSqrtWeightPerLight * s.blockLight), 0.2f, 6.0f * peakLight);
        const float k = intensity * 3.0f / total;
        const float radius = std::min(35.0f + std::sqrt(s.weight) * 0.22f, 120.0f);
        addLight(PointLight{{s.x / s.weight, s.y / s.weight}, s.r * k, s.g * k, s.b * k, radius, 2.5f, 3.0f, s.key});
    }
}

void SceneRenderer::runLightPass(bool highQuality, float dt) {
    // Every registered light competes for the budget; Low quality has fewer slots and shorter
    // shadow rays.
    lightCandidates_ = static_cast<int>(lights_.size());
    const float viewDiag = std::sqrt(view_.viewW * view_.viewW + view_.viewH * view_.viewH);
    selector_.select(lights_, highQuality ? kMaxLights : kLowQualityLights, viewDiag, layerArea(), dt);
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
    // Light fades follow the caller's clock; a stalled or restarted clock just holds them. The
    // first frame counts as one 60th of a second, so keyed lights already show on it.
    if (lastTime_ < 0.0f) lastTime_ = time - 1.0f / 60.0f;
    const float dt = clamp(time - lastTime_, 0.0f, 0.25f);
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
    compositeShader_.set("airColor", p.airR, p.airG, p.airB);
    compositeShader_.set("airTop", p.airTop);
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
