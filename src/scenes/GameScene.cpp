#include "scenes/GameScene.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <utility>

#include "app/App.hpp"
#include "app/InputLatch.hpp"
#include "app/RunRecords.hpp"
#include "core/Profiler.hpp"
#include "core/Timing.hpp"
#include "gen/LevelGenerator.hpp"
#include "ui/HelpPanel.hpp"
#include "ui/SettingsPanel.hpp"
#include "ui/Ui.hpp"

namespace pm {

namespace {

bool g_deathPreviewShown = false; ///< --ui death fells only the first run of the session
uint32_t g_previewFeats = 0;      ///< feats announced in a preview session, kept out of the profile
bool g_qualityStepped = false;    ///< graphics already lowered once this session for a slow GPU

void logAutoplay(const char* message) {
    std::printf("[autoplay] %s\n", message);
    std::fflush(stdout);
}

} // namespace

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
    eventNotice_ = {}; // a notice raised just before the portal stays with its stage
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
    scene_.resetLights();
    bot_ = Bot{};
    hints_.reset();
    minimap_.reset(*level_);
    guide_.reset(level_->layout().playerStart, level_->layout().mainPath, level_->exitPos());
    satchelCounted_ = 0; // each level counts its own satchel pickups
    mode_ = Mode::Playing;
    applyLaunchOptions();
    guide_.update(level_->player().body.pos);
    exiting_ = false;
    titleTimer_ = kTitleCardSeconds;
    titleTop_ = -1.0f;
    titleOmen_.clear();
    announcedGuardian_ = 0;
    stageDamageStart_ = run_.stats.damageTaken;
    if (run_.levelIndex >= 3 && !run_.inSanctum) earnFeat(FeatId::DeepDelver);
    if (run_.inSanctum) {
        // Sanctums restore the alchemist completely (under Lean Sanctums, half of what was lost).
        run_.hp = run_.under(Ordeal::LeanSanctums) ? run_.hp + (run_.maxHp - run_.hp) * 0.5f : run_.maxHp;
        level_->player().hp = run_.hp;
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
    if (!app_.options().previewOnly()) saveSuspendedRun(run_);
}

void GameScene::applyLaunchOptions() {
    if (app_.options().teleportX >= 0.0f) level_->debugTeleport({app_.options().teleportX, app_.options().teleportY});
    if (app_.options().openUi == "vault" && !level_->layout().vaults.empty()) {
        const Rectf& v = level_->layout().vaults.front();
        level_->debugTeleport({v.x + v.w * 0.5f, v.y + v.h - 12.0f});
    }
    if (app_.options().openUi == "transmuter" && !level_->transmuters().empty()) {
        level_->debugTeleport(level_->transmuters().front().pos + Vec2{-30.0f, -8.0f});
        camera_.reset(level_->player().body.pos);
    }
    if (app_.options().openUi == "shop") {
        // In front of the first spell for sale, its prompt open.
        const auto& wares = level_->pickups();
        const auto it = std::find_if(wares.begin(), wares.end(), [](const Pickup& p) { return p.price > 0 && p.type == PickupType::Spell; });
        if (it != wares.end()) {
            level_->debugTeleport(it->body.pos + Vec2{-6.0f, 0.0f});
            camera_.reset(level_->player().body.pos);
        }
    }
    if (app_.options().openUi == "map") {
        minimap_.revealAll(*level_);
        mode_ = Mode::Map;
    }
    if (app_.options().openUi == "inventory") mode_ = Mode::Inventory;
    else if (app_.options().openUi == "pause") mode_ = Mode::Paused;
    else if (app_.options().openUi == "help") mode_ = Mode::Help;
    else if (app_.options().openUi == "elixir") level_->player().status.hasted = level_->player().status.fury = 60.0f;
    else if (app_.options().openUi == "death" && !std::exchange(g_deathPreviewShown, true)) // the end screen, once
        level_->player().applyDamage(*level_, {1.0e6f, DamageType::Fire, {}, Team::Monster, false, true, enemyDef(EnemyType::Imp).name});
}

void GameScene::advanceStage() {
    if (run_.inSanctum) {
        run_.inSanctum = false;
        run_.levelIndex++;
    } else if (run_.levelIndex >= kFinalStage) {
        mode_ = Mode::Victory;
        endTimer_ = 0.0f;
        endInputArmed_ = false;
        titleTimer_ = 0.0f;
        recordProfile(true);
        return;
    } else {
        run_.inSanctum = true;
    }
    // Save the cleared stage's gains straight away: closing the game during the fade or the
    // loading screen then resumes at the next stage, as promised.
    if (!app_.options().previewOnly()) saveSuspendedRun(run_);
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
}

void GameScene::noteInputDevice() {
    // Button prompts follow whichever device was used last.
    usingGamepad_ = input::usingGamepad(usingGamepad_);
}

void GameScene::updateLens(float dt) {
    const Vector2 m = GetMousePosition();
    const Vec2 at = scene_.screenToWorld(m);
    // Resting means the cursor stays on one spot of the world: moving the mouse, walking or
    // the camera drifting all restart it.
    const float dx = m.x - lens_.mouse.x, dy = m.y - lens_.mouse.y;
    if (dx * dx + dy * dy > 9.0f || (at - lens_.anchor).lengthSq() > 4.0f * 4.0f) {
        lens_.rest = 0.0f;
        lens_.anchor = at;
    } else {
        lens_.rest += dt;
    }
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) lens_.rest = 0.0f; // casting is not looking
    lens_.mouse = m;
    std::string title, body;
    Color color = WHITE;
    // The cursor must rest a moment (or the flask be drawing), never while casting, and not
    // while the screen fades through a portal (the label would float over the black).
    const bool looking = lens_.rest >= 0.4f || IsKeyDown(KEY_Q);
    bool eligible = false;
    if (mode_ == Mode::Playing && !exiting_ && fade_ < 0.3f && app_.settings().matterLens && !usingGamepad_ && looking &&
        !IsMouseButtonDown(MOUSE_BUTTON_LEFT) && (at - level_->player().body.pos).lengthSq() < 120.0f * 120.0f) {
        eligible = true;
        bool fighting = false;
        for (const auto& e : level_->enemies()) {
            const Rectf b = e->bounds();
            if (!e->alive() || e->disguised() || at.x < b.x - 2.0f || at.x > b.x + b.w + 2.0f || at.y < b.y - 2.0f || at.y > b.y + b.h + 2.0f) continue;
            // In a fight the lens keeps quiet: the boss bar and the creature's own reactions say enough.
            if ((e->aggro() && e->team == Team::Monster) || e.get() == level_->bossBarTarget()) {
                fighting = true;
                break;
            }
            title = e->elite() ? std::string(affixName(e->affix())) + " " + e->def().name : std::string(e->def().name);
            body = TextFormat("%d / %d health", static_cast<int>(std::ceil(e->hp)), static_cast<int>(e->maxHp));
            color = e->team == Team::Player ? ui::color::kAccent : ui::color::kDanger;
            break;
        }
        if (title.empty() && !fighting) {
            const MatId mt = level_->world().matAt(floorToInt(at.x), floorToInt(at.y));
            body = matDescription(mt);
            if (!body.empty()) {
                title = matDef(mt).name;
                const Rgba c = materialColor(mt);
                color = ColorBrightness(Color{c.r, c.g, c.b, 255}, 0.35f);
            }
        }
    }
    if (!title.empty()) {
        lens_.same = title == lens_.title ? lens_.same + dt : 0.0f;
        lens_.title = std::move(title);
        lens_.body = std::move(body);
        lens_.color = color;
        lens_.hold = 0.3f;
    } else if (eligible && !lens_.title.empty() && (lens_.hold -= dt) > 0.0f) {
        lens_.same += dt; // fire and gas flicker cell by cell: keep the label a moment
    } else {
        lens_.title.clear();
        lens_.same = 0.0f;
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
        if (rx * rx + ry * ry > 0.1f) {
            padAim_ = Vec2{rx, ry}.normalized();
            padAimSet_ = true;
        } else if (!padAimSet_ && std::fabs(in.moveX) > 0.25f) {
            padAim_ = {in.moveX > 0.0f ? 1.0f : -1.0f, 0.0f}; // until the right stick is used, aim where you walk
        }
        // On a pad the aim keeps the stick's last direction instead of snapping to the idle mouse pointer.
        if (usingGamepad_ && level_) in.aim = level_->player().body.pos + padAim_ * 80.0f;
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
        botAim_ = in.aim;
    } else {
        in.aim = level_->player().body.pos + level_->player().aimDir();
    }
    if (app_.options().god) level_->player().hp = level_->player().maxHp;
    if (autoplay && bot_.hopelesslyStuck()) {
        level_->debugWarpAhead();
        bot_.clearStuck();
    }
    level_->tick(in);
    // Feats count only from ticks the alchemist was alive for: nothing that burns out on the
    // death screen (the run is already recorded) is credited.
    for (FeatId f : level_->takeFeats())
        if (controllable) earnFeat(f);
    minimap_.update(*level_);
    guide_.update(level_->player().body.pos);
    if (mode_ == Mode::Playing && !autoplay && app_.settings().hints) hints_.update(*level_, run_, kTickDt);

