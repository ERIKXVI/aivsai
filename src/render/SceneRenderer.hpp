#pragma once
// The frame pipeline. Everything in the world is drawn at "cell resolution" into layers:
//
//   albedo   : background + world cells + sprites            (RGBA8)
//   emission : glowing cells and sprites; alpha = occlusion  (RGBA8)
//   light    : shadowed point lights + solid occupancy       (RGBA16F)
//   glow     : multi-scale blur of the emission layer        (RGBA16F pyramid)
//
// The composite pass combines the layers at screen resolution with sub-cell camera offset,
// sharp-bilinear scaling, tonemapping and screen effects.

#include <array>
#include <vector>

#include "core/Math.hpp"
#include "raylib.h"
#include "render/Gpu.hpp"
#include "render/WorldRenderer.hpp"

namespace pm {

struct PointLight {
    Vec2 pos;             ///< world position in cells
    float r = 1, g = 1, b = 1;
    float radius = 64.0f; ///< in cells
    float bodySkip = 0.0f; ///< cells near the light that should not occlude it
};

struct LightingParams {
    float ambientR = 0.16f, ambientG = 0.15f, ambientB = 0.19f;
    float glowLight = 2.5f;
    float bloom = 0.5f;
    float exposure = 1.0f;
    float saturation = 1.0f;
    float tintR = 1.0f, tintG = 1.0f, tintB = 1.0f;
    float damageFlash = 0.0f;
    float aberration = 0.0f; ///< chromatic split in cells
    bool highQuality = true; ///< false: shorter shadow rays and fewer lights for weak GPUs
};

struct ViewInfo {
    int texW = 0, texH = 0;       ///< layer size in cells (view + margin)
    int originX = 0, originY = 0; ///< world cell drawn at layer texel (0, 0)
    float fracX = 0, fracY = 0;   ///< sub-cell camera offset
    float viewW = 0, viewH = 0;   ///< visible area in cells (fractional)
    float scale = 1.0f;           ///< screen pixels per cell
    int screenW = 0, screenH = 0;
};

class SceneRenderer {
public:
    static constexpr float kViewHeightCells = 270.0f;
    static constexpr int kMaxLights = 64;
    static constexpr int kGlowLevels = 4;

    SceneRenderer();
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    /// Computes this frame's view around `cameraCenter` and (re)allocates targets if needed.
    void beginFrame(Vec2 cameraCenter, int screenW, int screenH, float zoom = 1.0f);
    const ViewInfo& view() const { return view_; }

    /// CPU-side world layer; fill it with WorldRenderer::build() then stamp extra pixels.
    WorldRenderer& worldLayer() { return world_; }

    /// Albedo layer: clear + background are the caller's job (use layerPos for sprites).
    void beginAlbedo();
    void drawWorldAlbedo();
    void endAlbedo();

    /// Emission layer: the world's emission is drawn first, extra glow sprites are additive.
    void beginEmission();
    void endEmission();

    void addLight(const PointLight& light);
    /// Adds a refractive ring centred on a world position for this frame.
    void addShockwave(Vec2 world, float radius, float strength);
    /// Turns the brightest emissive regions of the world layer (lava, fire, glowing fungus)
    /// into shadow-casting point lights. Call after the world layer is uploaded.
    void addEmissiveLights(int maxLights);
    int lightCount() const { return static_cast<int>(lights_.size()); }

    /// Runs lighting, glow and composite, drawing to the current framebuffer (the screen).
    void present(const LightingParams& params, float time);

    /// World cell coordinates -> position inside the cell layers.
    Vector2 layerPos(Vec2 world) const {
        return {world.x - static_cast<float>(view_.originX), world.y - static_cast<float>(view_.originY)};
    }
    Vec2 screenToWorld(Vector2 screen) const;
    Vector2 worldToScreen(Vec2 world) const;

private:
    void ensureTargets();
    void releaseTargets();
    void runLightPass(bool highQuality);
    void runGlowPass();

    ViewInfo view_;
    WorldRenderer world_;
    RenderTexture2D albedo_{}, emission_{}, light_{};
    std::array<RenderTexture2D, kGlowLevels> glow_{};
    int allocW_ = 0, allocH_ = 0;

    gpu::ShaderProgram lightShader_, downShader_, upShader_, compositeShader_;
    std::vector<PointLight> lights_;
    std::vector<float> lightPosData_, lightColorData_;
    static constexpr int kMaxShockwaves = 6;
    std::vector<float> shockData_;
};

} // namespace pm
