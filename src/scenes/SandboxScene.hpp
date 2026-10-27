#pragma once
// Alchemy Sandbox: free-form painting with every material, a palette grouped by phase,
// fire and blasts. Doubles as a test bed for the simulation and renderer.

#include <memory>
#include <vector>

#include "app/InputLatch.hpp"
#include "app/Scene.hpp"
#include "render/Background.hpp"
#include "render/SceneRenderer.hpp"
#include "sim/World.hpp"

namespace pm {

class SandboxScene final : public Scene {
public:
    explicit SandboxScene(App& app);

    void tick() override;
    void frame(float dt) override;
    void render(float alpha) override;

private:
    struct Flash {
        Vec2 pos;
        float life;
    };

    void paint(int cx, int cy, int radius, MatId m);
    void torch(int cx, int cy);
    void explode(int cx, int cy, int radius);
    void buildInitialScene();
    /// Draws the palette and tool buttons; returns the panel bounds (screen space).
    Rectangle drawPalette();

    std::unique_ptr<World> world_;
    SceneRenderer renderer_;
    Background background_;
    std::vector<Flash> flashes_;
    Vec2 camera_{};
    float zoom_ = 1.0f;
    MatId brush_ = mat::Sand;
    int brushRadius_ = 6;
    input::WheelStepper wheel_{0.0f, 40.0f}; ///< mouse wheel to brush-size steps: one per notch, unthrottled
    float simMs_ = 0.0f;
    bool paused_ = false;
    bool pointerOverUi_ = false;
    bool paintArmed_ = false; ///< false until the button has been up once in this scene
    bool eraseArmed_ = false;
    bool explodeQueued_ = false;
    float paletteWidth_ = 0.0f; ///< screen pixels covered by the palette (last frame)
    uint32_t lastBlastSound_ = 0;
};

} // namespace pm
