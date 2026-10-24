#pragma once
// Application shell: window, audio device, worker threads, settings and the scene stack.

#include <memory>
#include <string>

#include "app/Persistence.hpp"
#include "app/Scene.hpp"
#include "core/ThreadPool.hpp"
#include "core/Timing.hpp"

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
    std::string loadout;      ///< comma-separated spell names for the first wand (testing)
    bool startInSanctum = false; ///< begin the --play run in a sanctum (testing)
    std::string openUi;       ///< "inventory" or "pause": open that screen once loaded (testing)
    int captureFrame = -1;    ///< save a screenshot after this many frames, then quit (testing)
    std::string capturePath = "capture.png";
    int captureEvery = 0;     ///< also save numbered screenshots every N frames (testing)
    int quitAfter = -1;       ///< quit after this many frames (testing)
    int width = 0, height = 0;
};

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

    LaunchOptions options_;
    ThreadPool pool_;
    Settings settings_;
    Profile profile_;
    std::unique_ptr<Audio> audio_;
    std::unique_ptr<SpriteBank> sprites_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<Scene> pendingScene_;
    int screenW_ = 0, screenH_ = 0;
    double time_ = 0.0;
    uint64_t frameCount_ = 0;
    bool quit_ = false;
};

} // namespace pm
