#include "scenes/MenuScene.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <vector>

#include "app/App.hpp"
#include "app/InputLatch.hpp"
#include "app/RunRecords.hpp"
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
    if (!app.options().automated()) suspended_ = loadSuspendedRun();
    const std::string& ui = app.options().openUi;
    if (ui == "loadout") panel_ = Panel::Loadout;
    else if (ui == "grimoire") panel_ = Panel::Grimoire;
    else if (ui == "help") panel_ = Panel::Help;
    else if (ui == "settings") panel_ = Panel::Settings;
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

Milestones MenuScene::milestones() const { return milestonesOf(app_.profile()); }

void MenuScene::abandonSuspendedRun() {
    if (!suspended_) return;
    recordRun(app_.profile(), *suspended_, false, "Abandoned the Work");
    discardSuspendedRun();
    suspended_.reset();
}

void MenuScene::replayRun(const RunHistoryEntry& entry) {
    abandonSuspendedRun();
    LoadoutId loadout = LoadoutId::Apprentice;
    if (entry.loadout >= 0 && entry.loadout < kLoadoutCount && loadoutUnlocked(static_cast<LoadoutId>(entry.loadout), milestones()))
        loadout = static_cast<LoadoutId>(entry.loadout);
    const auto diff = static_cast<Difficulty>(clamp(entry.difficulty, 0, 2));
    const int ordeal = std::min(entry.ordeal, app_.profile().ordealUnlocked);
    app_.setScene(std::make_unique<GameScene>(app_, newRun(entry.seed, diff, false, loadout, ordeal)));
}

void MenuScene::startRun(bool daily, LoadoutId loadout) {
    abandonSuspendedRun();
    Difficulty diff = static_cast<Difficulty>(clamp(app_.settings().difficulty, 0, 2));
    uint32_t seed = daily ? dailySeed() : randomRunSeed();
    if (daily) {
        app_.profile().lastDailySeed = static_cast<int>(seed);
        saveProfile(app_.profile());
    }
    // The Daily Rite is the same trial for everyone: no ordeal.
    const int ordeal = daily ? 0 : std::min(app_.settings().ordeal, app_.profile().ordealUnlocked);
    app_.setScene(std::make_unique<GameScene>(app_, newRun(seed, diff, daily, loadout, ordeal)));
}

