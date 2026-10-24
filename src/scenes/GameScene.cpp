#include "scenes/GameScene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/App.hpp"
#include "core/Profiler.hpp"
#include "core/Timing.hpp"
#include "gen/LevelGenerator.hpp"
#include "render/Sprites.hpp"
#include "scenes/MenuScene.hpp"
#include "ui/SettingsPanel.hpp"
#include "ui/Ui.hpp"

namespace pm {

namespace {

constexpr const char* kStageNumerals[] = {"I", "II", "III", "IV", "V"};

Color hexColor(uint32_t hex, uint8_t a = 255) {
    return Color{static_cast<unsigned char>((hex >> 16) & 0xFF), static_cast<unsigned char>((hex >> 8) & 0xFF),
                 static_cast<unsigned char>(hex & 0xFF), a};
}

std::string formatTime(float seconds) {
    int s = static_cast<int>(seconds);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

} // namespace

GameScene::GameScene(App& app, RunState run)
    : Scene(app), run_(std::move(run)), levelRenderer_(scene_, app.sprites(), app.pool()), hints_(app.profile().hintsSeen) {
    requestLevel();
}

GameScene::~GameScene() {
    if (pending_.valid()) pending_.wait();
    // Closing the window mid-run still counts the run and keeps its discoveries.
    recordProfile(false);
    if (app_.options().autoplay) {
        const Profiler& prof = Profiler::instance();
        for (int i = 0; i < static_cast<int>(ProfileSection::Count); ++i) {
            auto sec = static_cast<ProfileSection>(i);
            std::printf("[profile] %-12s avg %6.3f ms  peak %6.3f ms\n", Profiler::name(sec), prof.average(sec), prof.peak(sec));
        }
    }
}

void GameScene::requestLevel() {
    mode_ = Mode::Loading;
    level_.reset();
    const bool sanctum = run_.inSanctum;
    const int index = run_.levelIndex;
    const uint32_t seed = hash3(static_cast<int>(run_.seed), index, sanctum ? 1 : 0, 0xA1C4E3u);
    ThreadPool* pool = &app_.pool();
    pending_ = std::async(std::launch::async, [sanctum, index, seed, pool]() {
        if (sanctum) return generateSanctum(index, seed, *pool);
        return generateLevel(biomeForDepth(index), index, seed, *pool);
    });
}

void GameScene::finishLoading() {
    LevelLayout layout = pending_.get();
    level_ = std::make_unique<Level>(std::move(layout), run_, app_.audio(), app_.pool());
    camera_.reset(level_->player().body.pos);
    bot_ = Bot{};
    hints_.reset();
    mode_ = Mode::Playing;
    if (app_.options().openUi == "transmuter" && !level_->transmuters().empty()) {
        level_->debugTeleport(level_->transmuters().front().pos + Vec2{-30.0f, -8.0f});
        camera_.reset(level_->player().body.pos);
    }
    if (app_.options().openUi == "inventory") mode_ = Mode::Inventory;
    else if (app_.options().openUi == "pause") mode_ = Mode::Paused;
    exiting_ = false;
    titleTimer_ = 4.0f;
    if (run_.inSanctum) {
        // Sanctums restore the alchemist completely.
        run_.hp = run_.maxHp;
        level_->player().hp = run_.maxHp;
        titleMain_ = "SANCTUM";
        titleSub_ = biomeDef(BiomeId::Sanctum).subtitle;
    } else {
        const BiomeDef& b = biomeDef(level_->biome());
        titleMain_ = std::string(kStageNumerals[std::min(4, run_.levelIndex)]) + "  " + b.name;
        titleSub_ = b.subtitle;
    }
    run_.stats.deepest = std::max(run_.stats.deepest, run_.levelIndex + 1);
}

void GameScene::advanceStage() {
    if (run_.inSanctum) {
        run_.inSanctum = false;
        run_.levelIndex++;
    } else if (run_.levelIndex >= 4) {
        mode_ = Mode::Victory;
        endTimer_ = 0.0f;
        recordProfile(true);
        return;
    } else {
        run_.inSanctum = true;
    }
    requestLevel();
}

void GameScene::latchInput() {
    if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP) ||
        IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
        latchJump_ = true;
    if (IsKeyPressed(KEY_E) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) latchInteract_ = true;
    if (IsKeyPressed(KEY_F) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) latchDrink_ = true;
    for (int i = 0; i < RunState::kMaxWands; ++i)
        if (IsKeyPressed(KEY_ONE + i)) latchWand_ = i;
    float wheel = GetMouseWheelMove();
    if (wheel > 0.0f) latchCycle_ -= 1;
    if (wheel < 0.0f) latchCycle_ += 1;
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) latchCycle_ -= 1;
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) latchCycle_ += 1;

    // Button prompts follow whichever device was used last.
    const Vector2 mouseDelta = GetMouseDelta();
    if (GetKeyPressed() != 0 || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || mouseDelta.x * mouseDelta.x + mouseDelta.y * mouseDelta.y > 4.0f) {
        usingGamepad_ = false;
    } else if (IsGamepadAvailable(0)) {
        const float lx = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X), rx = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X),
                    ry = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y);
        if (std::fabs(lx) > 0.4f || rx * rx + ry * ry > 0.2f || GetGamepadButtonPressed() != GAMEPAD_BUTTON_UNKNOWN)
            usingGamepad_ = true;
    }
}