    switch (level_->pollEvent()) {
    case LevelEvent::ExitReached:
        exiting_ = true;
        if (!run_.inSanctum && run_.stats.damageTaken <= stageDamageStart_) earnFeat(FeatId::Untouched);
        if (autoplay) logAutoplay(TextFormat("stage %d%s cleared at %.1fs", run_.levelIndex, run_.inSanctum ? " (sanctum)" : "", run_.stats.time));
        break;
    case LevelEvent::PlayerDied:
        if (autoplay)
            logAutoplay(TextFormat("died on stage %d at %.1fs, kills %d - %s", run_.levelIndex, run_.stats.time, run_.stats.kills,
                                   deathCause(level_->player()).c_str()));
        mode_ = Mode::Dead;
        exiting_ = false;
        endTimer_ = 0.0f;
        endInputArmed_ = false;
        titleTimer_ = 0.0f;
        eventNotice_ = {};
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
        updateFeats(dt); // the ending's own feats are announced over the end screen
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
    // Title cards and notices wait while a menu covers them, so a quick look at How to Play in
    // the first seconds of a stage does not cost the player the stage's name or its omen.
    if (mode_ == Mode::Playing) titleTimer_ = std::max(0.0f, titleTimer_ - dt);
    updateFeats(dt);
    // A guardian is announced with a title card the first time it notices the alchemist.
    if (mode_ == Mode::Playing) {
        if (const Enemy* g = level_->bossBarTarget(); g && g->guardian() && g->id != announcedGuardian_) {
            announcedGuardian_ = g->id;
            titleMain_ = g->def().name;
            titleSub_ = guardianEpithet(g->type());
            titleOmen_.clear();
            titleTimer_ = kTitleCardSeconds;
            titleTop_ = -1.0f;
        }
    }
    // A stage event announces itself under the title card's place.
    if (EventNotice n = level_->events().takeNotice(); !n.text.empty()) {
        eventNotice_ = std::move(n);
        eventNoticeTime_ = 0.0f;
    }
    if (mode_ == Mode::Playing) eventNoticeTime_ += dt;
    // Spells that went to the satchel wait on a HUD badge until the editor is opened.
    satchelNew_ += level_->spellsToSatchel() - satchelCounted_;
    satchelCounted_ = level_->spellsToSatchel();
    // Health bars keep a pale chip of what a hit took, draining after a moment.
    if (mode_ == Mode::Playing || mode_ == Mode::Dead) {
        const Player& pl = level_->player();
        hpTrail_.update(pl.hp / pl.maxHp, dt);
        if (const Enemy* boss = level_->bossBarTarget()) {
            if (boss->id != bossTrailId_) bossTrail_ = {};
            bossTrailId_ = boss->id;
            bossTrail_.update(boss->hp / boss->maxHp, dt);
        }
    }
    // The gold counter pops whenever gold comes in.
    goldPop_ = std::max(0.0f, goldPop_ - dt);
    if (shownGold_ >= 0 && run_.gold > shownGold_) goldPop_ = kGoldPopSeconds;
    shownGold_ = run_.gold;

    if (input::keyPressed(KEY_F3)) debugOverlay_ = !debugOverlay_;
    updateHeartbeat(dt);
    if (mode_ != Mode::Playing) castBlocked_ = true;
    noteInputDevice();
    updateModeInput(dt);

    // Camera and audio follow the alchemist.
    const ViewInfo& v = scene_.view();
    Vec2 viewSize{v.viewW > 0 ? v.viewW : 480.0f, v.viewH > 0 ? v.viewH : 270.0f};
    const Player& pl = level_->player();
    Vec2 aim = usingGamepad_ ? pl.body.pos + padAim_ * 80.0f : scene_.screenToWorld(GetMousePosition());
    if (mode_ != Mode::Playing) aim = pl.body.pos;
    camera_.kick(level_->player().takeRecoil());
    updateLens(dt);
    camera_.update(pl.body.pos, aim, dt, {static_cast<float>(level_->world().width()), static_cast<float>(level_->world().height())},
                   viewSize);
    const BiomeDef& b = biomeDef(level_->biome());
    audio.setMusicMood(b.musicRoot, b.musicMode, level_->combatIntensity());
}

void GameScene::updateHeartbeat(float dt) {
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
}

void GameScene::updateModeInput(float dt) {
    // Alt-tabbing away pauses the run.
    if (mode_ == Mode::Playing && !IsWindowFocused() && !app_.options().automated()) mode_ = Mode::Paused;
    if (mode_ == Mode::Playing) {
        // The pause menu opens unarmed, with no entry focused (it starts on Resume).
        confirmAbandon_ = confirmQuit_ = false;
        pauseNav_ = ui::NavList{};
        latchInput();
        if (input::keyPressed(KEY_ESCAPE) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) mode_ = Mode::Paused;
        else if (input::keyPressed(KEY_TAB) || input::keyPressed(KEY_I) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_LEFT)) {
            mode_ = Mode::Inventory;
            hints_.markSeen(Hint::EditWands); // found the editor; no need to point at it
            satchelNew_ = 0;
            // A stick still held from walking must not count as a flick of the slot cursor.
            syncStickFlick();
        }
        else if (!level_->isSanctum() && (input::keyPressed(KEY_M) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)))
            mode_ = Mode::Map;
    } else if (mode_ == Mode::Map) {
        if (input::keyPressed(KEY_M) || input::keyPressed(KEY_ESCAPE) || input::keyPressed(KEY_TAB) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT))
            mode_ = Mode::Playing;
    } else if (mode_ == Mode::Inventory) {
        firstCastFlash_ = std::max(0.0f, firstCastFlash_ - dt);
        // B puts back a spell picked up with the pad cursor, otherwise it closes the editor.
        const bool padBack = IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
        if (padBack && drag_.active) {
            drag_ = Drag{};
        } else if (padBack || input::keyPressed(KEY_TAB) || input::keyPressed(KEY_I) || input::keyPressed(KEY_ESCAPE) ||
                   IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_LEFT)) {
            mode_ = Mode::Playing;
            drag_ = Drag{};
            firstCastFlash_ = 0.0f;
            for (Wand& w : run_.wands) w.resetDeck();
        } else {
            updateEditorPad();
        }
    } else if (mode_ == Mode::Paused) {
        if (ui::backPressed() || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) mode_ = Mode::Playing;
    } else if (mode_ == Mode::Dead) {
        endTimer_ += dt;
    }
}

