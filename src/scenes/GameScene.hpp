#pragma once
// A run in progress: loads levels in the background, drives the Level with player input,
// renders it and draws the HUD, inventory (wand editing), pause menu and end screens.

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "app/RunRecords.hpp"
#include "app/Scene.hpp"
#include "game/Bot.hpp"
#include "game/Camera.hpp"
#include "game/Input.hpp"
#include "game/Level.hpp"
#include "game/LevelRenderer.hpp"
#include "game/RouteGuide.hpp"
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

    // Durations shared by the update half (GameScene.cpp) and the drawing half (GameSceneUi.cpp).
    static constexpr float kTitleCardSeconds = 4.0f;  ///< how long a stage/guardian title card shows
    static constexpr float kToastSeconds = 3.5f;      ///< how long one feat toast shows
    static constexpr float kGoldPopSeconds = 0.25f;   ///< HUD gold counter pop after gold comes in
    static constexpr float kDeathScreenDelay = 1.6f;  ///< seconds after death before the end screen fades in

    void requestLevel();
    void stepLevel();
    void finishLoading();
    /// Command-line testing hooks (--at, --ui): teleport, open a screen, force the death preview.
    void applyLaunchOptions();
    void advanceStage();
    PlayerInput gatherInput();
    void latchInput();
    void noteInputDevice();
    void updateLens(float dt);
    /// Low-health heartbeat cue; it quickens as hp falls below a quarter of max.
    void updateHeartbeat(float dt);
    /// Pause on focus loss, per-mode key/pad transitions (pause, inventory, map) and mode timers.
    void updateModeInput(float dt);
    /// Records a feat in the profile the first time it is earned, and queues its toast.
    void earnFeat(FeatId feat);
    void updateFeats(float dt);
    void drawFeatToast() const;
    void drawEssences(float right, float y) const;
    void updateEditorPad();
    /// Records the left stick's current direction, so a stick already held does not count as a flick.
    void syncStickFlick();
    /// Drops the dragged spell onto a wand slot (wand >= 0) or satchel slot (wand -1), swapping.
    void dropDragOnto(int wand, int slot);
    SpellId& editorSlot(int wand, int slot);
    void recordProfile(bool victory);

    void drawWorldOverlays(float alpha) const;
    void drawHud() const;
    void drawCompass(float alpha) const;
    void drawInventory();
    void drawPauseMenu();
    void drawEndScreen(bool victory);
    void drawTitleCard() const;
    void drawEventNotice() const;
    void drawLoading() const;
    void drawControlsHint() const;
    void drawDebugOverlay() const;

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
    RouteGuide guide_; ///< where the compass points along the stage's route
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
    RunRecord record_;           ///< what the finished run added to the profile, for the end screen
    bool debugOverlay_ = false;
    bool usingGamepad_ = false;  ///< last input came from a gamepad (for button prompts)
    float heartbeat_ = 0.0f;     ///< seconds until the next low-health heartbeat
    // Matter lens: names what the resting cursor points at.
    struct Lens {
        std::string title, body;
        Color color = WHITE;
        float rest = 0.0f; ///< seconds the cursor has rested on one spot of the world
        float same = 0.0f; ///< seconds the same thing has been named
        float hold = 0.0f; ///< grace before a flickering reading (fire, gas, spray) clears the label
        Vector2 mouse{};
        Vec2 anchor;       ///< world spot where the rest began
    } lens_;
    int shownGold_ = -1;         ///< gold total the HUD last saw
    uint32_t announcedGuardian_ = 0; ///< id of the guardian whose title card was last shown
    struct Toast {
        FeatId feat;
        float time = 0.0f; ///< seconds shown so far
    };
    std::vector<Toast> toasts_;      ///< newly earned feats, announced one at a time
    float stageDamageStart_ = 0.0f;  ///< damage taken before this stage (for Untouched)
    float goldPop_ = 0.0f;       ///< seconds left on the HUD gold counter's pop
    EventNotice eventNotice_;    ///< the line announcing a stage event...
    float eventNoticeTime_ = 0.0f; ///< ... and how long it has shown

    // Input pressed between ticks is latched so nothing is lost at high frame rates.
    bool latchJump_ = false, latchInteract_ = false, latchDrink_ = false, latchCast_ = false;
    int latchWand_ = -1, latchCycle_ = 0;

    // Inventory drag & drop. With a gamepad a slot cursor stands in for the mouse.
    struct Drag {
        bool active = false;
        SpellId spell = SpellId::None;
        int wand = -1; ///< -1 = satchel
        int slot = -1;
    } drag_;
    struct SlotRef {
        int wand = 0; ///< -1 = satchel
        int slot = 0;
    } padCursor_;
    int stickX_ = 0, stickY_ = 0;   ///< left stick direction last frame, for flick navigation
    float firstCastFlash_ = 0.0f;   ///< seconds left highlighting a changed wand's first-cast line
    int firstCastWand_ = -1;
};

} // namespace pm