PlayerInput GameScene::gatherInput() {
    PlayerInput in;
    float move = 0.0f;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) move -= 1.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) move += 1.0f;
    const bool pad = IsGamepadAvailable(0);
    if (pad) {
        float lx = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
        if (std::fabs(lx) > 0.25f) move = lx;
    }
    in.moveX = clamp(move, -1.0f, 1.0f);
    in.jumpPressed = latchJump_;
    in.jumpHeld = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_W) || IsKeyDown(KEY_UP) ||
                  (pad && IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN));
    in.downHeld = IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN);
    in.aim = scene_.screenToWorld(GetMousePosition());
    in.castHeld = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    in.sprayHeld = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    in.absorbHeld = IsKeyDown(KEY_Q);
    if (pad) {
        float rx = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X), ry = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y);
        if (rx * rx + ry * ry > 0.1f && level_) in.aim = level_->player().body.pos + Vec2{rx, ry}.normalized() * 80.0f;
        if (GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.3f) in.castHeld = true;
        if (GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_TRIGGER) > 0.3f) in.sprayHeld = true;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_UP)) in.absorbHeld = true;
    }
    in.interactPressed = latchInteract_;
    in.drinkPressed = latchDrink_;
    in.selectWand = latchWand_;
    in.cycleWand = latchCycle_;
    latchJump_ = latchInteract_ = latchDrink_ = false;
    latchWand_ = -1;
    latchCycle_ = 0;
    return in;
}

void GameScene::tick() {
    if (!level_ || (mode_ != Mode::Playing && mode_ != Mode::Dead)) return;
    // After death the world keeps running in slow motion for a dramatic beat.
    tickAccumulator_ += slowMo_;
    while (tickAccumulator_ >= 1.0f) {
        tickAccumulator_ -= 1.0f;
        if (level_->consumeHitStop()) continue; // freeze frame; latched input waits for the next tick
        stepLevel();
    }
}

void GameScene::stepLevel() {
    const bool controllable = mode_ == Mode::Playing && !exiting_;
    const bool autoplay = app_.options().autoplay;
    PlayerInput in;
    if (controllable) {
        in = autoplay ? bot_.think(*level_) : gatherInput();
    } else {
        in.aim = level_->player().body.pos + level_->player().aimDir();
    }
    if (app_.options().god) level_->player().hp = level_->player().maxHp;
    if (autoplay && bot_.hopelesslyStuck()) {
        level_->debugWarpAhead();
        bot_.clearStuck();
    }
    level_->tick(in);
    if (mode_ == Mode::Playing && !autoplay) hints_.update(*level_, run_, kTickDt);

    switch (level_->pollEvent()) {
    case LevelEvent::ExitReached:
        exiting_ = true;
        if (autoplay) logAutoplay(TextFormat("stage %d%s cleared at %.1fs", run_.levelIndex, run_.inSanctum ? " (sanctum)" : "", run_.stats.time));
        break;
    case LevelEvent::PlayerDied:
        if (autoplay) logAutoplay(TextFormat("died on stage %d at %.1fs, kills %d", run_.levelIndex, run_.stats.time, run_.stats.kills));
        mode_ = Mode::Dead;
        endTimer_ = 0.0f;
        slowMo_ = 0.35f;
        recordProfile(false);
        break;
    default: break;
    }
}

void GameScene::logAutoplay(const char* message) {
    std::printf("[autoplay] %s\n", message);
    std::fflush(stdout);
}

void GameScene::frame(float dt) {
    time_ += dt;
    Audio& audio = app_.audio();
    audio.update(dt);

    if (mode_ == Mode::Loading) {
        fade_ = 1.0f;
        if (pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) finishLoading();
        return;
    }
    if (mode_ == Mode::Victory) {
        endTimer_ += dt;
        fade_ = std::max(0.0f, fade_ - dt);
        return;
    }
    if (!level_) return;

    // Fades: in on arrival, out when stepping through the portal.
    if (exiting_) {
        fade_ = std::min(1.0f, fade_ + dt * 1.8f);
        if (fade_ >= 1.0f) {
            advanceStage(); // releases the current level
            return;
        }
    } else {
        fade_ = std::max(0.0f, fade_ - dt * 1.5f);
    }
    titleTimer_ = std::max(0.0f, titleTimer_ - dt);

    if (IsKeyPressed(KEY_F3)) debugOverlay_ = !debugOverlay_;
    if (mode_ == Mode::Playing) {
        latchInput();
        if (IsKeyPressed(KEY_ESCAPE) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) mode_ = Mode::Paused;
        else if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_I) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_LEFT))
            mode_ = Mode::Inventory;
    } else if (mode_ == Mode::Inventory) {
        if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_I) || IsKeyPressed(KEY_ESCAPE) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_LEFT)) {
            mode_ = Mode::Playing;
            drag_ = Drag{};
            for (Wand& w : run_.wands) w.resetDeck();
        }
    } else if (mode_ == Mode::Paused) {
        if (IsKeyPressed(KEY_ESCAPE)) mode_ = Mode::Playing;
    } else if (mode_ == Mode::Settings) {
        if (IsKeyPressed(KEY_ESCAPE)) mode_ = Mode::Paused;
    } else if (mode_ == Mode::Dead) {
        endTimer_ += dt;
    }

    // Camera and audio follow the alchemist.
    const ViewInfo& v = scene_.view();
    Vec2 viewSize{v.viewW > 0 ? v.viewW : 480.0f, v.viewH > 0 ? v.viewH : 270.0f};
    const Player& pl = level_->player();
    Vec2 aim = scene_.screenToWorld(GetMousePosition());
    if (mode_ != Mode::Playing) aim = pl.body.pos;
    camera_.update(pl.body.pos, aim, dt, {static_cast<float>(level_->world().width()), static_cast<float>(level_->world().height())},
                   viewSize);
    const BiomeDef& b = biomeDef(level_->biome());
    audio.setMusicMood(b.musicRoot, b.musicMode, level_->combatIntensity());
}