void MenuScene::tick() {
    World& w = *world_;
    // Stage directions: pour something interesting every few seconds.
    nextEvent_ -= kTickDt;
    if (nextEvent_ <= 0.0f) {
        nextEvent_ = rng_.range(1.5f, 3.5f);
        static constexpr MatId kPours[] = {mat::Sand, mat::Water, mat::Lava, mat::Oil, mat::Acid, mat::Snow, mat::Gunpowder, mat::Vitae};
        MatId m = rng_.pick(kPours);
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
    // Drains: matter reaching the bottom of a drain pit (just above its bedrock) leaves the stage.
    for (int y = kMenuH - 6; y < kMenuH - 3; ++y)
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
    scene_.addEmissiveLights();
    LightingParams lp;
    lp.highQuality = app_.settings().highQuality;
    lp.ambientR = 0.12f;
    lp.ambientG = 0.11f;
    lp.ambientB = 0.15f;
    scene_.present(lp, time_);

    switch (panel_) {
    case Panel::Main: drawMain(); break;
    case Panel::Settings:
        ui::dimScreen(120);
        if (drawSettingsPanel(app_)) panel_ = Panel::Main;
        break;
    case Panel::Help: drawHelp(); break;
    case Panel::Loadout: drawLoadouts(); break;
    case Panel::Grimoire:
        grimoire_.setReplayAbandonsRun(suspended_.has_value());
        if (grimoire_.draw(app_)) panel_ = Panel::Main;
        if (const auto replay = grimoire_.takeReplayRequest(); replay && *replay < app_.profile().history.size()) {
            const RunHistoryEntry entry = app_.profile().history[*replay]; // copied: abandoning may rewrite history
            replayRun(entry);
        }
        break;
    }
}

void MenuScene::drawMain() {
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    Vector2 sub = scene_.worldToScreen({kMenuW * 0.5f, static_cast<float>(titleBottom_) + 12.0f});
    ui::text("a falling-sand alchemy roguelite", sw * 0.5f, sub.y, ui::fontSize(ui::scale()), Color{210, 190, 240, 230},
             ui::Align::Center);

    float bw = 150.0f * s, bh = 18.0f * s, gap = 4.0f * s;
    const int buttons = suspended_ ? 9 : 8;
    float x = sw * 0.5f - bw * 0.5f, y = std::min(sh * 0.44f, sh - 26.0f * s - static_cast<float>(buttons) * (bh + gap));
    nav_.update(buttons);
    int index = 0;
    Rectangle last{};
    // One menu entry: a button that the mouse, keyboard or gamepad can press.
    auto item = [&](const std::string& label) {
        last = {x, y, bw, bh};
        const bool pressed = ui::button(last, label, true, nav_.focused(index)) || nav_.activated(index);
        ++index;
        y += bh + gap;
        return pressed;
    };

    if (suspended_) {
        if (item("Continue the Work")) {
            app_.setScene(std::make_unique<GameScene>(app_, *suspended_));
            return;
        }
        if (ui::hovered(last)) {
            const RunState& r = *suspended_;
            std::string where = r.inSanctum ? std::string("Sanctum after ") + biomeDef(biomeForDepth(r.levelIndex)).name
                                            : std::string(stageNumeral(r.levelIndex)) + ". " + biomeDef(biomeForDepth(r.levelIndex)).name;
            ui::tooltip("Continue the Work", where + TextFormat("  -  %s, %d gold", loadoutDef(r.loadout).name, r.gold), ui::color::kGold);
        }
    }
    if (item("Begin the Great Work")) panel_ = Panel::Loadout;
    if (suspended_ && ui::hovered(last))
        ui::tooltip("Begin the Great Work", "Starting anew abandons your suspended run.", ui::color::kDanger);
    // Everyone attempts the daily rite with the same seed and the same kit. With a run
    // suspended, the first press only asks for confirmation.
    const int dailyIndex = index;
    if (item(confirmDaily_ ? "Abandon run for the Rite?" : "Daily Rite")) {
        if (suspended_ && !confirmDaily_) confirmDaily_ = true;
        else startRun(true, LoadoutId::Apprentice);
    }
    const bool dailyHot = ui::hovered(last) || nav_.focused(dailyIndex);
    if (!dailyHot) confirmDaily_ = false;
    if (ui::hovered(last)) {
        if (suspended_)
            ui::tooltip("Daily Rite", "Today's seed with the Apprentice's kit. Starting it abandons your suspended run.",
                        ui::color::kDanger);
        else
            ui::tooltip("Daily Rite", "Today's seed, the same for every alchemist, with the Apprentice's kit.", ui::color::kGold);
    }
    Settings& st = app_.settings();
    int d = clamp(st.difficulty, 0, 2);
    if (item(std::string("Difficulty: ") + kDifficultyNames[d])) {
        st.difficulty = (d + 1) % 3;
        saveSettings(st);
    }
    if (ui::hovered(last)) ui::tooltip(kDifficultyNames[d], kDifficultyHints[d], ui::color::kGold);
    if (item("Alchemy Sandbox")) app_.setScene(std::make_unique<SandboxScene>(app_));
    if (item("Grimoire")) panel_ = Panel::Grimoire;
    if (item("How to Play")) panel_ = Panel::Help;
    if (item("Settings")) panel_ = Panel::Settings;
    if (item("Quit")) app_.requestQuit();

    const Profile& p = app_.profile();
    std::string stats = TextFormat("Runs %d   Great Works completed %d   Deepest stage %d", p.runs, p.victories, p.deepest);
    if (p.bestTime > 0.0f) stats += "   Best time " + ui::formatClock(p.bestTime);
    ui::text(stats, sw * 0.5f, sh - static_cast<float>(small) - 8.0f * s, small, Color{180, 170, 190, 220}, ui::Align::Center);
    if (!p.history.empty()) {
        const RunHistoryEntry& lastRun = p.history.front();
        const std::string line = lastRun.victory ? "Last descent: the Great Work completed"
                                                 : "Last descent: " + lastRun.fate + " - " + depthLabel(lastRun);
        ui::text(line, sw * 0.5f, sh - static_cast<float>(small) * 2.3f - 8.0f * s, small, Color{150, 135, 160, 200},
                 ui::Align::Center);
    }
    ui::text("v" PM_VERSION_STRING, sw - 6.0f * s, sh - static_cast<float>(small) - 8.0f * s, small, Color{120, 110, 130, 200},
             ui::Align::Right);
}

void MenuScene::drawLoadouts() {
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale());
    const int small = ui::smallFontSize();
    const Milestones m = milestones();
    Settings& st = app_.settings();
    auto selected = static_cast<LoadoutId>(clamp(st.loadout, 0, kLoadoutCount - 1));
    if (!loadoutUnlocked(selected, m)) selected = LoadoutId::Apprentice;

    ui::dimScreen(150);
    const float rowH = 36.0f * s, rowGap = 4.0f * s;
    Rectangle r{sw * 0.5f - 210.0f * s, sh * 0.5f - 143.0f * s, 420.0f * s, 286.0f * s};
    ui::panel(r);
    ui::text("CHOOSE YOUR PATH", sw * 0.5f, r.y + 8.0f * s, ui::fontSize(ui::scale() + 1), ui::color::kGold, ui::Align::Center);

    float y = r.y + 28.0f * s;
    std::string tipTitle, tipBody;
    for (int i = 0; i < kLoadoutCount; ++i) {
        const auto id = static_cast<LoadoutId>(i);
        const LoadoutDef& def = loadoutDef(id);
        const bool unlocked = loadoutUnlocked(id, m);
        const bool isSelected = id == selected;
        Rectangle row{r.x + 10.0f * s, y, r.width - 20.0f * s, rowH};
        const bool hover = unlocked && ui::hovered(row);
        ui::panel(row, isSelected ? Color{58, 42, 86, 240} : Color{24, 19, 32, 230},
                  isSelected ? ui::color::kGold : (hover ? ui::color::kAccent : ui::color::kPanelBorder));
        const float tx = row.x + 8.0f * s;
        if (unlocked) {
            ui::text(def.name, tx, row.y + 5.0f * s, size, isSelected ? ui::color::kGold : ui::color::kText);
            ui::text(def.description, tx, row.y + 7.0f * s + static_cast<float>(size), small, ui::color::kTextDim);
            // Starting kit preview: the first wand's spells.
            const float icon = 14.0f * s;
            float ix = row.x + row.width - 8.0f * s - icon;
            if (def.randomKit) {
                ui::text("?", ix + icon * 0.5f, row.y + (rowH - static_cast<float>(size)) * 0.5f, size, ui::color::kAccent,
                         ui::Align::Center);
            } else {
                const RunState kit = newRun(1, Difficulty::Adept, false, id);
                const std::vector<SpellId>& slots = kit.wands[static_cast<size_t>(def.signatureWand)].slots;
                for (auto it = slots.rbegin(); it != slots.rend(); ++it) {
                    if (*it == SpellId::None) continue;
                    Rectangle ir{ix, row.y + (rowH - icon) * 0.5f, icon, icon};
                    ui::spellIcon(*it, ir);
                    if (ui::hovered(ir)) {
                        tipTitle = spellDef(*it).name;
                        tipBody = spellDetails(*it);
                    }
                    ix -= icon + 3.0f * s;
                }
            }
            if (hover && input::mousePressed(MOUSE_BUTTON_LEFT)) {
                if (isSelected) {
                    startRun(false, id);
                    return;
                }
                st.loadout = i;
                saveSettings(st);
            }
        } else {
            ui::text("Sealed", tx, row.y + 5.0f * s, size, Color{110, 100, 120, 255});
            ui::text(loadoutRequirement(id, m), tx, row.y + 7.0f * s + static_cast<float>(size), small, ui::color::kTextDim);
        }
        y += rowH + rowGap;
    }
    // Arrow keys move the selection between unlocked paths.
    const bool down = ui::navDownPressed();
    const bool up = ui::navUpPressed();
    const int dir = (down ? 1 : 0) - (up ? 1 : 0);
    if (dir != 0) {
        for (int step = 1; step < kLoadoutCount; ++step) {
            const int next = (static_cast<int>(selected) + dir * step + kLoadoutCount) % kLoadoutCount;
            if (loadoutUnlocked(static_cast<LoadoutId>(next), m)) {
                st.loadout = next;
                saveSettings(st);
                break;
            }
        }
    }

    drawOrdealRow({r.x + 10.0f * s, y, r.width - 20.0f * s, 20.0f * s}, tipTitle, tipBody);

    const float bw = 100.0f * s, bh = 16.0f * s, by = r.y + r.height - 24.0f * s;
    if (ui::button({sw * 0.5f - bw - 4.0f * s, by, bw, bh}, "Begin") || ui::confirmPressed()) {
        startRun(false, selected);
        return;
    }
    if (ui::button({sw * 0.5f + 4.0f * s, by, bw, bh}, "Back") || ui::backPressed()) panel_ = Panel::Main;
    if (!tipTitle.empty()) ui::tooltip(tipTitle, tipBody, ui::color::kAccent);
}

