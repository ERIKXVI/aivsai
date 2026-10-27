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
#include "render/LightBudget.hpp"
#include "render/WorldRenderer.hpp"

namespace pm {

struct LightingParams {
    float ambientR = 0.16f, ambientG = 0.15f, ambientB = 0.19f;
    float glowLight = 2.5f;
    float bloom = 0.5f;
    float exposure = 1.0f;
    float saturation = 1.0f;
    float tintR = 1.0f, tintG = 1.0f, tintB = 1.0f;
    float damageFlash = 0.0f;
    float aberration = 0.0f; ///< chromatic split in cells
    float airR = 0.0f, airG = 0.0f, airB = 0.0f; ///< haze added to open air, so unlit caves keep their shape
    float airTop = -1.0e6f;  ///< layer row the haze fades in below (keeps a sky above it clear)
    bool highQuality = true; ///< false: shorter shadow rays and a smaller light budget for weak GPUs
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
    static constexpr int kMaxLights = 64;      ///< light budget on High (the shader's slot count)
    static constexpr int kLowQualityLights = 28; ///< light budget on Low
    static constexpr int kMaxEmissiveLights = 8;
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

    /// Registers a candidate light; the budget picks which ones are drawn when presenting.
    void addLight(const PointLight& light);
    /// Adds a refractive ring centred on a world position for this frame.
    void addShockwave(Vec2 world, float radius, float strength);
    /// Turns the brightest emissive regions of the world layer (lava, fire, glowing fungus)
    /// into shadow-casting point lights, one per world-aligned 48-cell super-block (at most
    /// kMaxEmissiveLights). Call after the world layer is uploaded.
    void addEmissiveLights(int maxLights = kMaxEmissiveLights);
    /// Forgets light fades, so a new level does not inherit the last one's lights.
    void resetLights() {
        selector_.reset();
        litClusters_.clear();
    }
    /// Lights drawn and lights registered in the last presented frame.
    int lightsSelected() const { return lightsSelected_; }
    int lightCandidates() const { return lightCandidates_; }

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
    void runLightPass(bool highQuality, float dt);
    void runGlowPass();
    /// The world cells the layers cover; lights must reach it to be drawn.
    Rectf layerArea() const;

    ViewInfo view_;
    WorldRenderer world_;
    RenderTexture2D albedo_{}, emission_{}, light_{};
    std::array<RenderTexture2D, kGlowLevels> glow_{};
    int allocW_ = 0, allocH_ = 0;

    gpu::ShaderProgram lightShader_, downShader_, upShader_, compositeShader_;
    std::vector<PointLight> lights_;
    LightSelector selector_;
    /// One cell of the emissive clustering grid, rebuilt every frame.
    struct SuperBlock {
        float x = 0, y = 0, r = 0, g = 0, b = 0, weight = 0;
        float blockLight = 0.0f; ///< summed brightness of its blocks as separate lights
        float peak = 0.0f;       ///< weight of its brightest block
        float score = 0.0f;
        uint32_t key = 0;
    };
    std::vector<SuperBlock> superBlocks_;
    std::vector<uint32_t> clusterOrder_;
    std::vector<uint32_t> litClusters_; ///< keys of the super-blocks lit last frame
    float lastTime_ = -1.0f;
    int lightsSelected_ = 0, lightCandidates_ = 0;
    std::vector<float> lightPosData_, lightColorData_;
    static constexpr int kMaxShockwaves = 6;
    std::vector<float> shockData_;
};

} // namespace pm