void GameScene::render(float alpha) {
    ui::beginFrame(app_.screenWidth(), app_.screenHeight());
    ScopedTimer renderTimer(ProfileSection::Render);
    ClearBackground(BLACK);
    if (level_) {
        const float a = (mode_ == Mode::Playing || mode_ == Mode::Dead) ? alpha : 1.0f;
        Vec2 cam = camera_.position(level_->shake(), time_, app_.settings().screenShake);
        levelRenderer_.render(*level_, cam, a, time_, app_.screenWidth(), app_.screenHeight());
        drawWorldOverlays(a);
        if (mode_ == Mode::Playing) drawCompass(a);
        if (mode_ != Mode::Victory) drawHud();
        if (mode_ == Mode::Playing) hints_.draw(static_cast<float>(app_.screenWidth()), static_cast<float>(app_.screenHeight()), usingGamepad_);
        drawTitleCard();
        if (!run_.inSanctum && run_.levelIndex == 0 && level_->time() < 30.0f) drawControlsHint();
    }

    switch (mode_) {
    case Mode::Inventory: drawInventory(); break;
    case Mode::Paused: drawPauseMenu(); break;
    case Mode::Settings:
        DrawRectangle(0, 0, app_.screenWidth(), app_.screenHeight(), Color{0, 0, 0, 150});
        if (drawSettingsPanel(app_)) mode_ = Mode::Paused;
        break;
    case Mode::Dead:
        if (endTimer_ > 1.6f) drawEndScreen(false);
        break;
    case Mode::Victory: drawEndScreen(true); break;
    default: break;
    }

    if (fade_ > 0.0f) {
        DrawRectangle(0, 0, app_.screenWidth(), app_.screenHeight(), Color{0, 0, 0, static_cast<unsigned char>(fade_ * 255.0f)});
    }
    if (mode_ == Mode::Loading) drawLoading();
    if (app_.settings().showFps || debugOverlay_) drawDebugOverlay();
}

void GameScene::drawWorldOverlays(float alpha) {
    const int s = ui::scale();
    // Enemy health bars.
    for (const auto& e : level_->enemies()) {
        if (!e->alive() || e->hp >= e->maxHp || e->type() == EnemyType::Boss || e->team == Team::Player) continue;
        Vec2 p = e->body.interpolated(alpha);
        Vector2 sp = scene_.worldToScreen({p.x, p.y - e->body.halfH - 4.0f});
        float w = 14.0f * s, h = 2.0f * s;
        ui::bar({sp.x - w * 0.5f, sp.y, w, h}, e->hp / e->maxHp, ui::color::kHealth);
    }
    // Floating damage numbers and pickups messages.
    for (const FloatText& t : level_->floatTexts()) {
        Vector2 sp = scene_.worldToScreen(t.pos);
        float k = clamp(t.life / 0.4f, 0.0f, 1.0f);
        int size = ui::fontSize(std::max(1, static_cast<int>(std::round(static_cast<float>(s) * t.scale))));
        ui::text(t.text, sp.x, sp.y, size, hexColor(t.color, static_cast<uint8_t>(255 * k)), ui::Align::Center);
    }
    // Transmutation circles explain themselves when the alchemist comes close.
    for (const Transmuter& t : level_->transmuters()) {
        if ((t.pos - level_->player().body.pos).length() > 110.0f) continue;
        Vector2 sp = scene_.worldToScreen(t.pos - Vec2{0.0f, 36.0f});
        const int small = ui::fontSize(std::max(1, s - 1));
        if (t.complete) {
            ui::text("The circle is spent", sp.x, sp.y, small, ui::color::kTextDim, ui::Align::Center);
            continue;
        }
        ui::text("Transmutation Circle", sp.x, sp.y, ui::fontSize(s), Color{200, 170, 255, 255}, ui::Align::Center);
        ui::text(std::string("Pour in ") + matDef(t.wants).name + " to receive a perk", sp.x,
                 sp.y + static_cast<float>(ui::fontSize(s)) + 2.0f, small, ui::color::kText, ui::Align::Center);
        const float w = 50.0f * s, h = 3.0f * s;
        Rgba m = materialColor(t.wants);
        ui::bar({sp.x - w * 0.5f, sp.y + static_cast<float>(ui::fontSize(s) + small) + 6.0f, w, h},
                static_cast<float>(t.progress) / static_cast<float>(t.required), Color{m.r, m.g, m.b, 255});
    }

    // Interaction prompt.
    if (mode_ == Mode::Playing && level_->player().alive()) {
        if (const Pickup* p = level_->focusedPickup()) {
            Vector2 sp = scene_.worldToScreen({p->body.pos.x, p->body.pos.y - 16.0f});
            std::string label = "[E] " + p->label();
            ui::text(label, sp.x, sp.y, ui::fontSize(s), p->price > 0 && run_.gold < p->price ? ui::color::kDanger : ui::color::kText,
                     ui::Align::Center);
            if (p->type == PickupType::Spell) {
                ui::text(spellDef(p->spell).description, sp.x, sp.y + 12.0f * s, ui::fontSize(std::max(1, s - 1)), ui::color::kTextDim,
                         ui::Align::Center);
            } else if (p->type == PickupType::PerkAltar) {
                ui::text(perkDef(p->perk).description, sp.x, sp.y + 12.0f * s, ui::fontSize(std::max(1, s - 1)), ui::color::kTextDim,
                         ui::Align::Center);
            }
        }
    }
}