void MenuScene::drawOrdealRow(Rectangle row, std::string& tipTitle, std::string& tipBody) {
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale());
    const int small = ui::smallFontSize();
    const float cx = row.x + row.width * 0.5f;
    const int unlocked = app_.profile().ordealUnlocked;
    if (unlocked <= 0) {
        ui::text("Ordeals open once the Great Work is complete", cx, row.y + (row.height - static_cast<float>(small)) * 0.5f, small,
                 ui::color::kTextDim, ui::Align::Center);
        return;
    }
    // Left and right (or the arrows) choose how hard; the choice is kept for later runs.
    Settings& st = app_.settings();
    int ordeal = std::min(st.ordeal, unlocked);
    const float arrowW = 18.0f * s;
    const bool less = ui::navLeftPressed();
    const bool more = ui::navRightPressed();
    if (ui::button({row.x, row.y, arrowW, row.height}, "<") || less) ordeal = std::max(0, ordeal - 1);
    if (ui::button({row.x + row.width - arrowW, row.y, arrowW, row.height}, ">") || more) ordeal = std::min(unlocked, ordeal + 1);
    if (ordeal != st.ordeal) {
        st.ordeal = ordeal;
        saveSettings(st);
    }
    const std::string label = ordeal > 0 ? ordealLabel(ordeal) + ": " + ordealDef(ordeal).name : "No ordeal";
    ui::text(label, cx, row.y + (row.height - static_cast<float>(size)) * 0.5f, size, ordeal > 0 ? Color{255, 140, 110, 255} : ui::color::kTextDim,
             ui::Align::Center);
    if (ui::hovered(row) && tipTitle.empty()) {
        tipTitle = ordeal > 0 ? ordealLabel(ordeal) : "Ordeals";
        tipBody = ordeal > 0 ? ordealRules(ordeal)
                             : TextFormat("Each ordeal adds a rule to those beneath it. Complete the Great Work under the highest "
                                          "open to unlock the next (%d of %d open).",
                                          unlocked, kMaxOrdeal);
    }
}

