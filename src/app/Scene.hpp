#pragma once
// A scene is one screen of the game (menu, run, sandbox...). The App drives it with a fixed
// 60 Hz simulation tick plus a variable-rate render call with an interpolation factor.

namespace pm {

class App;

class Scene {
public:
    explicit Scene(App& app) : app_(app) {}
    virtual ~Scene() = default;

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    /// Fixed-rate simulation step (kTickRate per second).
    virtual void tick() = 0;
    /// Called once per rendered frame before render(); `dt` is real elapsed seconds.
    virtual void frame(float dt) { (void)dt; }
    /// Draws the scene. `alpha` in [0,1) is the fraction of the next tick already elapsed.
    virtual void render(float alpha) = 0;
    /// Window size changed.
    virtual void onResize(int width, int height) { (void)width; (void)height; }

protected:
    App& app_;
};

} // namespace pm
