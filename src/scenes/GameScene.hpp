#pragma once
// A run in progress: loads levels in the background, drives the Level with player input,
// renders it and draws the HUD, inventory (wand editing), pause menu and end screens.

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "app/InputLatch.hpp"
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

    void onFrameInput() override;
    void tick() override;
    void frame(float dt) override;
    void render(float alpha) override;
    void onSuspend() override;

private:
    enum class Mode { Loading, Playing, Inventory, Map, Paused, Settings, Help, Dead, Victory };

    // Durations shared by the run flow (GameScene.cpp) and the presentation files (GameSceneUi.cpp, GameSceneMenus.cpp).
    static constexpr float kTitleCardSeconds = 4.0f;   ///< how long a stage/guardian title card shows
    static constexpr float kToastSeconds = 3.5f;       ///< how long one feat toast shows
    static constexpr float kGoldPopSeconds = 0.25f;    ///< HUD gold counter pop after gold comes in
    static constexpr float kDeathScreenDelay = 1.6f;   ///< seconds after death before the end screen fades in
    static constexpr float kVictoryScreenDelay = 0.5f; ///< seconds after victory before the end screen fades in

    // Run flow (GameScene.cpp)
    void requestLevel();
    void stepLevel();
    void finishLoading();
    /// Command-line testing hooks (--at, --ui): teleport, open a screen, force the death preview.
    void applyLaunchOptions();
    void advanceStage();
    /// Ends the run on its death or victory screen and records it in the profile.
    void enterEndScreen(bool victory);
    PlayerInput gatherInput();
    void latchInput();
    void updateLens(float dt);
    /// Low-health heartbeat cue; it quickens as hp falls below a quarter of max.
    void updateHeartbeat(float dt);
    /// Pause on focus loss or a disconnected pad, per-mode key/pad transitions (pause, inventory, map) and mode timers.
    void updateModeInput(float dt);
    /// Raises the title card (stage or guardian); its height on screen is picked again as it fades in.
    void showTitleCard(std::string title, std::string subtitle, std::string omen = {});
    /// A guardian gets a title card the first time it notices the alchemist.
    void announceGuardian();
    /// The satchel badge, the health bars' pale chips and the gold counter's pop.
    void updateHudFeedback(float dt);
    /// Records a feat in the profile the first time it is earned, and queues its toast.
    void earnFeat(FeatId feat);
    void updateFeats(float dt);
    void recordProfile(bool victory);
    /// Steps graphics down to Low once if the GPU cannot keep up on High.
    void watchFrameRate();

    // HUD, overlays and banners (GameSceneUi.cpp)
    void drawWorldOverlays(float alpha) const;
    /// Transmutation circles near the alchemist: the matter each craves and how to pour it.
    void drawTransmuterPrompts() const;
    /// The focused pickup's label and details card (spell, perk, potion, wand).
    void drawPickupPrompt(const Pickup& p) const;
    void drawHud() const;
    /// Top left: the health, mana, reload, levitation and breath bars, and the status chips.
    void drawVitals() const;
    /// Top right: gold with its pop, the stage and its omen, the essences and the corner minimap.
    void drawRunCorner() const;
    /// Bottom left: the wand icons, the held wand's deck and the satchel's newest spell.
    void drawHotbar() const;
    /// Bottom right: the flask, its contents and its controls.
    void drawFlask() const;
    /// The boss's (or a guardian's) health bar along the top.
    void drawBossBar() const;
    void drawEssences(float right, float y) const;
    void drawCompass(float alpha) const;
    /// A floor or shop wand's slots and stats, ending just above `bottom` (the pickup prompt),
    /// compared with the wand in hand.
    void drawWandCard(const Wand& w, float cx, float bottom) const;
    /// The aim reticle (in place of the system cursor) with a ring showing the wand's readiness.
    void drawReticle(float alpha) const;
    /// In play, with the alchemist alive. Still captures (automated, not autoplay) leave it out:
    /// their idle pointer would sit on the screen's centre, over the alchemist.
    bool showReticle() const;
    void drawFeatToast() const;
    void drawTitleCard() const;
    /// Height of the title card's text block, in pixels.
    float titleCardHeight() const;
    /// Where the title card goes, as a fraction of the screen height: high or low, whichever
    /// keeps it clear of the alchemist and of the guardian it announces.
    float pickTitleTop() const;
    void drawEventNotice() const;
    /// The violet bloom that swells from the portal and swallows the screen as it fades through it.
    void drawPortalBloom() const;
    void drawLoading() const;
    void drawControlsHint() const;
    void drawDebugOverlay() const;

    // Wand editor (GameSceneEditor.cpp)
    struct InventoryLayout; ///< where the editor's panel and its parts go this frame
    struct InventoryHover;  ///< what the editor's slots and figures found under the mouse or the pad cursor
    void updateEditorPad();
    /// Records the left stick's current direction, so a stick already held does not count as a flick.
    void syncStickFlick();
    /// Drops the dragged spell onto a wand slot (wand >= 0) or satchel slot (wand -1), swapping.
    void dropDragOnto(int wand, int slot);
    /// Right-click (Y with a pad) on a spell: a wand's spell goes back to the satchel; a satchel spell, or a
    /// wand's when the satchel is full, is set down at the alchemist's feet as a pickup.
    void takeSpellOut(int wand, int slot);
    SpellId& editorSlot(int wand, int slot);
    void drawInventory();
    /// Sizes the editor's panel to its wand rows, satchel, perks, guide caption and rules card, and picks the guide.
    InventoryLayout layoutInventory() const;
    /// A row per wand (figures, slots, first-cast line) down from the panel's top; returns the y under the last.
    float drawWandRows(const InventoryLayout& lay, InventoryHover& hover) const;
    /// A wand's name, timing, mana and deck quirks left of its slots; hovering them shows its other figures.
    void drawWandFigures(const InventoryLayout& lay, size_t wi, float y, InventoryHover& hover) const;
    /// One wand or satchel slot, empty while its spell is in hand; noted in `hover` when the cursor is on it.
    void drawEditorSlot(int wand, int slotIndex, Rectangle r, SpellId sp, InventoryHover& hover) const;
    /// The line under a wand's slots: what its first cast does, or the booster left with nothing to work on.
    void drawFirstCastLine(const InventoryLayout& lay, size_t wi, float y) const;
    /// The satchel's label and its two rows of slots from `y`; returns the y under them.
    float drawSatchelGrid(const InventoryLayout& lay, float y, InventoryHover& hover) const;
    /// The perks held, in a column beside the satchel from `y`, and the run's kills, gold and time at the panel's foot.
    void drawPerksAndRunFigures(const InventoryLayout& lay, float y, InventoryHover& hover) const;
    /// The guided first edit: its two slots glow, a ghost of its spell drifts between them and its caption explains.
    void drawWandGuide(const InventoryLayout& lay, const InventoryHover& hover, float satchelBottom) const;
    /// The pad's slot cursor, put on the guide's spell while it is on no slot and the guide is not done.
    void drawPadCursor(InventoryHover& hover) const;
    /// Mouse presses: the left picks a spell up or puts the one in hand down, the right takes a spell out.
    void handleInventoryClicks(const InventoryHover& hover);
    /// The spell in hand, at the mouse or above the pad cursor; a mouse release away from its own slot drops it.
    void dragHeldSpell(const InventoryLayout& lay, const InventoryHover& hover);

    // Pause menu and end screens (GameSceneMenus.cpp)
    void drawPauseMenu();
    void drawEndScreen(bool victory);
    /// The end screen's lasting progress (records, Grimoire, unlocks, the next path's bar) from y; returns the y under it.
    float drawEndProgress(float y, unsigned char alpha) const;
    /// The end screen's buttons and pad/Enter shortcuts, armed once the face buttons are released.
    void drawEndActions(bool victory, float y);
    /// Arms the end screen's pad and Enter shortcuts once it has settled and the face buttons are up; true while armed.
    bool armEndShortcuts(bool victory);
    /// New Work (or Try Again), Retry Seed and Main Menu in a row from `buttonR`; true once one of them is chosen.
    bool drawEndButtonRow(bool victory, bool armed, int carriedOrdeal, Rectangle buttonR, float gap);
    /// Under the row, the offer to begin a new run as the path this run unlocked.
    void drawPathOffer(bool armed, int carriedOrdeal, Rectangle offer);
    /// Under the row, after two early falls in a row without a victory, the offer to try again as an Apprentice.
    void drawGentlerRoadOffer(bool victory, bool armed, Rectangle easier);
    /// The death screen's descent strip at `y`: the five stages, filled as far as the alchemist
    /// got, with a caption naming what lies below. Returns the y under it.
    float drawDescent(float y, unsigned char alpha) const;

    RunState run_;
    std::unique_ptr<Level> level_;
    std::future<LevelLayout> pending_;
    SceneRenderer scene_;
    LevelRenderer levelRenderer_;
    FollowCamera camera_;
    Bot bot_;
    HintTracker hints_;
    ui::NavList pauseNav_;
    bool confirmAbandon_ = false; ///< Abandon Run pressed once: the next press ends the run
    bool confirmQuit_ = false;    ///< Quit Game pressed once: the next press closes the game
    Minimap minimap_;
    RouteGuide guide_; ///< where the compass points along the stage's route
    Mode mode_ = Mode::Loading;

    float fade_ = 1.0f;          ///< 1 = black
    float loadTimer_ = 0.0f;     ///< seconds spent on the loading screen
    bool loadSkipped_ = false;   ///< the player pressed on past the motto
    bool castBlocked_ = false;   ///< the left button was held over a menu; ignore it until released
    bool exiting_ = false;       ///< fading out towards the next stage
    bool pauseOnArrival_ = false; ///< a pause asked for during the portal fade or the loading screen opens on the next stage
    float titleTimer_ = 0.0f;
    float titleTop_ = -1.0f; ///< the card's top as a fraction of the screen height; picked when it appears
    std::string titleMain_, titleSub_, titleOmen_;
    float endTimer_ = 0.0f;
    bool endInputArmed_ = false; ///< the end screen's pad and Enter shortcuts are live
    float time_ = 0.0f;
    float slowMo_ = 1.0f;
    float tickAccumulator_ = 0.0f;
    bool profileRecorded_ = false;
    RunRecord record_;           ///< what the finished run added to the profile, for the end screen
    RunStats endStats_{};        ///< the run's stats as recorded, for the end screen (later kills do not count)
    bool debugOverlay_ = false;
    Vec2 botAim_;              ///< where the autoplay bot aims (its reticle is drawn there)
    Vec2 padAim_{1.0f, 0.0f}; ///< the gamepad's aim direction, kept when the right stick is released
    bool padAimSet_ = false;   ///< the right stick has aimed at least once
    bool usingGamepad_ = false;  ///< last input came from a gamepad (for button prompts); a run starts with the menu's device
    bool padWasAvailable_ = false; ///< pad 0 was connected (and readable) last frame, to notice it dropping out
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
    ui::BarTrail hpTrail_, bossTrail_; ///< the pale chips a hit leaves on the health bars
    int satchelNew_ = 0;              ///< spells gone to the satchel since the editor was last opened
    int satchelCounted_ = 0;          ///< the level's spellsToSatchel() already added to satchelNew_
    SpellId satchelLatest_ = SpellId::None; ///< the newest of them, kept across stages
    uint32_t bossTrailId_ = 0;        ///< whose health the boss chip follows
    struct Toast {
        FeatId feat;
        float time = 0.0f; ///< seconds shown so far
    };
    std::vector<Toast> toasts_;      ///< newly earned feats, announced one at a time
    float stageDamageStart_ = 0.0f;  ///< damage taken before this stage (for Untouched)
    float goldPop_ = 0.0f;       ///< seconds left on the HUD gold counter's pop
    EventNotice eventNotice_;    ///< the line announcing a stage event...
    float eventNoticeTime_ = 0.0f; ///< ... and how long it has shown
    float frameAvg_ = 0.0f;        ///< smoothed real frame time, seconds
    float slowFor_ = 0.0f;         ///< how long play has run slower than the High-quality threshold

    // Input pressed between ticks is latched so nothing is lost at high frame rates.
    bool latchJump_ = false, latchInteract_ = false, latchDrink_ = false, latchCast_ = false;
    int latchWand_ = -1, latchCycle_ = 0;
    input::WheelStepper wheel_; ///< mouse wheel to wand steps

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
