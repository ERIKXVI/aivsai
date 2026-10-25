// Offline checks for the procedural audio. Synthesis needs no audio device, so these run in CI.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "audio/Music.hpp"
#include "audio/SfxSynth.hpp"
#include "tests/TestFramework.hpp"

using namespace pm;

namespace {

struct Levels {
    float peak = 0.0f, rms = 0.0f;
    bool finite = true;
};

Levels measure(const std::vector<float>& samples) {
    Levels l;
    double sum = 0.0;
    for (float v : samples) {
        if (!std::isfinite(v)) l.finite = false;
        l.peak = std::max(l.peak, std::fabs(v));
        sum += static_cast<double>(v) * v;
    }
    l.rms = samples.empty() ? 0.0f : static_cast<float>(std::sqrt(sum / static_cast<double>(samples.size())));
    return l;
}

/// Renders `seconds` of interleaved stereo music in the chunk size the audio thread uses.
std::vector<float> renderMusic(int root, int mode, float intensity, int seconds) {
    MusicSynth synth;
    synth.setMood(root, mode, intensity);
    std::vector<float> buffer(static_cast<size_t>(seconds) * MusicSynth::kSampleRate * 2);
    constexpr unsigned kChunk = 4096;
    const size_t frames = buffer.size() / 2;
    for (size_t frame = 0; frame < frames; frame += kChunk)
        synth.render(buffer.data() + frame * 2, static_cast<unsigned>(std::min<size_t>(kChunk, frames - frame)));
    return buffer;
}

} // namespace

TEST_CASE("every sound effect renders audible mono 16-bit audio") {
    for (int i = 0; i < static_cast<int>(Sfx::Count); ++i) {
        Wave w = synthesizeSfx(static_cast<Sfx>(i));
        CHECK(w.data != nullptr);
        CHECK(w.channels == 1);
        CHECK(w.sampleSize == 16);
        CHECK(w.sampleRate == static_cast<unsigned>(kSfxSampleRate));
        // The longest effect (the rumble) lasts two seconds.
        CHECK(w.frameCount > 0 && w.frameCount <= 3u * kSfxSampleRate);
        std::vector<float> samples(w.frameCount);
        const auto* data = static_cast<const short*>(w.data);
        for (unsigned j = 0; data && j < w.frameCount; ++j) samples[j] = static_cast<float>(data[j]) / 32768.0f;
        const Levels l = measure(samples);
        const bool audible = l.peak > 0.02f && l.rms > 0.005f;
        if (!audible) std::printf("    sfx %d is near silent: peak %.3f rms %.4f\n", i, l.peak, l.rms);
        CHECK(audible);
        UnloadWave(w);
    }
}

TEST_CASE("sound effect synthesis is deterministic") {
    for (int i = 0; i < static_cast<int>(Sfx::Count); ++i) {
        Wave a = synthesizeSfx(static_cast<Sfx>(i));
        Wave b = synthesizeSfx(static_cast<Sfx>(i));
        CHECK(a.frameCount == b.frameCount);
        if (a.frameCount == b.frameCount && a.data && b.data)
            CHECK(std::memcmp(a.data, b.data, a.frameCount * sizeof(short)) == 0);
        UnloadWave(a);
        UnloadWave(b);
    }
}

TEST_CASE("music in every mode is finite, bounded and audible, calm or in combat") {
    for (int mode = 0; mode < MusicSynth::kModeCount; ++mode) {
        for (float intensity : {0.1f, 1.0f}) {
            const Levels l = measure(renderMusic(45 + mode, mode, intensity, 6));
            const bool ok = l.finite && l.peak <= 1.0f && l.rms > 0.01f;
            if (!ok)
                std::printf("    music mode %d intensity %.1f: peak %.3f rms %.4f%s\n", mode, intensity, l.peak, l.rms,
                            l.finite ? "" : " (non-finite)");
            CHECK(ok);
        }
    }
}

TEST_CASE("music setMood clamps an out-of-range mode to the nearest scale") {
    constexpr int kLast = MusicSynth::kModeCount - 1;
    const std::vector<float> first = renderMusic(48, 0, 0.5f, 2), last = renderMusic(48, kLast, 0.5f, 2);
    // The first and last scales open on different chords, so the render does depend on the mode.
    CHECK(first != last);
    CHECK(renderMusic(48, -2, 0.5f, 2) == first);
    CHECK(renderMusic(48, kLast + 3, 0.5f, 2) == last);
}
