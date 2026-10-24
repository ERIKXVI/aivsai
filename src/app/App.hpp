#pragma once
// Application shell: window, audio device, worker threads, settings and the scene stack.

#include <memory>
#include <string>

#include "app/Scene.hpp"
#include "core/ThreadPool.hpp"

namespace pm {

constexpr int kTickRate = 60;
constexpr float kTickDt = 1.0f / static_cast<float>(kTickRate);

struct LaunchOptions {
    bool sandbox = false;     ///< start directly in the alchemy sandbox
    bool startRun = false;    ///< skip the menu and start a run
    bool windowed = false;    ///< force windowed mode
    bool autoplay = false;    ///< let the test bot drive the player
    uint32_t seed = 0;        ///< 0 = random
    int captureFrame = -1;    ///< save a screenshot after this many frames, then quit (testing)
    std::string capturePath = "capture.png";
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
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<Scene> pendingScene_;
    int screenW_ = 0, screenH_ = 0;
    double time_ = 0.0;
    uint64_t frameCount_ = 0;
    bool quit_ = false;
};

} // namespace pm
