#include "scenes/SandboxScene.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "app/App.hpp"
#include "app/InputLatch.hpp"
#include "audio/Audio.hpp"
#include "raylib.h"
#include "scenes/MenuScene.hpp"
#include "ui/Ui.hpp"

namespace pm {

namespace {

constexpr int kWorldW = 640;
constexpr int kWorldH = 384;
constexpr int kMaxBrush = 40;

struct PaletteGroup {
    const char* title;
    Phase phase;
};

constexpr PaletteGroup kGroups[] = {
    {"Solids", Phase::Solid}, {"Powders", Phase::Powder}, {"Liquids", Phase::Liquid}, {"Gases", Phase::Gas}, {"Energy", Phase::Fire},
};

bool paintable(MatId m) { return m != mat::Empty && m != mat::Bedrock; }

} // namespace

SandboxScene::SandboxScene(App& app) : Scene(app), world_(std::make_unique<World>(kWorldW, kWorldH, 1234u)) {
    buildInitialScene();
    background_.setBiome(BiomeId::Nigredo, 3u);
    camera_ = {world_->width() * 0.5f, world_->height() * 0.5f};
    zoom_ = SceneRenderer::kViewHeightCells / static_cast<float>(world_->height());
}

void SandboxScene::buildInitialScene() {
    World& w = *world_;
    for (int y = 0; y < w.height(); ++y)
        for (int x = 0; x < w.width(); ++x) w.clear(x, y);
    for (int x = 0; x < w.width(); ++x) {
        for (int y = w.height() - 20; y < w.height(); ++y) w.place(x, y, mat::Rock);
        w.place(x, 0, mat::Bedrock);
        w.place(x, w.height() - 1, mat::Bedrock);
    }
    for (int y = 0; y < w.height(); ++y) {
        w.place(0, y, mat::Bedrock);
        w.place(w.width() - 1, y, mat::Bedrock);
    }
    // A wooden bowl of oil, a pool of water, a lava basin, a sand heap, a keg of gunpowder
    // and a glass vat of acid: everything needed for a first experiment.
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
    for (int y = 322; y < 364; ++y)
        for (int x = 222; x < 246; ++x) w.place(x, y, (x == 222 || x == 245 || y == 322) ? mat::Wood : mat::Gunpowder);
    for (int y = 230; y < 300; ++y) {
        w.place(500, y, mat::Glass);
        w.place(560, y, mat::Glass);
    }
    for (int x = 500; x <= 560; ++x) w.place(x, 300, mat::Glass);
    for (int y = 262; y < 300; ++y)
        for (int x = 501; x < 560; ++x) w.place(x, y, mat::Acid);
    w.wakeAll();
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

void SandboxScene::torch(int cx, int cy) {
    // Flames in open air, and anything flammable nearby catches.
    for (int k = 0; k < 4; ++k) {
        const int x = cx + static_cast<int>(world_->randomBits() % 7) - 3, y = cy + static_cast<int>(world_->randomBits() % 7) - 3;
        if (!world_->inBounds(x, y)) continue;
        if (world_->at(x, y).mat == mat::Empty) world_->place(x, y, mat::Fire);
        else world_->ignite(x, y);
    }
}

void SandboxScene::explode(int cx, int cy, int radius) {
    // Blast a crater; its rim catches fire and the air fills with smoke.
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
            const int d2 = dx * dx + dy * dy;
            const int x = cx + dx, y = cy + dy;
            if (d2 > radius * radius || !world_->inBounds(x, y)) continue;
            const MaterialDef& def = matDef(world_->at(x, y).mat);
            if (def.hardness >= 200) continue;
            const uint32_t r = world_->randomBits();
            if (d2 > (radius - 3) * (radius - 3)) {
                if ((r & 3u) == 0) world_->place(x, y, mat::Fire);
                else if (def.phase == Phase::Solid && (r & 4u)) continue;
                else world_->clear(x, y);
            } else {
                world_->place(x, y, (r & 7u) == 0 ? mat::Smoke : mat::Empty);
            }
        }
    for (int k = 0; k < 24; ++k) world_->ignite(cx + static_cast<int>(world_->randomBits() % 17) - 8, cy + static_cast<int>(world_->randomBits() % 17) - 8);
    flashes_.push_back({{static_cast<float>(cx), static_cast<float>(cy)}, 0.35f});
    if (world_->tick() != lastBlastSound_) {
        lastBlastSound_ = world_->tick();
        app_.audio().play(radius > 12 ? Sfx::ExplosionBig : Sfx::ExplosionSmall, 0.8f);
    }
}

void SandboxScene::tick() {
    Vec2 m = renderer_.screenToWorld(GetMousePosition());
    int mx = floorToInt(m.x), my = floorToInt(m.y);
    // A stroke paints only if the button went down over the canvas (not on the palette, nor
    // the click that opened the sandbox); releasing the button re-arms it.
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) paintArmed_ = true;
    if (!IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) eraseArmed_ = true;
    if (!pointerOverUi_) {
        if (paintArmed_ && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) paint(mx, my, brushRadius_, brush_);
        if (eraseArmed_ && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) paint(mx, my, brushRadius_, mat::Empty);
    }
    if (IsKeyDown(KEY_F)) torch(mx, my);
    if (explodeQueued_) {
        explode(mx, my, std::max(8, brushRadius_ + 6));
        explodeQueued_ = false;
    }