void GameScene::drawCompass(float alpha) {
    // A faint arrow orbiting the alchemist points towards the exit portal.
    const Player& pl = level_->player();
    if (!pl.alive() || level_->isSanctum()) return;
    const Vec2 p = pl.body.interpolated(alpha);
    const Vec2 to = level_->exitPos() - p;
    const float dist = to.length();
    if (dist < 60.0f) return;
    const Vec2 dir = to / dist;
    const float s = static_cast<float>(ui::scale());
    Vector2 c = scene_.worldToScreen(p + dir * 22.0f);
    Vector2 tip{c.x + dir.x * 6.0f * s, c.y + dir.y * 6.0f * s};
    Vector2 left{c.x - dir.y * 3.0f * s, c.y + dir.x * 3.0f * s};
    Vector2 right{c.x + dir.y * 3.0f * s, c.y - dir.x * 3.0f * s};
    const float pulse = 0.55f + 0.25f * std::sin(time_ * 3.0f);
    Color col{200, 170, 255, static_cast<unsigned char>(255.0f * pulse)};
    DrawTriangle(tip, right, left, col);
    DrawTriangle(tip, left, right, col);
}

void GameScene::drawHud() {
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale());
    const int small = ui::fontSize(std::max(1, ui::scale() - 1));
    const Player& pl = level_->player();
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());

    // Health, mana, levitation, breath.
    float x = 10.0f * s, y = 10.0f * s;
    float barW = 110.0f * s;
    ui::bar({x, y, barW, 7.0f * s}, pl.hp / pl.maxHp, ui::color::kHealth);
    ui::text(TextFormat("%d / %d", static_cast<int>(std::ceil(pl.hp)), static_cast<int>(pl.maxHp)), x + 3.0f * s,
             y + 3.5f * s - static_cast<float>(small) * 0.5f, small, WHITE);
    y += 9.0f * s;
    if (!run_.wands.empty()) {
        const Wand& w = run_.wands[static_cast<size_t>(run_.activeWand)];
        ui::bar({x, y, barW, 4.0f * s}, w.mana / w.manaMax, ui::color::kMana);
        y += 5.0f * s;
        float reload = w.cooldownTotal > 0.0f ? 1.0f - w.cooldown / w.cooldownTotal : 1.0f;
        ui::bar({x, y, barW, 2.0f * s}, reload, w.recharging ? Color{255, 170, 60, 255} : Color{200, 200, 220, 255});
        y += 3.0f * s;
    }
    if (pl.levitation() < 0.999f) {
        ui::bar({x, y, barW * 0.6f, 2.0f * s}, pl.levitation(), Color{190, 170, 255, 255});
        y += 3.0f * s;
    }
    if (pl.breath() < 0.999f) {
        ui::bar({x, y, barW * 0.6f, 2.0f * s}, pl.breath(), Color{90, 200, 255, 255});
        y += 3.0f * s;
    }
    // Status icons.
    std::string statuses;
    if (pl.status.burning > 0.0f) statuses += "BURNING  ";
    if (pl.status.wet > 0.0f) statuses += "WET  ";
    if (pl.status.oiled > 0.0f) statuses += "OILED  ";
    if (pl.status.poisoned > 0.0f) statuses += "POISONED  ";
    if (pl.status.chilled > 0.0f) statuses += "CHILLED  ";
    if (!statuses.empty()) ui::text(statuses, x, y + 2.0f * s, small, Color{255, 200, 120, 255});

    // Gold and stage.
    ui::text(TextFormat("%d gold", run_.gold), sw - 10.0f * s, 10.0f * s, size, ui::color::kGold, ui::Align::Right);
    std::string stage = run_.inSanctum ? "Sanctum" : std::string(kStageNumerals[std::min(4, run_.levelIndex)]) + ". " + biomeDef(level_->biome()).name;
    ui::text(stage, sw - 10.0f * s, 10.0f * s + static_cast<float>(size) + 2.0f * s, small, ui::color::kTextDim, ui::Align::Right);

    // Wands and the active deck.
    const float icon = 13.0f * s;
    float wx = 10.0f * s, wy = sh - icon - 10.0f * s;
    for (size_t i = 0; i < run_.wands.size(); ++i) {
        const Wand& w = run_.wands[i];
        bool active = static_cast<int>(i) == run_.activeWand;
        Rectangle r{wx, wy, icon, icon};
        ui::panel(r, active ? Color{60, 44, 90, 235} : Color{20, 16, 28, 220}, active ? ui::color::kAccent : ui::color::kPanelBorder);
        // Tiny wand glyph.
        Vector2 a{r.x + 3.0f * s, r.y + r.height - 3.0f * s}, b{r.x + r.width - 3.0f * s, r.y + 3.0f * s};
        DrawLineEx(a, b, 1.5f * s, hexColor(w.handleColor));
        DrawCircleV(b, 1.5f * s, hexColor(w.gemColor));
        ui::text(std::to_string(i + 1), r.x + 1.0f * s, r.y, small, ui::color::kTextDim);
        wx += icon + 3.0f * s;
    }
    if (!run_.wands.empty()) {
        const Wand& w = run_.wands[static_cast<size_t>(run_.activeWand)];
        float dx = wx + 8.0f * s;
        ui::text(w.name, dx, wy - static_cast<float>(small) - 2.0f * s, small, ui::color::kText);
        const float slot = 11.0f * s;
        int nextSlot = w.deckPos;
        for (int i = 0; i < w.capacity; ++i) {
            Rectangle r{dx + static_cast<float>(i) * (slot + 2.0f * s), wy + (icon - slot) * 0.5f, slot, slot};
            SpellId spell = w.order.empty() ? w.slots[static_cast<size_t>(i)] : w.slots[static_cast<size_t>(i)];
            ui::spellIcon(spell, r, i == nextSlot && !w.shuffle);
        }
    }

    // Flask.
    {
        const Flask& f = run_.flask;
        float fw = 12.0f * s, fh = 18.0f * s;
        Rectangle r{sw - fw - 12.0f * s, sh - fh - 10.0f * s, fw, fh};
        DrawRectangleRec(r, Color{20, 24, 34, 220});
        if (!f.empty()) {
            Rgba c = cellColor(makeCell(f.mat, 128), 0, 0, 0);
            float frac = static_cast<float>(f.amount) / Flask::kCapacity;
            DrawRectangleRec({r.x, r.y + r.height * (1.0f - frac), r.width, r.height * frac}, Color{c.r, c.g, c.b, 255});
        }
        DrawRectangleLinesEx(r, s, Color{190, 220, 240, 200});
        DrawRectangleRec({r.x + r.width * 0.3f, r.y - 3.0f * s, r.width * 0.4f, 3.0f * s}, Color{120, 80, 50, 255});
        std::string label = f.empty() ? "Empty flask" : std::string(matDef(f.mat).name) + TextFormat(" %d%%", f.amount * 100 / Flask::kCapacity);
        ui::text(label, r.x - 4.0f * s, r.y + r.height - static_cast<float>(small), small, ui::color::kTextDim, ui::Align::Right);
        ui::text("RMB pour  Q absorb  F drink", r.x - 4.0f * s, r.y + r.height - static_cast<float>(small) * 2.2f, small,
                 Color{120, 112, 104, 200}, ui::Align::Right);
    }

    // Boss health.
    if (const Enemy* boss = level_->boss(); boss && boss->aggro()) {
        float bw = sw * 0.5f;
        Rectangle r{(sw - bw) * 0.5f, 14.0f * s, bw, 6.0f * s};
        ui::text(boss->def().name, sw * 0.5f, r.y - static_cast<float>(small) - 2.0f, small, Color{255, 220, 140, 255}, ui::Align::Center);
        ui::bar(r, boss->hp / boss->maxHp, Color{220, 160, 40, 255});
    }
}

