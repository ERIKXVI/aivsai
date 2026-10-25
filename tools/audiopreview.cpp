// Developer tool: renders every sound effect and a few minutes of generative music to WAV
// files and prints level statistics, so audio can be sanity-checked without speakers.
//   pm_audiopreview <outdir>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "audio/Music.hpp"
#include "audio/SfxSynth.hpp"
#include "raylib.h"

namespace {

constexpr const char* kSfxNames[] = {"cast_spark", "cast_bolt", "cast_fire", "cast_heavy", "cast_magic",
                                     "cast_lightning", "dig", "explosion_small", "explosion_big", "hit", "enemy_hurt",
                                     "enemy_die", "player_hurt", "player_die", "jump", "land", "splash", "pickup",
                                     "coin", "potion", "drink", "chest_open", "portal", "perk", "ui_click", "ui_hover",
                                     "ui_deny", "sizzle", "freeze", "squish", "roar", "heartbeat", "rumble"};

struct Stats {
    float peak = 0.0f, rms = 0.0f;
    bool finite = true;
};

Stats analyze(const float* samples, size_t count) {
    Stats s;
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        float v = samples[i];
        if (!std::isfinite(v)) s.finite = false;
        s.peak = std::max(s.peak, std::fabs(v));
        sum += static_cast<double>(v) * v;
    }
    s.rms = count ? static_cast<float>(std::sqrt(sum / static_cast<double>(count))) : 0.0f;
    return s;
}

} // namespace

int main(int argc, char** argv) {
    using namespace pm;
    std::string out = argc > 1 ? argv[1] : ".";
    SetTraceLogLevel(LOG_WARNING);

    static_assert(sizeof(kSfxNames) / sizeof(kSfxNames[0]) == static_cast<size_t>(Sfx::Count), "name table out of sync");
    for (int i = 0; i < static_cast<int>(Sfx::Count); ++i) {
        Wave w = synthesizeSfx(static_cast<Sfx>(i));
        std::vector<float> f(w.frameCount);
        const auto* data = static_cast<const short*>(w.data);
        for (unsigned j = 0; j < w.frameCount; ++j) f[j] = static_cast<float>(data[j]) / 32768.0f;
        Stats st = analyze(f.data(), f.size());
        std::printf("sfx %-16s %5.2fs peak %.2f rms %.3f%s\n", kSfxNames[i],
                    static_cast<double>(w.frameCount) / w.sampleRate, st.peak, st.rms,
                    st.finite ? "" : "  NON-FINITE!");
        const std::string path = out + "/sfx_" + kSfxNames[i] + ".wav";
        if (!ExportWave(w, path.c_str())) {
            std::fprintf(stderr, "failed to write %s\n", path.c_str());
            UnloadWave(w);
            return 1;
        }
        UnloadWave(w);
    }

    // Music: 24 s per biome mode, intensity ramping from calm to combat.
    for (int mode = 0; mode < MusicSynth::kModeCount; ++mode) {
        MusicSynth synth;
        synth.setVolume(0.7f);
        const int seconds = 24;
        std::vector<float> buffer(static_cast<size_t>(seconds) * MusicSynth::kSampleRate * 2);
        const unsigned chunk = 4096;
        for (size_t frame = 0; frame < buffer.size() / 2; frame += chunk) {
            float t = static_cast<float>(frame) / static_cast<float>(buffer.size() / 2);
            synth.setMood(45 + mode, mode, t > 0.5f ? 1.0f : 0.1f);
            synth.setAmbient(mode == 3 ? 0.5f : 0.0f, mode == 1 ? 0.4f : 0.0f, mode == 3 ? 0.6f : 0.0f);
            unsigned n = static_cast<unsigned>(std::min<size_t>(chunk, buffer.size() / 2 - frame));
            synth.render(buffer.data() + frame * 2, n);
        }
        Stats st = analyze(buffer.data(), buffer.size());
        std::printf("music mode %d: peak %.2f rms %.3f%s\n", mode, st.peak, st.rms, st.finite ? "" : "  NON-FINITE!");
        std::vector<short> pcm(buffer.size());
        for (size_t i = 0; i < buffer.size(); ++i) pcm[i] = static_cast<short>(std::clamp(buffer[i], -1.0f, 1.0f) * 32767.0f);
        Wave w{static_cast<unsigned>(buffer.size() / 2), MusicSynth::kSampleRate, 16, 2, pcm.data()};
        const std::string path = out + "/music_mode" + std::to_string(mode) + ".wav";
        if (!ExportWave(w, path.c_str())) {
            std::fprintf(stderr, "failed to write %s\n", path.c_str());
            return 1;
        }
    }
    return 0;
}
