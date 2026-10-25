#include "scenes/GameScene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/App.hpp"
#include "app/InputLatch.hpp"
#include "app/RunRecords.hpp"
#include "core/Profiler.hpp"
#include "core/Timing.hpp"
#include "gen/LevelGenerator.hpp"
#include "render/Sprites.hpp"
#include "scenes/MenuScene.hpp"
#include "ui/SettingsPanel.hpp"
#include "ui/Ui.hpp"

namespace pm {

GameScene::GameScene(App& app, RunState run)
    : Scene(app), run_(std::move(run)), levelRenderer_(scene_, app.sprites(), app.pool()), hints_(app.hintsShown()) {
    requestLevel();
}

GameScene::~GameScene() {
    if (pending_.valid()) pending_.wait();
    // Closing the window mid-run leaves the suspended-run save for "Continue the Work".
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
    // A drag begun during the exit fade must not carry wand/slot indices into the next stage.
    drag_ = Drag{};
    for (Wand& w : run_.wands) w.resetDeck();
    loadTimer_ = 0.0f;
    loadSkipped_ = false;
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
    minimap_.reset(*level_);
    mode_ = Mode::Playing;
    if (app_.options().teleportX >= 0.0f) level_->debugTeleport({app_.options().teleportX, app_.options().teleportY});
    if (app_.options().openUi == "vault" && !level_->layout().vaults.empty()) {
        const Rectf& v = level_->layout().vaults.front();
        level_->debugTeleport({v.x + v.w * 0.5f, v.y + v.h - 12.0f});
    }
    if (app_.options().openUi == "transmuter" && !level_->transmuters().empty()) {
        level_->debugTeleport(level_->transmuters().front().pos + Vec2{-30.0f, -8.0f});
        camera_.reset(level_->player().body.pos);
    }
    if (app_.options().openUi == "map") {
        minimap_.revealAll(*level_);
        mode_ = Mode::Map;
    }
    if (app_.options().openUi == "inventory") mode_ = Mode::Inventory;
    else if (app_.options().openUi == "pause") mode_ = Mode::Paused;
    exiting_ = false;
    titleTimer_ = 4.0f;
    titleOmen_.clear();
    if (run_.inSanctum) {
        // Sanctums restore the alchemist completely.
        run_.hp = run_.maxHp;
        level_->player().hp = run_.maxHp;
        titleMain_ = "SANCTUM";
        titleSub_ = biomeDef(BiomeId::Sanctum).subtitle;
    } else {
        const BiomeDef& b = biomeDef(level_->biome());
        titleMain_ = std::string(stageNumeral(run_.levelIndex)) + "  " + b.name;
        titleSub_ = b.subtitle;
        if (const Omen omen = level_->layout().omen; omen != Omen::None) {
            titleOmen_ = std::string("Omen: ") + omenDef(omen).name + ". " + omenDef(omen).description;
            app_.audio().play(Sfx::Perk, 0.45f, 0.6f); // a low chime marks the omen
            if (app_.options().autoplay) logAutoplay(TextFormat("stage %d omen %s", run_.levelIndex, omenDef(omen).name));
        }
    }
    run_.stats.deepest = std::max(run_.stats.deepest, run_.levelIndex + 1);
    // Every stage start is a resume point.
    if (!app_.options().automated()) saveSuspendedRun(run_);
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
    // Save the cleared stage's gains straight away: closing the game during the fade or the
    // loading screen then resumes at the next stage, as promised.
    if (!app_.options().automated()) saveSuspendedRun(run_);
    requestLevel();
}

void GameScene::latchInput() {
    if (input::keyPressed(KEY_SPACE) || input::keyPressed(KEY_W) || input::keyPressed(KEY_UP) ||
        IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
        latchJump_ = true;
    if (input::keyPressed(KEY_E) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) latchInteract_ = true;
    if (input::keyPressed(KEY_F) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) latchDrink_ = true;
    if (input::mousePressed(MOUSE_BUTTON_LEFT)) latchCast_ = true; // a tap shorter than a frame still casts
    for (int i = 0; i < RunState::kMaxWands; ++i)
        if (input::keyPressed(KEY_ONE + i)) latchWand_ = i;
    float wheel = GetMouseWheelMove();
    if (wheel > 0.0f) latchCycle_ -= 1;
    if (wheel < 0.0f) latchCycle_ += 1;
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) latchCycle_ -= 1;
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) latchCycle_ += 1;

    // Button prompts follow whichever device was used last.
    const Vector2 mouseDelta = GetMouseDelta();
    if (input::anyKeyPressed() || input::mousePressed(MOUSE_BUTTON_LEFT) || mouseDelta.x * mouseDelta.x + mouseDelta.y * mouseDelta.y > 4.0f) {
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
    // A click that closed a menu (Resume, Tab mid-drag) must not keep casting once play resumes.
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) castBlocked_ = false;
    in.castHeld = (!castBlocked_ && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) || latchCast_;
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
    latchJump_ = latchInteract_ = latchDrink_ = latchCast_ = false;
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
    // Through the portal the stage holds still while the screen fades, so nothing can harm the
    // alchemist after they have reached the exit.
    if (exiting_) return;
    const bool controllable = mode_ == Mode::Playing;
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
    minimap_.update(*level_);
    if (mode_ == Mode::Playing && !autoplay && app_.settings().hints) hints_.update(*level_, run_, kTickDt);

