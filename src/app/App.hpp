#pragma once
// Application shell: window, audio device, worker threads, settings and the scene stack.

#include <cstdint>
#include <memory>
#include <string>

#include "app/Persistence.hpp"
#include "app/Scene.hpp"
#include "core/ThreadPool.hpp"

namespace pm {

class Audio;
class SpriteBank;

struct LaunchOptions {
    bool sandbox = false;     ///< start directly in the alchemy sandbox
    bool startRun = false;    ///< skip the menu and start a run
    bool windowed = false;    ///< force windowed mode
    bool autoplay = false;    ///< let the test bot drive the player
    bool turbo = false;       ///< run ticks as fast as possible, render rarely (headless tests)
    bool god = false;         ///< player cannot die (coverage tests)
    uint32_t seed = 0;        ///< 0 = random
    int startLevel = 0;       ///< stage to begin a --play run on (testing)
    int startLoadout = 0;     ///< starting loadout (LoadoutId) for a --play run
    int ordeal = 0;           ///< ordeal level for a --play run (testing)
    std::string testWand;     ///< comma-separated spell names for the first wand, "+Name" for the satchel, "-" empty (testing)
    bool startInSanctum = false; ///< begin the --play run in a sanctum (testing)
    std::string openUi;       ///< open a screen once loaded: inventory, pause, transmuter, vault, lair, shop, map, help, elixir, death (in a run) or loadout, grimoire, help, settings (menu)
    int captureFrame = -1;    ///< save a screenshot after this many frames, then quit (testing)
    std::string capturePath = "capture.png";
    int captureEvery = 0;     ///< also save numbered screenshots every N frames (testing)
    int quitAfter = -1;       ///< quit after this many frames (testing)
    int width = 0, height = 0;
    float teleportX = -1.0f, teleportY = -1.0f; ///< place the player here once loaded (testing)

    /// Automated sessions (bot, captures) must not touch the player's profile.
    bool automated() const { return autoplay || captureFrame >= 0 || captureEvery > 0 || quitAfter >= 0; }
    /// Runs that must leave the profile and the suspended run alone: bots, captures and the
    /// --ui death preview.
    bool previewOnly() const { return automated() || openUi == "death"; }
};

/// A fresh random seed for a new run. Never 0, because `LaunchOptions::seed == 0` means
/// "random", so every seed shown to the player can be replayed with --seed.
uint32_t randomRunSeed();

class App {
public:
    explicit App(const LaunchOptions& options);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int run();

    /// Replaces the current scene at the end of the frame.
    void setScene(std::unique_ptr<Scene> scene);
    void requestQuit() { quit_ = true; }

    ThreadPool& pool() { return pool_; }
    Audio& audio() { return *audio_; }
    const SpriteBank& sprites() const { return *sprites_; }
    Settings& settings() { return settings_; }
    Profile& profile() { return profile_; }
    /// Contextual hints already shown since the game was launched (bit = Hint). Kept per
    /// launch rather than per profile so everyone sharing a PC gets the introduction.
    uint32_t& hintsShown() { return hintsShown_; }
    /// Pushes volume settings to the audio system.
    void applySettings();
    const LaunchOptions& options() const { return options_; }
    int screenWidth() const { return screenW_; }
    int screenHeight() const { return screenH_; }
    double time() const { return time_; }
    uint64_t frameCount() const { return frameCount_; }

    void toggleFullscreen();

private:
    void applyPendingScene();
    void enterBorderless();
    /// Sets the frame limiter for the current monitor, or 30 fps while in the background.
    void applyFrameCap();
    /// On focus changes: keeps the display awake while focused and throttles the background.
    void noteFocus();

    LaunchOptions options_;
    ThreadPool pool_;
    Settings settings_;
    Profile profile_;
    uint32_t hintsShown_ = 0;
    std::unique_ptr<Audio> audio_;
    std::unique_ptr<SpriteBank> sprites_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<Scene> pendingScene_;
    int screenW_ = 0, screenH_ = 0;
    double time_ = 0.0;
    uint64_t frameCount_ = 0;
    bool quit_ = false;
    bool focused_ = false; ///< the window had focus at the last noteFocus()
};

} // namespace pm