void GameScene::watchFrameRate() {
    // A GPU that cannot hold about 45 fps on High during play is stepped down to Low once, with a
    // note, so a weak laptop runs smoothly without the player hunting through the settings.
    constexpr float kSlowFrame = 1.0f / 45.0f, kPatience = 4.0f;
    Settings& st = app_.settings();
    if (g_qualityStepped || !st.highQuality || app_.options().automated() || mode_ != Mode::Playing || !level_ ||
        level_->time() < 3.0f || !IsWindowFocused()) {
        slowFor_ = 0.0f;
        return;
    }
    const float frame = GetFrameTime();
    // A stall (a dragged window, a hitch, a laptop waking) says nothing about the GPU, and when the
    // simulation itself is the slow part, fewer lights would not help.
    constexpr float kStall = 0.1f;
    constexpr double kSimBoundMs = 8.0;
    if (frame > kStall || Profiler::instance().average(ProfileSection::Tick) > kSimBoundMs) return;
    frameAvg_ = frameAvg_ <= 0.0f ? frame : frameAvg_ * 0.95f + frame * 0.05f;
    slowFor_ = frameAvg_ > kSlowFrame ? slowFor_ + frame : 0.0f;
    if (slowFor_ < kPatience) return;
    g_qualityStepped = true;
    st.highQuality = false;
    saveSettings(st);
    eventNotice_ = {"Graphics set to Low to keep the game smooth - change it in Settings", 0xc8c0e0};
    eventNoticeTime_ = 0.0f;
}

