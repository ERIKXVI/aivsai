#include "app/App.hpp"

#include <algorithm>

#include "audio/Audio.hpp"
#include "game/Run.hpp"
#include "raylib.h"
#include "render/Sprites.hpp"
#include "scenes/GameScene.hpp"
#include "scenes/MenuScene.hpp"
#include "scenes/SandboxScene.hpp"
#include "sim/Material.hpp"
#include "ui/Ui.hpp"

namespace pm {

App::App(const LaunchOptions& options) : options_(options) {
    unsigned flags = FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT;
    SetConfigFlags(flags);
    SetTraceLogLevel(LOG_WARNING);

    int w = options_.width > 0 ? options_.width : 1600;
    int h = options_.height > 0 ? options_.height : 900;
    InitWindow(w, h, "Prima Materia");
    SetWindowMinSize(960, 540);
    SetExitKey(KEY_NULL);

    if (!options_.windowed && options_.width == 0) {
        // Fit the window comfortably on the current monitor.
        int monitor = GetCurrentMonitor();
        int mw = GetMonitorWidth(monitor), mh = GetMonitorHeight(monitor);
        if (mw > 0 && mh > 0) {
            int ww = std::min(w, mw * 85 / 100), wh = std::min(h, mh * 85 / 100);
            SetWindowSize(ww, wh);
            SetWindowPosition((mw - ww) / 2, (mh - wh) / 2);
        }
    }

    int refresh = GetMonitorRefreshRate(GetCurrentMonitor());
    SetTargetFPS(refresh > 0 ? std::max(refresh, 60) : 144);

    InitAudioDevice();
    initMaterials();
    settings_ = loadSettings();
    profile_ = loadProfile();
    audio_ = std::make_unique<Audio>();
    sprites_ = std::make_unique<SpriteBank>();
    sprites_->load();
    ui::setAudio(audio_.get());
    applySettings();
    if (settings_.fullscreen && !options_.windowed) ToggleBorderlessWindowed();

    screenW_ = GetScreenWidth();
    screenH_ = GetScreenHeight();
}

App::~App() {
    scene_.reset();
    pendingScene_.reset();
    ui::setAudio(nullptr);
    sprites_.reset();
    audio_.reset();
    if (IsAudioDeviceReady()) CloseAudioDevice();
    CloseWindow();
}

void App::setScene(std::unique_ptr<Scene> scene) { pendingScene_ = std::move(scene); }

void App::applyPendingScene() {
    if (pendingScene_) {
        scene_ = std::move(pendingScene_);
        scene_->onResize(screenW_, screenH_);
    }
}

void App::toggleFullscreen() {
    ToggleBorderlessWindowed();
    settings_.fullscreen = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
}

void App::applySettings() {
    audio_->setMasterVolume(settings_.masterVolume);
    audio_->setMusicVolume(settings_.musicVolume);
    audio_->setSfxVolume(settings_.sfxVolume);
}

int App::run() {
    if (options_.sandbox) {
        setScene(std::make_unique<SandboxScene>(*this));
    } else if (options_.startRun) {
        uint32_t seed = options_.seed ? options_.seed : static_cast<uint32_t>(GetRandomValue(1, 0x7FFFFFFF));
        setScene(std::make_unique<GameScene>(*this, newRun(seed, static_cast<Difficulty>(clamp(settings_.difficulty, 0, 2)), false)));
    } else {
        setScene(std::make_unique<MenuScene>(*this));
    }
    applyPendingScene();

    double accumulator = 0.0;
    double last = GetTime();
    while (!quit_ && !WindowShouldClose()) {
        double now = GetTime();
        double frameDt = std::min(now - last, 0.25);
        last = now;
        time_ += frameDt;

        if (IsKeyPressed(KEY_F11) || ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) && IsKeyPressed(KEY_ENTER))) {
            toggleFullscreen();
        }
        if (GetScreenWidth() != screenW_ || GetScreenHeight() != screenH_) {
            screenW_ = GetScreenWidth();
            screenH_ = GetScreenHeight();
            if (scene_) scene_->onResize(screenW_, screenH_);
        }

        // Fixed-rate simulation. If the machine falls behind we drop time rather than
        // spiralling: at most 3 ticks per rendered frame.
        accumulator += frameDt;
        int ticks = 0;
        while (accumulator >= kTickDt && ticks < 3) {
            scene_->tick();
            accumulator -= kTickDt;
            ++ticks;
        }
        if (ticks == 3) accumulator = std::min(accumulator, static_cast<double>(kTickDt));

        scene_->frame(static_cast<float>(frameDt));

        BeginDrawing();
        scene_->render(static_cast<float>(accumulator / kTickDt));
        EndDrawing();

        ++frameCount_;
        if (options_.captureEvery > 0 && frameCount_ % static_cast<uint64_t>(options_.captureEvery) == 0) {
            Image shot = LoadImageFromScreen();
            std::string path = options_.capturePath;
            auto dot = path.rfind('.');
            path.insert(dot == std::string::npos ? path.size() : dot,
                        TextFormat("_%04d", static_cast<int>(frameCount_ / static_cast<uint64_t>(options_.captureEvery))));
            ExportImage(shot, path.c_str());
            UnloadImage(shot);
        }
        if (options_.quitAfter >= 0 && frameCount_ >= static_cast<uint64_t>(options_.quitAfter)) quit_ = true;
        if (options_.captureFrame >= 0 && frameCount_ == static_cast<uint64_t>(options_.captureFrame)) {
            Image shot = LoadImageFromScreen();
            ExportImage(shot, options_.capturePath.c_str());
            UnloadImage(shot);
            quit_ = true;
        }
        applyPendingScene();
    }
    return 0;
}

} // namespace pm
