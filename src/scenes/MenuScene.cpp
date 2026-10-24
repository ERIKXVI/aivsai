#include "scenes/MenuScene.hpp"

#include <cmath>
#include <ctime>
#include <vector>

#include "app/App.hpp"
#include "core/Timing.hpp"
#include "game/Run.hpp"
#include "scenes/GameScene.hpp"
#include "scenes/SandboxScene.hpp"
#include "ui/SettingsPanel.hpp"
#include "ui/Ui.hpp"

namespace pm {

namespace {

constexpr int kMenuW = 512;
constexpr int kMenuH = 288;
constexpr const char* kTitle = "PRIMA MATERIA";
constexpr const char* kDifficultyNames[] = {"Apprentice", "Adept", "Archmage"};
constexpr const char* kDifficultyHints[] = {"Gentler foes, more health.", "The intended experience.",
                                            "For seasoned alchemists."};

uint32_t dailySeed() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    int ymd = (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
    return hash32(static_cast<uint32_t>(ymd) ^ 0xDA11u);
}

} // namespace

MenuScene::MenuScene(App& app) : Scene(app), rng_(static_cast<uint64_t>(std::time(nullptr))) {
    buildWorld();
    background_.setBiome(BiomeId::Sanctum, 7u);
}

void MenuScene::buildWorld() {
    world_ = std::make_unique<World>(kMenuW, kMenuH, 99u);
    World& w = *world_;
    // Floor with drains so poured matter flows away instead of filling the screen.
    for (int x = 0; x < kMenuW; ++x) {
        bool drain = (x / 40) % 3 == 1;
        for (int y = kMenuH - 18; y < kMenuH; ++y) {
            float wobble = std::sin(static_cast<float>(x) * 0.07f) * 3.0f;
            if (y < kMenuH - 14 + static_cast<int>(wobble)) continue;
            if (drain && y < kMenuH - 3) continue;
            Cell c = makeCell(y > kMenuH - 6 ? mat::Bedrock : mat::Basalt, hash2(x, y, 3u));
            w.at(x, y) = c;
        }
    }
    // Carve the title from runestone with a golden rim.
    const int fontSize = 30;
    Image text = ImageText(kTitle, fontSize, WHITE);
    const int tx = (kMenuW - text.width) / 2, ty = 40;
    titleTop_ = ty;
    titleBottom_ = ty + text.height;
    auto* px = static_cast<Color*>(text.data);
    // Embolden the thin pixel font by one pixel so the letters read as solid stone.
    std::vector<uint8_t> solid(static_cast<size_t>(text.width) * text.height, 0);
    for (int y = 0; y < text.height; ++y)
        for (int x = 0; x < text.width; ++x)
            if (px[y * text.width + x].a >= 128) {
                solid[static_cast<size_t>(y) * text.width + x] = 1;
                if (x + 1 < text.width) solid[static_cast<size_t>(y) * text.width + x + 1] = 1;
                if (y + 1 < text.height) solid[static_cast<size_t>(y + 1) * text.width + x] = 1;
            }
    for (int y = 0; y < text.height; ++y) {
        for (int x = 0; x < text.width; ++x) {
            if (!solid[static_cast<size_t>(y) * text.width + x]) continue;
            bool edge = false;
            for (int dy = -1; dy <= 1 && !edge; ++dy)
                for (int dx = -1; dx <= 1 && !edge; ++dx) {
                    int nx = x + dx, ny = y + dy;
                    edge = nx < 0 || ny < 0 || nx >= text.width || ny >= text.height || !solid[static_cast<size_t>(ny) * text.width + nx];
                }
            Cell c = makeCell(edge ? mat::GoldOre : mat::Rune, hash2(x, y, 11u));
            c.var = static_cast<uint8_t>(edge ? 250 : 120 + (y * 5));
            w.at(tx + x, ty + y) = c;
        }
    }
    UnloadImage(text);
    // A shelf under the title catches the pours.
    for (int x = tx - 20; x < tx + 20 + (kMenuW - 2 * tx); ++x)
        for (int y = titleBottom_ + 6; y < titleBottom_ + 9; ++y) w.at(x, y) = makeCell(mat::Obsidian, hash2(x, y, 5u));
    w.wakeAll();
}

void MenuScene::startRun(bool daily) {
    Difficulty diff = static_cast<Difficulty>(clamp(app_.settings().difficulty, 0, 2));
    uint32_t seed = daily ? dailySeed() : static_cast<uint32_t>(GetRandomValue(1, 0x7FFFFFFF));
    if (daily) {
        app_.profile().lastDailySeed = static_cast<int>(seed);
        saveProfile(app_.profile());
    }
    app_.setScene(std::make_unique<GameScene>(app_, newRun(seed, diff, daily)));
}

void MenuScene::tick() {
    World& w = *world_;
    // Stage directions: pour something interesting every few seconds.
    nextEvent_ -= kTickDt;
    if (nextEvent_ <= 0.0f) {
        nextEvent_ = rng_.range(1.5f, 3.5f);
        static constexpr MatId kPours[] = {mat::Sand, mat::Water, mat::Lava, mat::Oil, mat::Acid, mat::Snow, mat::Gunpowder, mat::Vitae};
        MatId m = kPours[rng_.rangeInt(0, 7)];
        int x = rng_.rangeInt(40, kMenuW - 40);
        int amount = rng_.rangeInt(150, 400);
        for (int i = 0; i < amount; ++i) {
            Cell c = makeCell(m, rng_.next());
            particles_.addMatter({static_cast<float>(x) + rng_.range(-3.0f, 3.0f), rng_.range(2.0f, 10.0f)},
                                 {rng_.range(-15.0f, 15.0f), rng_.range(0.0f, 40.0f + static_cast<float>(i) * 0.6f)}, c, 6.0f);
        }
        if (m == mat::Oil || m == mat::Gunpowder) {
            // Light it a moment later with a falling ember.
            Cell ember = makeCell(mat::Fire, rng_.next());
            ember.life = 200;
            particles_.addMatter({static_cast<float>(x), 1.0f}, {0.0f, 120.0f}, ember, 3.0f);
        }
    }
    // Drains: matter reaching the bottom edge leaves the stage.
    for (int y = kMenuH - 3; y < kMenuH; ++y)
        for (int x = 0; x < kMenuW; ++x) {
            Phase p = phaseOf(w.at(x, y).mat);
            if (p == Phase::Liquid || p == Phase::Powder) w.clear(x, y);
        }
    w.step(app_.pool());
    w.events().clear();
    particles_.update(w, kTickDt);
}

void MenuScene::frame(float dt) {
    time_ += dt;
    app_.audio().update(dt);
    app_.audio().setMusicMood(48, 5, 0.0f);
}

void MenuScene::render(float alpha) {
    (void)alpha;
    ui::beginFrame(app_.screenWidth(), app_.screenHeight());
    const float zoom = SceneRenderer::kViewHeightCells / static_cast<float>(kMenuH);
    scene_.beginFrame({kMenuW * 0.5f, kMenuH * 0.5f}, app_.screenWidth(), app_.screenHeight(), zoom);
    const ViewInfo& v = scene_.view();
    WorldRenderer& layer = scene_.worldLayer();
    layer.build(*world_, v.originX, v.originY, world_->tick());
    particles_.draw(layer);
    layer.upload();

    scene_.beginAlbedo();
    ClearBackground(BLACK);
    background_.draw(v.originX, v.originY, v.texW, v.texH);
    scene_.drawWorldAlbedo();
    scene_.endAlbedo();
    scene_.beginEmission();
    scene_.endEmission();

    // Two drifting lanterns light the title.
    float t = time_;
    scene_.addLight(PointLight{{kMenuW * 0.3f + std::sin(t * 0.4f) * 60.0f, 30.0f}, 1.6f, 1.2f, 0.7f, 170.0f, 0.0f});
    scene_.addLight(PointLight{{kMenuW * 0.7f + std::sin(t * 0.33f + 2.0f) * 60.0f, 110.0f}, 0.7f, 0.8f, 1.8f, 170.0f, 0.0f});
    scene_.addEmissiveLights(16);
    LightingParams lp;
    lp.ambientR = 0.12f;
    lp.ambientG = 0.11f;
    lp.ambientB = 0.15f;
    scene_.present(lp, time_);

    switch (panel_) {
    case Panel::Main: drawMain(); break;
    case Panel::Settings:
        DrawRectangle(0, 0, app_.screenWidth(), app_.screenHeight(), Color{0, 0, 0, 120});
        if (drawSettingsPanel(app_)) panel_ = Panel::Main;
        break;
    case Panel::Help: drawHelp(); break;
    }
}

void MenuScene::drawMain() {
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const float s = static_cast<float>(ui::scale());
    const int small = ui::fontSize(std::max(1, ui::scale() - 1));
    Vector2 sub = scene_.worldToScreen({kMenuW * 0.5f, static_cast<float>(titleBottom_) + 12.0f});
    ui::text("a falling-sand alchemy roguelite", sw * 0.5f, sub.y, ui::fontSize(ui::scale()), Color{210, 190, 240, 230},
             ui::Align::Center);

    float bw = 150.0f * s, bh = 18.0f * s, gap = 5.0f * s;
    float x = sw * 0.5f - bw * 0.5f, y = sh * 0.46f;
    if (ui::button({x, y, bw, bh}, "Begin the Great Work")) startRun(false);
    y += bh + gap;
    if (ui::button({x, y, bw, bh}, "Daily Rite")) startRun(true);
    y += bh + gap;
    Settings& st = app_.settings();
    int d = clamp(st.difficulty, 0, 2);
    if (ui::button({x, y, bw, bh}, std::string("Difficulty: ") + kDifficultyNames[d])) {
        st.difficulty = (d + 1) % 3;
        saveSettings(st);
    }
    if (ui::hovered({x, y, bw, bh})) ui::tooltip(kDifficultyNames[d], kDifficultyHints[d], ui::color::kGold);
    y += bh + gap;
    if (ui::button({x, y, bw, bh}, "Alchemy Sandbox")) app_.setScene(std::make_unique<SandboxScene>(app_));
    y += bh + gap;
    if (ui::button({x, y, bw, bh}, "How to Play")) panel_ = Panel::Help;
    y += bh + gap;
    if (ui::button({x, y, bw, bh}, "Settings")) panel_ = Panel::Settings;
    y += bh + gap;
    if (ui::button({x, y, bw, bh}, "Quit")) app_.requestQuit();

    const Profile& p = app_.profile();
    std::string stats = TextFormat("Runs %d   Great Works completed %d   Deepest stage %d", p.runs, p.victories, p.deepest);
    if (p.bestTime > 0.0f) stats += TextFormat("   Best time %d:%02d", static_cast<int>(p.bestTime) / 60, static_cast<int>(p.bestTime) % 60);
    ui::text(stats, sw * 0.5f, sh - static_cast<float>(small) - 8.0f * s, small, Color{180, 170, 190, 220}, ui::Align::Center);
    ui::text("v" PM_VERSION_STRING, sw - 6.0f * s, sh - static_cast<float>(small) - 8.0f * s, small, Color{120, 110, 130, 200},
             ui::Align::Right);
}

void MenuScene::drawHelp() {
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale());
    const int small = ui::fontSize(std::max(1, ui::scale() - 1));
    DrawRectangle(0, 0, app_.screenWidth(), app_.screenHeight(), Color{0, 0, 0, 150});
    Rectangle r{sw * 0.5f - 200.0f * s, sh * 0.5f - 130.0f * s, 400.0f * s, 260.0f * s};
    ui::panel(r);
    ui::text("HOW TO PLAY", sw * 0.5f, r.y + 8.0f * s, ui::fontSize(ui::scale() + 1), ui::color::kGold, ui::Align::Center);
    float y = r.y + 30.0f * s;
    const char* controls[] = {
        "A / D  -  move                  W / Space  -  jump, hold to levitate",
        "Mouse  -  aim                   Left click  -  cast the active wand",
        "1-4 / wheel  -  switch wand     Tab  -  edit wands (drag spells)",
        "Right click  -  pour your flask  Q  -  absorb liquids and powders",
        "F  -  drink from the flask       E  -  pick up / open / buy",
    };
    for (const char* c : controls) {
        ui::text(c, r.x + 12.0f * s, y, small, ui::color::kText);
        y += static_cast<float>(small) * 1.35f;
    }
    y += 6.0f * s;
    ui::text("The alchemist's craft", r.x + 12.0f * s, y, size, ui::color::kAccent);
    y += static_cast<float>(size) + 4.0f * s;
    const char* tips =
        "Every pixel is matter. Fire spreads through wood, oil and gunpowder. Water cools lava into obsidian. "
        "Acid eats stone. Gold ore crumbles into gold dust when blasted - walk over it to collect. "
        "Wands cast their spells in order: modifiers boost the next projectile, multicasts fire several at once, "
        "and trigger spells release the next spell where they land. Reach the portal at the bottom of each "
        "stage; rest and shop in the sanctum between stages; complete the Great Work in the Athanor.";
    ui::textWrapped(tips, r.x + 12.0f * s, y, r.width - 24.0f * s, small, ui::color::kTextDim);
    if (ui::button({sw * 0.5f - 50.0f * s, r.y + r.height - 24.0f * s, 100.0f * s, 16.0f * s}, "Back") || IsKeyPressed(KEY_ESCAPE))
        panel_ = Panel::Main;
}

} // namespace pm