void GameScene::render(float alpha) {
    ui::beginFrame(app_.screenWidth(), app_.screenHeight());
    watchFrameRate();
    // The system cursor gives way to the reticle in play, and is back in every menu and screen.
    if (showReticle() != IsCursorHidden()) {
        if (showReticle()) HideCursor();
        else ShowCursor();
    }
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
        levelRenderer_.setDeathFade(mode_ == Mode::Dead ? clamp(endTimer_ / kDeathScreenDelay, 0.0f, 1.0f) : 0.0f);
        levelRenderer_.render(*level_, cam, a, time_, app_.screenWidth(), app_.screenHeight());
        if (mode_ != Mode::Dead && mode_ != Mode::Victory) drawWorldOverlays(a);
        if (mode_ == Mode::Playing) drawCompass(a);
        if (showReticle()) drawReticle(a);
        // The end screen covers the HUD: an empty health bar adds nothing under it.
        if (mode_ != Mode::Victory && !(mode_ == Mode::Dead && endTimer_ > kDeathScreenDelay)) drawHud();
        // The lens names a thing for a few seconds, then lets it be.
        if (mode_ == Mode::Playing && !lens_.title.empty() && lens_.same < 4.0f) ui::tooltip(lens_.title, lens_.body, lens_.color);
        if (mode_ == Mode::Playing && app_.settings().hints) hints_.draw(static_cast<float>(app_.screenWidth()), static_cast<float>(app_.screenHeight()), usingGamepad_);
        // Banners belong to play: under a menu they would ghost through its dimming.
        if (mode_ == Mode::Playing) {
            // Placed while it fades in, once the camera has settled against the level's edges.
            if (titleTimer_ > kTitleCardSeconds - 0.4f || (titleTimer_ > 0.0f && titleTop_ < 0.0f)) titleTop_ = pickTitleTop();
            drawTitleCard();
            drawEventNotice();
            if (app_.settings().hints && !run_.inSanctum && run_.levelIndex == 0 && level_->time() < 30.0f) drawControlsHint();
        }
    }

    switch (mode_) {
    case Mode::Inventory: drawInventory(); break;
    case Mode::Map:
        minimap_.drawFull(*level_, static_cast<float>(app_.screenWidth()), static_cast<float>(app_.screenHeight()), time_, usingGamepad_);
        break;
    case Mode::Paused: drawPauseMenu(); break;
    case Mode::Settings:
        ui::dimScreen(150);
        if (drawSettingsPanel(app_)) mode_ = Mode::Paused;
        break;
    case Mode::Help:
        if (drawHelpPanel(app_, usingGamepad_)) mode_ = Mode::Paused;
        break;
    case Mode::Dead:
        if (endTimer_ > kDeathScreenDelay) drawEndScreen(false);
        break;
    case Mode::Victory: drawEndScreen(true); break;
    default: break;
    }
    drawFeatToast();

    if (exiting_ && level_) {
        // Stepping through the portal: a violet bloom swells from it and swallows the screen
        // as it fades to black.
        const Vector2 c = scene_.worldToScreen(level_->exitPos() - Vec2{0.0f, 9.0f});
        const auto glow = static_cast<unsigned char>(210.0f * std::sin(fade_ * PI));
        DrawCircleGradient(static_cast<int>(c.x), static_cast<int>(c.y), (0.1f + 1.3f * fade_) * static_cast<float>(app_.screenWidth()),
                           Color{235, 215, 255, glow}, Color{235, 215, 255, 0});
    }
    if (fade_ > 0.0f) ui::dimScreen(static_cast<unsigned char>(fade_ * 255.0f));
    if (mode_ == Mode::Loading) drawLoading();
    if (app_.settings().showFps || debugOverlay_) drawDebugOverlay();
}