void GameScene::drawTitleCard() {
    if (titleTimer_ <= 0.0f) return;
    float a = std::min(1.0f, titleTimer_ / 1.0f) * std::min(1.0f, (4.0f - titleTimer_) * 2.0f);
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const int s = ui::scale();
    unsigned char alpha = static_cast<unsigned char>(255.0f * clamp(a, 0.0f, 1.0f));
    ui::text(titleMain_, sw * 0.5f, sh * 0.22f, ui::fontSize(s * 3), Color{240, 226, 200, alpha}, ui::Align::Center);
    ui::text(titleSub_, sw * 0.5f, sh * 0.22f + static_cast<float>(ui::fontSize(s * 3)) + 6.0f * s, ui::fontSize(s + 1),
             Color{190, 170, 220, alpha}, ui::Align::Center);
}

void GameScene::drawControlsHint() {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::fontSize(std::max(1, ui::scale() - 1));
    float t = level_->time();
    float a = clamp(std::min((t - 1.0f) * 2.0f, (30.0f - t) / 2.0f), 0.0f, 1.0f);
    if (a <= 0.0f) return;
    static constexpr const char* kKeyboard[] = {"A/D move   W/Space jump - hold to levitate", "Mouse aim   LMB cast   1-4 / wheel switch wand",
                                                "Wand 2 digs through rock   Tab edit wands", "E interact   Esc pause   Follow the arrow to the portal"};
    static constexpr const char* kGamepad[] = {"Left stick move   A jump - hold to levitate", "Right stick aim   RT cast   LB / RB switch wand",
                                               "Wand 2 digs through rock   Back edit wands", "B interact   Start pause   Follow the arrow to the portal"};
    const auto& lines = usingGamepad_ ? kGamepad : kKeyboard;
    float x = 10.0f * s, y = 60.0f * s;
    Rectangle r{x - 4.0f * s, y - 4.0f * s, 170.0f * s, (static_cast<float>(small) * 1.3f) * 4.0f + 8.0f * s};
    unsigned char al = static_cast<unsigned char>(200.0f * a);
    DrawRectangleRec(r, Color{10, 8, 16, static_cast<unsigned char>(160.0f * a)});
    for (const char* l : lines) {
        ui::text(l, x, y, small, Color{220, 210, 200, al});
        y += static_cast<float>(small) * 1.3f;
    }
}

