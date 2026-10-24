#include "scenes/SandboxScene.hpp"

#include <chrono>

#include "app/App.hpp"
#include "raylib.h"

namespace pm {

namespace {
constexpr int kWorldW = 640;
constexpr int kWorldH = 384;
} // namespace

SandboxScene::SandboxScene(App& app) : Scene(app), world_(std::make_unique<World>(kWorldW, kWorldH, 1234u)) {
    buildInitialScene();
    camera_ = {world_->width() * 0.5f, world_->height() * 0.5f};
    zoom_ = SceneRenderer::kViewHeightCells / static_cast<float>(world_->height());
}

void SandboxScene::buildInitialScene() {
    World& w = *world_;
    for (int x = 0; x < w.width(); ++x) {
        for (int y = w.height() - 20; y < w.height(); ++y) w.place(x, y, mat::Rock);
        w.place(x, 0, mat::Bedrock);
    }
    for (int y = 0; y < w.height(); ++y) {
        w.place(0, y, mat::Bedrock);
        w.place(w.width() - 1, y, mat::Bedrock);
    }
    // A wooden bowl holding oil, a pool of water and a lava basin for quick experiments.
    for (int x = 80; x < 200; ++x) w.place(x, 300, mat::Wood);
    for (int y = 250; y < 300; ++y) {
        w.place(80, y, mat::Wood);
        w.place(199, y, mat::Wood);
    }
    for (int x = 81; x < 199; ++x)
        for (int y = 270; y < 300; ++y) w.place(x, y, mat::Oil);
    for (int x = 260; x < 420; ++x)
        for (int y = 330; y < 364; ++y) w.place(x, y, mat::Water);
    for (int x = 470; x < 600; ++x)
        for (int y = 340; y < 364; ++y) w.place(x, y, mat::Lava);
    for (int x = 300; x < 380; ++x)
        for (int y = 60; y < 120; ++y) w.place(x, y, mat::Sand);
}

void SandboxScene::paint(int cx, int cy, int radius, MatId m) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy > radius * radius) continue;
            int x = cx + dx, y = cy + dy;
            if (!world_->inBounds(x, y) || world_->at(x, y).mat == mat::Bedrock) continue;
            if (m == mat::Empty) world_->clear(x, y);
            else if (phaseOf(m) == Phase::Solid || world_->at(x, y).mat == mat::Empty) {
                if (phaseOf(m) != Phase::Solid && (world_->randomBits() & 3u) != 0) continue;
                world_->place(x, y, m);
            }
        }
    }
}

void SandboxScene::tick() {
    Vec2 m = renderer_.screenToWorld(GetMousePosition());
    int mx = floorToInt(m.x), my = floorToInt(m.y);
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) paint(mx, my, brushRadius_, brush_);
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) paint(mx, my, brushRadius_, mat::Empty);
    if (IsKeyPressed(KEY_F)) world_->ignite(mx, my);

    if (paused_) return;
    auto t0 = std::chrono::steady_clock::now();
    world_->step(app_.pool());
    auto t1 = std::chrono::steady_clock::now();
    float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    simMs_ = simMs_ * 0.95f + ms * 0.05f;
    world_->events().clear();
}

void SandboxScene::frame(float dt) {
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        int next = static_cast<int>(brush_) + (wheel > 0 ? 1 : -1);
        if (next <= 0) next = kMaterialCount - 1;
        if (next >= kMaterialCount) next = 1;
        brush_ = static_cast<MatId>(next);
    }
    if (IsKeyPressed(KEY_LEFT_BRACKET)) brushRadius_ = std::max(1, brushRadius_ - 1);
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) brushRadius_ = std::min(40, brushRadius_ + 1);
    if (IsKeyPressed(KEY_P)) paused_ = !paused_;
}

void SandboxScene::render(float alpha) {
    (void)alpha;
    renderer_.beginFrame(camera_, app_.screenWidth(), app_.screenHeight(), zoom_);
    const ViewInfo& v = renderer_.view();
    WorldRenderer& layer = renderer_.worldLayer();
    layer.build(*world_, v.originX, v.originY, world_->tick());
    layer.upload();

    renderer_.beginAlbedo();
    ClearBackground(Color{34, 28, 30, 255});
    renderer_.drawWorldAlbedo();
    renderer_.endAlbedo();
    renderer_.beginEmission();
    renderer_.endEmission();

    Vec2 m = renderer_.screenToWorld(GetMousePosition());
    renderer_.addLight(PointLight{m, 2.6f, 2.1f, 1.5f, 160.0f});
    renderer_.addLight(PointLight{{140.0f, 200.0f}, 0.8f, 1.4f, 3.2f, 180.0f});

    ClearBackground(BLACK);
    LightingParams lp;
    renderer_.present(lp, static_cast<float>(app_.time()));

    DrawText(TextFormat("%s  r=%d   sim %.2f ms  chunks %d  fps %d", matDef(brush_).name, brushRadius_, simMs_,
                        world_->activeChunkCount(), GetFPS()),
             12, 12, 20, RAYWHITE);
}

} // namespace pm