    switch (level_->pollEvent()) {
    case LevelEvent::ExitReached:
        exiting_ = true;
        if (autoplay) logAutoplay(TextFormat("stage %d%s cleared at %.1fs", run_.levelIndex, run_.inSanctum ? " (sanctum)" : "", run_.stats.time));
        break;
    case LevelEvent::PlayerDied:
        if (autoplay)
            logAutoplay(TextFormat("died on stage %d at %.1fs, kills %d - %s", run_.levelIndex, run_.stats.time, run_.stats.kills,
                                   deathCause(level_->player()).c_str()));
        mode_ = Mode::Dead;
        exiting_ = false;
        endTimer_ = 0.0f;
        slowMo_ = 0.35f;
        recordProfile(false);
        break;
    default: break;
    }
}

void GameScene::onSuspend() {
    // Minimising skips frames entirely, so the focus check never sees it: pause here instead.
    if (mode_ == Mode::Playing && !app_.options().automated()) mode_ = Mode::Paused;
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
        // The loading screen lingers long enough to read its motto, unless the player presses
        // on (bot runs skip ahead).
        constexpr float kMinLoadSeconds = 1.6f;
        fade_ = 1.0f;
        loadTimer_ += dt;
        if (input::anyKeyPressed() || input::mousePressed(MOUSE_BUTTON_LEFT) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
            loadSkipped_ = true;
        const bool lingered = loadTimer_ >= kMinLoadSeconds || loadSkipped_ || app_.options().autoplay;
        if (lingered && pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            finishLoading();
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

    if (input::keyPressed(KEY_F3)) debugOverlay_ = !debugOverlay_;
    // A heartbeat quickens as the alchemist nears death.
    if (mode_ == Mode::Playing && level_->player().alive() && level_->player().hp < level_->player().maxHp * 0.25f) {
        heartbeat_ -= dt;
        if (heartbeat_ <= 0.0f) {
            app_.audio().play(Sfx::Heartbeat, 0.8f);
            heartbeat_ = 0.55f + 0.6f * level_->player().hp / (level_->player().maxHp * 0.25f);
        }
    } else {
        heartbeat_ = 0.0f;
    }
    if (mode_ != Mode::Playing) castBlocked_ = true;
    // Alt-tabbing away pauses the run.
    if (mode_ == Mode::Playing && !IsWindowFocused() && !app_.options().automated()) mode_ = Mode::Paused;
    if (mode_ == Mode::Playing) {
        latchInput();
        if (input::keyPressed(KEY_ESCAPE) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) mode_ = Mode::Paused;
        else if (input::keyPressed(KEY_TAB) || input::keyPressed(KEY_I) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_LEFT))
            mode_ = Mode::Inventory;
        else if (!level_->isSanctum() && (input::keyPressed(KEY_M) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)))
            mode_ = Mode::Map;
    } else if (mode_ == Mode::Map) {
        if (input::keyPressed(KEY_M) || input::keyPressed(KEY_ESCAPE) || input::keyPressed(KEY_TAB) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT))
            mode_ = Mode::Playing;
    } else if (mode_ == Mode::Inventory) {
        if (input::keyPressed(KEY_TAB) || input::keyPressed(KEY_I) || input::keyPressed(KEY_ESCAPE) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_LEFT)) {
            mode_ = Mode::Playing;
            drag_ = Drag{};
            for (Wand& w : run_.wands) w.resetDeck();
        }
    } else if (mode_ == Mode::Paused) {
        if (ui::backPressed() || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) mode_ = Mode::Playing;
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
        // Interpolate across the level's own step: in death slow-motion a level step spans
        // several app ticks, and during hit-stop the level holds still.
        float a = 1.0f;
        if ((mode_ == Mode::Playing || mode_ == Mode::Dead) && !level_->hitStopActive())
            a = std::min(1.0f, tickAccumulator_ + alpha * slowMo_);
        Vec2 cam = camera_.position(level_->shake(), time_, app_.settings().screenShake);
        levelRenderer_.setHighQuality(app_.settings().highQuality);
        levelRenderer_.render(*level_, cam, a, time_, app_.screenWidth(), app_.screenHeight());
        if (mode_ != Mode::Dead && mode_ != Mode::Victory) drawWorldOverlays(a);
        if (mode_ == Mode::Playing) drawCompass(a);
        if (mode_ != Mode::Victory) drawHud();
        if (mode_ == Mode::Playing && app_.settings().hints) hints_.draw(static_cast<float>(app_.screenWidth()), static_cast<float>(app_.screenHeight()), usingGamepad_);
        drawTitleCard();
        if (app_.settings().hints && !run_.inSanctum && run_.levelIndex == 0 && level_->time() < 30.0f) drawControlsHint();
    }

    switch (mode_) {
    case Mode::Inventory: drawInventory(); break;
    case Mode::Map: minimap_.drawFull(*level_, static_cast<float>(app_.screenWidth()), static_cast<float>(app_.screenHeight()), time_); break;
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

void GameScene::recordProfile(bool victory) {
    if (profileRecorded_ || app_.options().automated()) return;
    profileRecorded_ = true;
    std::string fate = "Completed the Great Work";
    if (!victory) fate = level_ && !level_->player().alive() ? deathCause(level_->player()) : "Abandoned the Work";
    if (fate.empty()) fate = "Perished";
    const RunRecord record = recordRun(app_.profile(), run_, victory, fate);
    discoveries_ = record.discoveries;
    unlockedPaths_ = record.unlockedPaths;
    discardSuspendedRun();
}

} // namespace pm