void GameScene::drawInventory() {
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale());
    const int small = ui::fontSize(std::max(1, ui::scale() - 1));
    DrawRectangle(0, 0, app_.screenWidth(), app_.screenHeight(), Color{0, 0, 0, 160});

    const float slot = 16.0f * s, gap = 3.0f * s;
    Rectangle panelR{sw * 0.5f - 230.0f * s, sh * 0.5f - 150.0f * s, 460.0f * s, 300.0f * s};
    ui::panel(panelR);
    ui::text("SATCHEL OF SPELLS", panelR.x + panelR.width * 0.5f, panelR.y + 6.0f * s, size, ui::color::kGold, ui::Align::Center);
    ui::text("Drag spells between wands and satchel. Right-click removes. Tab closes.", panelR.x + panelR.width * 0.5f,
             panelR.y + 8.0f * s + static_cast<float>(size), small, ui::color::kTextDim, ui::Align::Center);

    struct Hover {
        SpellId spell = SpellId::None;
        int wand = -2;
        int slot = -1;
    } hover;
    Vector2 mouse = GetMousePosition();
    std::string wandTip, wandTipTitle;

    float y = panelR.y + 32.0f * s;
    for (size_t wi = 0; wi < run_.wands.size(); ++wi) {
        Wand& w = run_.wands[wi];
        float x = panelR.x + 10.0f * s;
        Rectangle nameR{x, y, 110.0f * s, slot};
        ui::text(w.name, x, y, small, static_cast<int>(wi) == run_.activeWand ? ui::color::kText : ui::color::kTextDim);
        ui::text(TextFormat("delay %.2fs  recharge %.2fs", w.castDelay, w.rechargeTime), x, y + static_cast<float>(small) + 1.0f * s,
                 small, Color{140, 130, 150, 255});
        ui::text(TextFormat("mana %d (+%d/s)%s%s", static_cast<int>(w.manaMax), static_cast<int>(w.manaCharge), w.shuffle ? "  shuffle" : "",
                            w.spellsPerCast > 1 ? "  multicast" : ""),
                 x, y + (static_cast<float>(small) + 1.0f * s) * 2.0f, small, Color{140, 130, 150, 255});
        if (ui::hovered({nameR.x, nameR.y, nameR.width, slot * 1.6f})) {
            wandTipTitle = w.name;
            wandTip = TextFormat("Spread %.1f deg. Spells per cast %d. Capacity %d.%s", w.spreadDeg, w.spellsPerCast, w.capacity,
                                 w.shuffle ? " Shuffle: casts spells in random order." : "");
        }
        float sx = panelR.x + 130.0f * s;
        for (int i = 0; i < w.capacity; ++i) {
            Rectangle r{sx + static_cast<float>(i) * (slot + gap), y, slot, slot};
            SpellId sp = w.slots[static_cast<size_t>(i)];
            bool isDragSrc = drag_.active && drag_.wand == static_cast<int>(wi) && drag_.slot == i;
            ui::spellIcon(isDragSrc ? SpellId::None : sp, r, CheckCollisionPointRec(mouse, r));
            if (CheckCollisionPointRec(mouse, r)) hover = {sp, static_cast<int>(wi), i};
        }
        y += slot * 1.9f + 4.0f * s;
    }

    // Satchel grid.
    y = std::max(y, panelR.y + 190.0f * s);
    ui::text("Satchel", panelR.x + 10.0f * s, y - static_cast<float>(small) - 2.0f * s, small, ui::color::kTextDim);
    for (int i = 0; i < RunState::kInventorySize; ++i) {
        int col = i % 8, row = i / 8;
        Rectangle r{panelR.x + 10.0f * s + static_cast<float>(col) * (slot + gap), y + static_cast<float>(row) * (slot + gap), slot, slot};
        SpellId sp = run_.inventory[static_cast<size_t>(i)];
        bool isDragSrc = drag_.active && drag_.wand == -1 && drag_.slot == i;
        ui::spellIcon(isDragSrc ? SpellId::None : sp, r, CheckCollisionPointRec(mouse, r));
        if (CheckCollisionPointRec(mouse, r)) hover = {sp, -1, i};
    }

    // Perks and flask summary.
    float px = panelR.x + 10.0f * s + 8.0f * (slot + gap) + 10.0f * s;
    ui::text("Perks", px, y - static_cast<float>(small) - 2.0f * s, small, ui::color::kTextDim);
    float py = y;
    for (int p = 0; p < kPerkCount; ++p) {
        int n = run_.perkCount(static_cast<PerkId>(p));
        if (n == 0) continue;
        const PerkDef& d = perkDef(static_cast<PerkId>(p));
        std::string label = n > 1 ? std::string(d.name) + " x" + std::to_string(n) : d.name;
        ui::text(label, px, py, small, hexColor(d.color));
        if (ui::hovered({px, py, 100.0f * s, static_cast<float>(small)})) {
            wandTipTitle = d.name;
            wandTip = d.description;
        }
        py += static_cast<float>(small) * 1.2f;
        if (py > panelR.y + panelR.height - 14.0f * s) break;
    }
    ui::text(TextFormat("Kills %d   Gold %d   Time %s", run_.stats.kills, run_.gold, formatTime(run_.stats.time).c_str()),
             panelR.x + panelR.width - 10.0f * s, panelR.y + panelR.height - static_cast<float>(small) - 6.0f * s, small,
             ui::color::kTextDim, ui::Align::Right);

    // Drag & drop.
    auto spellAt = [&](int wand, int slotIndex) -> SpellId& {
        return wand < 0 ? run_.inventory[static_cast<size_t>(slotIndex)] : run_.wands[static_cast<size_t>(wand)].slots[static_cast<size_t>(slotIndex)];
    };
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hover.wand != -2 && hover.spell != SpellId::None) {
        drag_ = {true, hover.spell, hover.wand, hover.slot};
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && hover.wand >= 0 && hover.spell != SpellId::None) {
        if (run_.addSpell(hover.spell)) spellAt(hover.wand, hover.slot) = SpellId::None;
        app_.audio().play(Sfx::UiClick, 0.6f);
    }
    if (drag_.active) {
        Rectangle r{mouse.x - slot * 0.5f, mouse.y - slot * 0.5f, slot, slot};
        ui::spellIcon(drag_.spell, r, true);
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (hover.wand != -2) {
                SpellId& src = spellAt(drag_.wand, drag_.slot);
                SpellId& dst = spellAt(hover.wand, hover.slot);
                std::swap(src, dst);
                app_.audio().play(Sfx::UiClick, 0.7f);
            }
            drag_ = Drag{};
        }
    } else if (hover.spell != SpellId::None) {
        ui::tooltip(spellDef(hover.spell).name, spellDetails(hover.spell), ui::spellTypeColor(hover.spell));
    } else if (!wandTip.empty()) {
        ui::tooltip(wandTipTitle, wandTip, ui::color::kGold);
    }
}