void GameScene::recordProfile(bool victory) {
    if (profileRecorded_) return;
    profileRecorded_ = true;
    std::string fate = run_.quintessence() ? "Perfected the Work" : "Completed the Great Work";
    if (!victory) fate = level_ && !level_->player().alive() ? deathCause(level_->player()) : "Abandoned the Work";
    if (fate.empty()) fate = "Perished";
    Profile scratch;
    const Profile* after = &app_.profile();
    if (app_.options().previewOnly()) {
        // Bot runs, captures and previews show what the end screen would say, but leave the profile be.
        scratch = app_.profile();
        record_ = foldRun(scratch, run_, victory, fate);
        after = &scratch;
    } else {
        record_ = recordRun(app_.profile(), run_, victory, fate);
        discardSuspendedRun();
    }
    // Feats the ending earns.
    const bool died = level_ && !level_->player().alive();
    if (victory) earnFeat(FeatId::MagnumOpus);
    if (victory && run_.quintessence()) earnFeat(FeatId::Quintessence);
    if (victory && run_.difficulty == Difficulty::Archmage) earnFeat(FeatId::Archmage);
    if (run_.daily && (victory || died)) earnFeat(FeatId::DailyRite);
    // Any path beyond the Apprentice's open, however it opened (an abandoned run included).
    const Milestones m = milestonesOf(*after);
    for (int i = 1; i < kLoadoutCount; ++i)
        if (loadoutUnlocked(static_cast<LoadoutId>(i), m)) {
            earnFeat(FeatId::Pathfinder);
            break;
        }
    if (std::popcount(after->spellsSeen) >= 25) earnFeat(FeatId::Polymath);
}

void GameScene::earnFeat(FeatId feat) {
    // Previews announce feats but leave the profile exactly as loaded, so no later save in the
    // menu can write them to disk.
    Profile& p = app_.profile();
    const bool preview = app_.options().previewOnly();
    uint32_t& mask = preview ? g_previewFeats : p.featsEarned;
    if ((mask | p.featsEarned) & featBit(feat)) return;
    mask |= featBit(feat);
    if (!preview) saveProfile(p);
    toasts_.push_back({feat});
    app_.audio().play(Sfx::Perk, 0.6f, 1.3f);
}

void GameScene::updateFeats(float dt) {
    if (mode_ == Mode::Playing) {
        if (run_.gold >= 500) earnFeat(FeatId::Midas);
        if (run_.flask.amount >= Flask::kCapacity) earnFeat(FeatId::Brimming);
        if (run_.stats.chestsOpened >= 5) earnFeat(FeatId::TreasureHunter);
    }
    // Toasts show one at a time.
    if (!toasts_.empty() && (toasts_.front().time += dt) > kToastSeconds) toasts_.erase(toasts_.begin());
}

} // namespace pm