void MenuScene::drawHelp() {
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale());
    const int small = ui::smallFontSize();

    // Controls as a two-column key / action table.
    struct Binding {
        const char* keys;
        const char* action;
    };
    static constexpr Binding kBindings[] = {
        {"A / D", "move"},          {"Right click", "pour the flask"},
        {"W / Space", "jump, hold to levitate"}, {"Q (hold)", "absorb into the flask"},
        {"Mouse", "aim"},           {"F", "drink from the flask"},
        {"Left click", "cast"},     {"E", "pick up, open, buy"},
        {"1-4 / wheel", "switch wand"}, {"M", "map"},
        {"Tab", "edit wands"},      {"Esc", "pause"},
    };
    // The rules of matter, one short paragraph each.
    static constexpr Binding kTips[] = {
        {"Matter", "Every pixel is simulated. Fire spreads through wood, oil and gunpowder; water cools lava into obsidian; "
                   "acid eats stone. Blast gold ore and walk over the dust to collect it."},
        {"Wands", "Spells cast in order. Modifiers boost the next projectile, multicasts fire several at once and triggers "
                  "release the next spell where they land."},
        {"Flask", "Carry any liquid or powder. Douse fires, dissolve walls, set oil traps - or drink: Vitae heals, "
                  "quicksilver lends haste and ichor fury. "
                  "Pour what a transmutation circle craves into it to earn a perk."},
        {"The Work", "Find the portal at the bottom of each stage. Sanctums between stages heal you and sell spells. "
                     "Vaults hide treasure off the main route."},
    };
    const float rowH = static_cast<float>(small) * 1.45f;
    const float tableH = rowH * static_cast<float>((std::size(kBindings) + 1) / 2) + 8.0f * s;

    // Size the panel from its content: at small UI scales the body font does not shrink, so the
    // panel widens (fewer wrapped lines) and grows instead of letting text run over the Back button.
    auto contentHeight = [&](float width) {
        float h = 30.0f * s + tableH;
        for (const Binding& tip : kTips)
            h += static_cast<float>(size) + 1.0f * s + ui::wrappedHeight(tip.action, width - 24.0f * s, small) + 4.0f * s;
        return h + 28.0f * s; // room for the Back button
    };
    float width = 420.0f * s;
    float height = std::max(200.0f * s, contentHeight(width));
    if (height > sh - 16.0f * s) {
        width = std::max(width, std::min(sw - 16.0f * s, 640.0f * s));
        height = std::max(200.0f * s, contentHeight(width));
    }
    height = std::min(height, sh - 8.0f * s);

    ui::dimScreen(150);
    const Rectangle r{sw * 0.5f - width * 0.5f, sh * 0.5f - height * 0.5f, width, height};
    ui::panel(r);
    ui::text("HOW TO PLAY", sw * 0.5f, r.y + 8.0f * s, ui::fontSize(ui::scale() + 1), ui::color::kGold, ui::Align::Center);

    const float colW = (r.width - 24.0f * s) * 0.5f, keyW = 62.0f * s;
    float y = r.y + 30.0f * s;
    for (size_t i = 0; i < std::size(kBindings); ++i) {
        const float cx = r.x + 12.0f * s + static_cast<float>(i % 2) * colW, cy = y + static_cast<float>(i / 2) * rowH;
        ui::text(kBindings[i].keys, cx + keyW, cy, small, ui::color::kGold, ui::Align::Right);
        ui::text(kBindings[i].action, cx + keyW + 8.0f * s, cy, small, ui::color::kText);
    }
    y += tableH;

    for (const Binding& tip : kTips) {
        ui::text(tip.keys, r.x + 12.0f * s, y, size, ui::color::kAccent);
        y += static_cast<float>(size) + 1.0f * s;
        y += ui::textWrapped(tip.action, r.x + 12.0f * s, y, r.width - 24.0f * s, small, ui::color::kTextDim) + 4.0f * s;
    }
    if (ui::button({sw * 0.5f - 50.0f * s, r.y + r.height - 24.0f * s, 100.0f * s, 16.0f * s}, "Back") || ui::backPressed())
        panel_ = Panel::Main;
}

} // namespace pm