void GameScene::drawPauseMenu() {
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const float s = static_cast<float>(ui::scale());
    DrawRectangle(0, 0, app_.screenWidth(), app_.screenHeight(), Color{0, 0, 0, 150});
    Rectangle panelR{sw * 0.5f - 80.0f * s, sh * 0.5f - 75.0f * s, 160.0f * s, 150.0f * s};
    ui::panel(panelR);
    ui::text("PAUSED", sw * 0.5f, panelR.y + 8.0f * s, ui::fontSize(ui::scale() + 1), ui::color::kGold, ui::Align::Center);
    float bw = 130.0f * s, bh = 18.0f * s, x = sw * 0.5f - bw * 0.5f, y = panelR.y + 30.0f * s;
    if (ui::button({x, y, bw, bh}, "Resume")) mode_ = Mode::Playing;
    y += bh + 5.0f * s;
    if (ui::button({x, y, bw, bh}, "Settings")) mode_ = Mode::Settings;
    y += bh + 5.0f * s;
    if (ui::button({x, y, bw, bh}, "Abandon Run")) {
        recordProfile(false);
        app_.setScene(std::make_unique<MenuScene>(app_));
    }
    y += bh + 5.0f * s;
    if (ui::button({x, y, bw, bh}, "Quit Game")) {
        recordProfile(false);
        app_.requestQuit();
    }
}

