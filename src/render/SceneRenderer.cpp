#include "render/SceneRenderer.hpp"

#include <cmath>

#include "EmbeddedShaders.hpp"
#include "rlgl.h"

namespace pm {

SceneRenderer::SceneRenderer() {
    lightShader_.load(embedded::light_fs, "light.fs");
    downShader_.load(embedded::down_fs, "down.fs");
    upShader_.load(embedded::up_fs, "up.fs");
    compositeShader_.load(embedded::composite_fs, "composite.fs");
    lights_.reserve(kMaxLights);
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
    if (static_cast<int>(lights_.size()) >= kMaxLights) return;
    // Cull lights whose radius doesn't reach the view.
    float lx = light.pos.x - static_cast<float>(view_.originX);
    float ly = light.pos.y - static_cast<float>(view_.originY);
    if (lx + light.radius < 0 || ly + light.radius < 0 || lx - light.radius > view_.texW ||
        ly - light.radius > view_.texH)
        return;
    lights_.push_back(light);
}

void SceneRenderer::runLightPass() {
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
    runLightPass();
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
