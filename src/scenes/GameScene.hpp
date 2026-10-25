#pragma once
// A run in progress: loads levels in the background, drives the Level with player input,
// renders it and draws the HUD, inventory (wand editing), pause menu and end screens.

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "app/Scene.hpp"
#include "game/Bot.hpp"
#include "game/Camera.hpp"
#include "game/Input.hpp"
#include "game/Level.hpp"
#include "game/LevelRenderer.hpp"
#include "game/Run.hpp"
#include "render/SceneRenderer.hpp"
#include "scenes/Hints.hpp"
#include "ui/Minimap.hpp"
#include "ui/Ui.hpp"

namespace pm {

class GameScene final : public Scene {
public:
    GameScene(App& app, RunState run);
    ~GameScene() override;

    void tick() override;
    void frame(float dt) override;
    void render(float alpha) override;
    void onSuspend() override;

private:
    enum class Mode { Loading, Playing, Inventory, Map, Paused, Settings, Dead, Victory };

    void requestLevel();
    void stepLevel();
    static void logAutoplay(const char* message);
    void finishLoading();
    void advanceStage();
    PlayerInput gatherInput();
    void latchInput();
    void recordProfile(bool victory);

    void drawWorldOverlays(float alpha);
    void drawHud();
    void drawCompass(float alpha);
    void drawInventory();
    void drawPauseMenu();
    void drawEndScreen(bool victory);
    void drawTitleCard();
    void drawLoading();
    void drawControlsHint();
    void drawDebugOverlay();

    RunState run_;
    std::unique_ptr<Level> level_;
    std::future<LevelLayout> pending_;
    SceneRenderer scene_;
    LevelRenderer levelRenderer_;
    FollowCamera camera_;
    Bot bot_;
    HintTracker hints_;
    ui::NavList pauseNav_;
    Minimap minimap_;
    Mode mode_ = Mode::Loading;

    float fade_ = 1.0f;          ///< 1 = black
    float loadTimer_ = 0.0f;     ///< seconds spent on the loading screen
    bool loadSkipped_ = false;   ///< the player pressed on past the motto
    bool castBlocked_ = false;   ///< the left button was held over a menu; ignore it until released
    bool exiting_ = false;       ///< fading out towards the next stage
    float titleTimer_ = 0.0f;
    std::string titleMain_, titleSub_, titleOmen_;
    float endTimer_ = 0.0f;
    float time_ = 0.0f;
    float slowMo_ = 1.0f;
    float tickAccumulator_ = 0.0f;
    bool profileRecorded_ = false;
    std::string discoveries_;                ///< new Grimoire entries this run ("2 spells, 1 perk")
    std::vector<std::string> unlockedPaths_; ///< starting paths unlocked by this run
    bool debugOverlay_ = false;
    bool usingGamepad_ = false;
    float heartbeat_ = 0.0f;     ///< seconds until the next low-health heartbeat  ///< last input came from a gamepad (for button prompts)

    // Input pressed between ticks is latched so nothing is lost at high frame rates.
    bool latchJump_ = false, latchInteract_ = false, latchDrink_ = false, latchCast_ = false;
    int latchWand_ = -1, latchCycle_ = 0;

    // Inventory drag & drop.
    struct Drag {
        bool active = false;
        SpellId spell = SpellId::None;
        int wand = -1; ///< -1 = satchel
        int slot = -1;
    } drag_;
};

} // namespace pm