void GameScene::drawEndScreen(bool victory) {
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const float s = static_cast<float>(ui::scale());
    float a = clamp((endTimer_ - (victory ? 0.5f : 1.6f)) * 1.5f, 0.0f, 1.0f);
    DrawRectangle(0, 0, app_.screenWidth(), app_.screenHeight(), Color{0, 0, 0, static_cast<unsigned char>(170.0f * a)});
    if (a <= 0.0f) return;
    unsigned char al = static_cast<unsigned char>(255.0f * a);
    const char* title = victory ? "MAGNUM OPUS" : "THE WORK IS UNDONE";
    const char* sub = victory ? "The Prima Materia is perfected. You have become the Stone."
                              : "Your matter returns to the prima materia...";
    ui::text(title, sw * 0.5f, sh * 0.2f, ui::fontSize(ui::scale() * 3), victory ? Color{255, 220, 120, al} : Color{230, 80, 80, al},
             ui::Align::Center);
    ui::text(sub, sw * 0.5f, sh * 0.2f + static_cast<float>(ui::fontSize(ui::scale() * 3)) + 8.0f * s, ui::fontSize(ui::scale()),
             Color{210, 200, 220, al}, ui::Align::Center);

    const RunStats& st = run_.stats;
    std::string reached = run_.levelIndex <= 4 ? biomeDef(biomeForDepth(run_.levelIndex)).name : "The Athanor";
    std::string lines[] = {
        std::string("Reached: ") + (run_.inSanctum ? "Sanctum after " : "") + reached,
        "Time: " + formatTime(st.time),
        "Creatures slain: " + std::to_string(st.kills),
        "Gold gathered: " + std::to_string(st.goldCollected),
        "Spells cast: " + std::to_string(st.spellsCast),
        "Chests opened: " + std::to_string(st.chestsOpened),
        TextFormat("Seed: %u%s", run_.seed, run_.daily ? " (Daily Rite)" : ""),
    };
    float y = sh * 0.38f;
    for (const std::string& l : lines) {
        ui::text(l, sw * 0.5f, y, ui::fontSize(ui::scale()), Color{220, 210, 200, al}, ui::Align::Center);
        y += static_cast<float>(ui::fontSize(ui::scale())) * 1.35f;
    }
    if (a < 1.0f) return;
    float bw = 120.0f * s, bh = 18.0f * s;
    y += 10.0f * s;
    if (ui::button({sw * 0.5f - bw - 4.0f * s, y, bw, bh}, victory ? "New Work" : "Try Again") || IsKeyPressed(KEY_ENTER)) {
        RunState fresh = newRun(static_cast<uint32_t>(GetRandomValue(1, 0x7FFFFFFF)), run_.difficulty, false, run_.loadout);
        app_.setScene(std::make_unique<GameScene>(app_, std::move(fresh)));
    }
    if (ui::button({sw * 0.5f + 4.0f * s, y, bw, bh}, "Main Menu") || IsKeyPressed(KEY_ESCAPE)) {
        app_.setScene(std::make_unique<MenuScene>(app_));
    }
}

void GameScene::drawDebugOverlay() {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::fontSize(std::max(1, ui::scale() - 1));
    float x = 10.0f * s, y = static_cast<float>(app_.screenHeight()) * 0.45f;
    ui::text(TextFormat("%d FPS", GetFPS()), x, y, small, GREEN);
    y += static_cast<float>(small) * 1.3f;
    if (!debugOverlay_) return;
    const Profiler& prof = Profiler::instance();
    for (int i = 0; i < static_cast<int>(ProfileSection::Count); ++i) {
        auto sec = static_cast<ProfileSection>(i);
        ui::text(TextFormat("%-12s %5.2f ms (peak %5.2f)", Profiler::name(sec), prof.average(sec), prof.peak(sec)), x, y, small, GREEN);
        y += static_cast<float>(small) * 1.2f;
    }
    if (level_) {
        ui::text(TextFormat("chunks %d  particles %zu  projectiles %zu  enemies %zu", level_->world().activeChunkCount(),
                            level_->particles().count(), level_->projectiles().size(), level_->enemies().size()),
                 x, y, small, GREEN);
    }
}

void GameScene::drawLoading() {
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const int s = ui::scale();
    std::string text = run_.inSanctum ? "Entering the sanctum" : "Descending";
    int dots = static_cast<int>(time_ * 3.0f) % 4;
    ui::text(text + std::string(static_cast<size_t>(dots), '.'), sw * 0.5f, sh * 0.5f, ui::fontSize(s + 1), ui::color::kTextDim,
             ui::Align::Center);
}

void GameScene::recordProfile(bool victory) {
    if (profileRecorded_ || app_.options().automated()) return;
    profileRecorded_ = true;
    Profile& p = app_.profile();
    p.runs++;
    p.deepest = std::max(p.deepest, run_.levelIndex + 1);
    p.totalKills += run_.stats.kills;
    p.totalGold += run_.stats.goldCollected;
    if (victory) {
        p.victories++;
        if (p.bestTime <= 0.0f || run_.stats.time < p.bestTime) p.bestTime = run_.stats.time;
    }
    if (run_.daily) p.bestDailyDepth = std::max(p.bestDailyDepth, run_.levelIndex + 1);
    p.chestsOpened += run_.stats.chestsOpened;
    // Grimoire discoveries.
    p.spellsSeen |= run_.spellsSeen;
    for (const Wand& w : run_.wands)
        for (SpellId s : w.slots)
            if (s != SpellId::None) p.spellsSeen |= 1ull << static_cast<unsigned>(s);
    for (size_t i = 0; i < run_.perks.size(); ++i)
        if (run_.perks[i] > 0) p.perksSeen |= 1u << i;
    for (size_t i = 0; i < p.creatureKills.size(); ++i) p.creatureKills[i] += run_.killsByType[i];
    saveProfile(p);
}

} // namespace pm
