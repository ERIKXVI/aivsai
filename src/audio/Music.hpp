#pragma once
// Generative music. A small synthesizer runs on the audio thread: detuned saw pads follow a
// chord progression in the biome's mode, a bass line and Karplus-Strong plucks improvise
// over it, drums fade in with combat intensity, and everything sits in a cave reverb.
// Ambient beds (fire crackle, water babble, lava rumble) are mixed into the same stream.

#include <atomic>
#include <memory>

#include "raylib.h"

namespace pm {

/// The synthesizer itself, independent of any audio device (also used by offline tools).
/// Parameters are atomics so the game thread can steer a synth running on the audio thread.
class MusicSynth {
public:
    static constexpr int kSampleRate = 44100;

    MusicSynth();
    ~MusicSynth();
    MusicSynth(const MusicSynth&) = delete;
    MusicSynth& operator=(const MusicSynth&) = delete;

    void setVolume(float v) { volume_.store(v); }
    /// `root` is a MIDI note, `mode` a scale index, `intensity` 0..1 (drives drums/arps).
    void setMood(int root, int mode, float intensity);
    void setAmbient(float fire, float water, float lava);
    /// Renders interleaved stereo float samples.
    void render(float* out, unsigned int frames);

private:
    struct State;
    std::unique_ptr<State> state_;

    std::atomic<float> volume_{0.7f};
    std::atomic<float> intensity_{0.0f};
    std::atomic<int> root_{45};
    std::atomic<int> mode_{0};
    std::atomic<float> fire_{0.0f}, water_{0.0f}, lava_{0.0f};
};

/// Streams a MusicSynth to the audio device.
class MusicEngine {
public:
    MusicEngine();
    ~MusicEngine();
    MusicEngine(const MusicEngine&) = delete;
    MusicEngine& operator=(const MusicEngine&) = delete;

    void setVolume(float v) { synth_.setVolume(v); }
    void setMood(int root, int mode, float intensity) { synth_.setMood(root, mode, intensity); }
    void setAmbient(float fire, float water, float lava) { synth_.setAmbient(fire, water, lava); }
    void update(float) {}

private:
    static void streamCallback(void* buffer, unsigned int frames);

    MusicSynth synth_;
    AudioStream stream_{};
    bool started_ = false;
};

} // namespace pm
