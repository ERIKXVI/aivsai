#pragma once
// Alchemy Sandbox: free-form painting with every material. Doubles as a test bed for the
// simulation and renderer.

#include <memory>

#include "app/Scene.hpp"
#include "render/WorldRenderer.hpp"
#include "sim/World.hpp"

namespace pm {

class SandboxScene final : public Scene {
public:
    explicit SandboxScene(App& app);

    void tick() override;
    void frame(float dt) override;
    void render(float alpha) override;

private:
    void paint(int cx, int cy, int radius, MatId m);
    void buildInitialScene();

    std::unique_ptr<World> world_;
    WorldRenderer renderer_;
    MatId brush_ = mat::Sand;
    int brushRadius_ = 6;
    float simMs_ = 0.0f;
    bool paused_ = false;
};

} // namespace pm
