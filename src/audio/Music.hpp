#pragma once
// Generative music. A small synthesizer runs on the audio thread: detuned saw pads follow a
// chord progression in the biome's mode, a bass line and Karplus-Strong plucks improvise
// over it, drums fade in with combat intensity, and everything sits in a cave reverb.
// Ambient beds (fire crackle, water babble, lava rumble) are mixed into the same stream.

#include <atomic>

#include "raylib.h"

namespace pm {

class MusicEngine {
public:
    MusicEngine();
    ~MusicEngine();
    MusicEngine(const MusicEngine&) = delete;
    MusicEngine& operator=(const MusicEngine&) = delete;

    void setVolume(float v) { volume_.store(v); }
    /// `root` is a MIDI note, `mode` a scale index, `intensity` 0..1 (drives drums/arps).
    void setMood(int root, int mode, float intensity);
    void setAmbient(float fire, float water, float lava);
    void update(float dt);

private:
    struct State;
    static void streamCallback(void* buffer, unsigned int frames);
    void render(float* out, unsigned int frames);

    AudioStream stream_{};
    bool started_ = false;
    State* state_ = nullptr; ///< audio-thread synthesis state

    std::atomic<float> volume_{0.7f};
    std::atomic<float> intensity_{0.0f};
    std::atomic<int> root_{45};
    std::atomic<int> mode_{0};
    std::atomic<float> fire_{0.0f}, water_{0.0f}, lava_{0.0f};
};

} // namespace pm