    for (Flash& f : flashes_) f.life -= kTickDt;
    flashes_.erase(std::remove_if(flashes_.begin(), flashes_.end(), [](const Flash& f) { return f.life <= 0.0f; }), flashes_.end());

    if (paused_) return;
    auto t0 = std::chrono::steady_clock::now();
    world_->step(app_.pool());
    auto t1 = std::chrono::steady_clock::now();
    float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    simMs_ = simMs_ * 0.95f + ms * 0.05f;
    for (const SimBlast& b : world_->takeBlasts())
        explode(floorToInt(b.center.x), floorToInt(b.center.y), static_cast<int>(b.radius()));
}

void SandboxScene::frame(float dt) {
    app_.audio().update(dt);
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) brushRadius_ = clamp(brushRadius_ + (wheel > 0 ? 1 : -1), 1, kMaxBrush);
    if (input::keyPressed(KEY_LEFT_BRACKET)) brushRadius_ = std::max(1, brushRadius_ - 1);
    if (input::keyPressed(KEY_RIGHT_BRACKET)) brushRadius_ = std::min(kMaxBrush, brushRadius_ + 1);
    if (input::keyPressed(KEY_P) || input::keyPressed(KEY_SPACE)) paused_ = !paused_;
    if (input::keyPressed(KEY_X) || input::mousePressed(MOUSE_BUTTON_MIDDLE)) explodeQueued_ = true;
    if (input::keyPressed(KEY_ESCAPE)) app_.setScene(std::make_unique<MenuScene>(app_));
}

Rectangle SandboxScene::drawPalette() {
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale()), small = ui::smallFontSize();
    constexpr int kColumns = 7;
    const float swatch = 12.0f * s, gap = 2.0f * s;
    const float panelW = kColumns * (swatch + gap) + 14.0f * s;
    Rectangle panel{6.0f * s, 6.0f * s, panelW, static_cast<float>(app_.screenHeight()) - 12.0f * s};
    ui::panel(panel, Color{16, 12, 22, 215});
    float x0 = panel.x + 8.0f * s, y = panel.y + 6.0f * s;
    ui::text("ALCHEMY", panel.x + panel.width * 0.5f, y, size, ui::color::kGold, ui::Align::Center);
    y += static_cast<float>(size) + 6.0f * s;

    MatId hovered = mat::Empty;
    for (const PaletteGroup& g : kGroups) {
        ui::text(g.title, x0, y, small, ui::color::kTextDim);
        y += static_cast<float>(small) + 2.0f * s;
        int col = 0;
        for (int i = 1; i < kMaterialCount; ++i) {
            const auto m = static_cast<MatId>(i);
            if (!paintable(m) || matDef(m).phase != g.phase) continue;
            Rectangle r{x0 + static_cast<float>(col) * (swatch + gap), y, swatch, swatch};
            const Rgba c = materialColor(m);
            DrawRectangleRec(r, Color{c.r, c.g, c.b, 255});
            const bool selected = m == brush_;
            DrawRectangleLinesEx(r, selected ? 2.0f * s * 0.5f + 1.0f : 1.0f, selected ? WHITE : Color{0, 0, 0, 160});
            if (ui::hovered(r)) {
                hovered = m;
                if (input::mousePressed(MOUSE_BUTTON_LEFT)) brush_ = m;
            }
            if (++col == kColumns) {
                col = 0;
                y += swatch + gap;
            }
        }
        if (col != 0) y += swatch + gap;
        y += 4.0f * s;
    }

    // Brush size and actions.
    ui::text(TextFormat("Brush %d", brushRadius_), x0, y, small, ui::color::kText);
    const float bw = 14.0f * s;
    if (ui::button({panel.x + panel.width - 2.0f * bw - 10.0f * s, y - 2.0f * s, bw, 12.0f * s}, "-")) brushRadius_ = std::max(1, brushRadius_ - 2);
    if (ui::button({panel.x + panel.width - bw - 8.0f * s, y - 2.0f * s, bw, 12.0f * s}, "+")) brushRadius_ = std::min(kMaxBrush, brushRadius_ + 2);
    y += 14.0f * s;
    const float fullW = panel.width - 16.0f * s, bh = 14.0f * s;
    if (ui::button({x0, y, fullW, bh}, paused_ ? "Resume" : "Pause")) paused_ = !paused_;
    y += bh + 3.0f * s;
    if (ui::button({x0, y, fullW, bh}, "Clear")) {
        for (int yy = 1; yy < world_->height() - 1; ++yy)
            for (int xx = 1; xx < world_->width() - 1; ++xx) world_->clear(xx, yy);
        world_->wakeAll();
    }
    y += bh + 3.0f * s;
    if (ui::button({x0, y, fullW, bh}, "Reset")) buildInitialScene();
    y += bh + 3.0f * s;
    if (ui::button({x0, y, fullW, bh}, "Main Menu")) app_.setScene(std::make_unique<MenuScene>(app_));

    if (hovered != mat::Empty) ui::tooltip(matDef(hovered).name, "", ui::color::kText);
    return panel;
}

