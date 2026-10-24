#pragma once
// Title screen. The background is a live simulation: the title is carved in gold and
// runestone while sand, water and lava pour over it and react, lit by the full pipeline.

#include <memory>

#include "app/Scene.hpp"
#include "render/Background.hpp"
#include "render/SceneRenderer.hpp"
#include "sim/Particles.hpp"
#include "sim/World.hpp"

namespace pm {

class MenuScene final : public Scene {
public:
    explicit MenuScene(App& app);

    void tick() override;
    void frame(float dt) override;
    void render(float alpha) override;

private:
    enum class Panel { Main, Settings, Help };

    void buildWorld();
    void startRun(bool daily);
    void drawMain();
    void drawHelp();

    std::unique_ptr<World> world_;
    ParticleSystem particles_;
    SceneRenderer scene_;
    Background background_;
    Rng rng_;
    Panel panel_ = Panel::Main;
    float time_ = 0.0f;
    float nextEvent_ = 2.0f;
    int titleTop_ = 0, titleBottom_ = 0;
};

} // namespace pm