void SandboxScene::render(float alpha) {
    (void)alpha;
    ui::beginFrame(app_.screenWidth(), app_.screenHeight());
    // Centre the world in the space right of the palette.
    const float cellPx = static_cast<float>(app_.screenHeight()) / SceneRenderer::kViewHeightCells * zoom_;
    camera_.x = world_->width() * 0.5f - paletteWidth_ * 0.5f / std::max(0.1f, cellPx);
    renderer_.beginFrame(camera_, app_.screenWidth(), app_.screenHeight(), zoom_);
    const ViewInfo& v = renderer_.view();
    WorldRenderer& layer = renderer_.worldLayer();
    layer.build(*world_, v.originX, v.originY, world_->tick(), &app_.pool());
    layer.upload();

    renderer_.beginAlbedo();
    ClearBackground(BLACK);
    background_.draw(v.originX, v.originY, v.texW, v.texH);
    renderer_.drawWorldAlbedo();
    renderer_.endAlbedo();
    renderer_.beginEmission();
    renderer_.endEmission();

    const Vec2 m = renderer_.screenToWorld(GetMousePosition());
    renderer_.addLight(PointLight{m, 1.6f, 1.3f, 1.0f, 120.0f, 0.0f, 100.0f});
    for (const Flash& f : flashes_) renderer_.addLight(PointLight{f.pos, 6.0f * f.life, 4.0f * f.life, 2.0f * f.life, 140.0f, 0.0f, 2.0f});
    renderer_.addEmissiveLights();

    ClearBackground(BLACK);
    LightingParams lp;
    lp.highQuality = app_.settings().highQuality;
    lp.ambientR = 0.72f;
    lp.ambientG = 0.70f;
    lp.ambientB = 0.76f;
    renderer_.present(lp, static_cast<float>(app_.time()));

    // Brush outline.
    const Vector2 c = GetMousePosition();
    const float rpx = static_cast<float>(brushRadius_) * v.scale;
    const Rectangle panel = drawPalette();
    paletteWidth_ = panel.x + panel.width;
    pointerOverUi_ = CheckCollisionPointRec(c, panel);
    // A press that lands on the palette must not turn into a stroke when dragged off it.
    if (pointerOverUi_ && input::mousePressed(MOUSE_BUTTON_LEFT)) paintArmed_ = false;
    if (pointerOverUi_ && input::mousePressed(MOUSE_BUTTON_RIGHT)) eraseArmed_ = false;
    if (!pointerOverUi_) {
        const Rgba bc = materialColor(brush_);
        DrawCircleLinesV(c, rpx, Color{bc.r, bc.g, bc.b, 200});
        DrawCircleLinesV(c, rpx + 1.0f, Color{0, 0, 0, 120});
    }

    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const float hx = panel.x + panel.width + (sw - panel.x - panel.width) * 0.5f;
    ui::text(TextFormat("%s   brush %d%s", matDef(brush_).name, brushRadius_, paused_ ? "   PAUSED" : ""), hx, 8.0f * s,
             ui::fontSize(ui::scale()), ui::color::kText, ui::Align::Center);
    ui::text("LMB paint   RMB erase   Wheel brush size   F fire   X / MMB blast   Space pause   Esc menu", hx,
             sh - static_cast<float>(small) - 8.0f * s, small, ui::color::kTextDim, ui::Align::Center);
    if (app_.settings().showFps)
        ui::text(TextFormat("sim %.2f ms  chunks %d  fps %d", simMs_, world_->activeChunkCount(), GetFPS()), sw - 8.0f * s, 8.0f * s,
                 small, ui::color::kTextDim, ui::Align::Right);
}

} // namespace pm
