#include "audio/Music.hpp"

#include <array>
#include <cmath>
#include <vector>

#include "core/Math.hpp"
#include "core/Rng.hpp"

namespace pm {

namespace {

constexpr int kRate = MusicSynth::kSampleRate;
constexpr int kModes = MusicSynth::kModeCount;

// Phrygian, Lydian, Dorian, Harmonic minor, Phrygian dominant, Ionian.
constexpr int kScales[kModes][7] = {
    {0, 1, 3, 5, 7, 8, 10}, {0, 2, 4, 6, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10},
    {0, 2, 3, 5, 7, 8, 11}, {0, 1, 4, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 9, 11},
};
// Chord roots as scale degrees; each mode gets a progression that suits its colour.
constexpr int kProgressions[kModes][4] = {
    {0, 1, 0, 6}, {0, 1, 4, 0}, {0, 3, 0, 4}, {0, 5, 3, 4}, {0, 1, 3, 1}, {0, 5, 3, 4},
};

std::atomic<MusicEngine*> g_engine{nullptr};

float midiToHz(float note) { return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f); }

int scaleNote(int root, int mode, int degree) {
    int octave = degree >= 0 ? degree / 7 : (degree - 6) / 7;
    int d = degree - octave * 7;
    return root + octave * 12 + kScales[mode][d];
}

/// Chamberlin state-variable lowpass.
struct SvFilter {
    float low = 0.0f, band = 0.0f;
    float process(float x, float cutoffHz, float q) {
        float f = 2.0f * std::sin(kPi * std::min(cutoffHz, 8000.0f) / kRate);
        low += f * band;
        float high = x - low - q * band;
        band += f * high;
        return low;
    }
};

struct Pluck {
    std::array<float, 2048> buf{};
    int len = 0, pos = 0;
    float pan = 0.5f;
    float damping = 0.996f;
    bool active = false;
    int life = 0;

    void trigger(float hz, float velocity, float panPos, Rng& rng) {
        len = clamp(static_cast<int>(kRate / hz), 2, 2047);
        for (int i = 0; i < len; ++i) buf[static_cast<size_t>(i)] = rng.range(-1.0f, 1.0f) * velocity;
        // Soften the excitation for a rounder, harp-like tone.
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 1; i < len; ++i) buf[static_cast<size_t>(i)] = 0.5f * (buf[static_cast<size_t>(i)] + buf[static_cast<size_t>(i - 1)]);
        pos = 0;
        pan = panPos;
        active = true;
        life = kRate * 4;
    }
    float tick() {
        if (!active) return 0.0f;
        float a = buf[static_cast<size_t>(pos)];
        int next = pos + 1 == len ? 0 : pos + 1;
        buf[static_cast<size_t>(pos)] = (a + buf[static_cast<size_t>(next)]) * 0.5f * damping;
        pos = next;
        if (--life <= 0) active = false;
        return a;
    }
};

struct Comb {
    std::vector<float> buf;
    size_t pos = 0;
    float feedback = 0.8f, damp = 0.3f, store = 0.0f;
    explicit Comb(int size = 1) : buf(static_cast<size_t>(size), 0.0f) {}
    float process(float x) {
        float y = buf[pos];
        store = y * (1.0f - damp) + store * damp;
        buf[pos] = x + store * feedback;
        if (++pos == buf.size()) pos = 0;
        return y;
    }
};

struct Allpass {
    std::vector<float> buf;
    size_t pos = 0;
    explicit Allpass(int size = 1) : buf(static_cast<size_t>(size), 0.0f) {}
    float process(float x) {
        float b = buf[pos];
        float y = -x + b;
        buf[pos] = x + b * 0.5f;
        if (++pos == buf.size()) pos = 0;
        return y;
    }
};

} // namespace

struct MusicSynth::State {
    Rng rng{0xC0FFEEu};
    int samplesPerStep = kRate * 60 / 80 / 4;
    int stepCounter = 0;
    int step = -1;
    int chordIndex = 0;
    int root = 45, mode = 0;
    float intensity = 0.0f;
    float fire = 0.0f, water = 0.0f, lava = 0.0f;

    std::array<float, 3> padFreq{110.0f, 138.0f, 165.0f};
    std::array<float, 3> padTarget{110.0f, 138.0f, 165.0f};
    std::array<float, 6> padPhase{};
    SvFilter padFilterL, padFilterR;
    float lfoPhase = 0.0f;
    float dronePhase = 0.0f, droneFreq = 55.0f;

    float bassPhase = 0.0f, bassFreq = 55.0f, bassEnv = 0.0f;

    std::array<Pluck, 8> plucks;
    int nextPluck = 0;
    int melody = 7;

    float kickPhase = 0.0f, kickEnv = 0.0f, kickFreq = 0.0f;
    float hatEnv = 0.0f, hatLp = 0.0f;

    std::vector<float> delayL = std::vector<float>(kRate, 0.0f), delayR = std::vector<float>(kRate, 0.0f);
    size_t delayPos = 0;
    size_t delayLen = static_cast<size_t>(kRate * 3 / 8);

    std::array<Comb, 4> combL{Comb(1557), Comb(1617), Comb(1491), Comb(1422)};
    std::array<Comb, 4> combR{Comb(1580), Comb(1640), Comb(1514), Comb(1445)};
    std::array<Allpass, 2> apL{Allpass(556), Allpass(441)};
    std::array<Allpass, 2> apR{Allpass(579), Allpass(464)};

    float fireLp = 0.0f, crackle = 0.0f, waterLp = 0.0f, waterLp2 = 0.0f, waterMod = 0.0f, lavaLp = 0.0f;

    void setChord() {
        const int degree = kProgressions[mode][chordIndex % 4];
        for (int v = 0; v < 3; ++v) padTarget[static_cast<size_t>(v)] = midiToHz(static_cast<float>(scaleNote(root + 12, mode, degree + v * 2)));
        droneFreq = midiToHz(static_cast<float>(root - 12));
    }

    void triggerPluck(int degree, float velocity) {
        Pluck& p = plucks[static_cast<size_t>(nextPluck)];
        nextPluck = (nextPluck + 1) % static_cast<int>(plucks.size());
        p.damping = 0.995f + rng.range(0.0f, 0.003f);
        p.trigger(midiToHz(static_cast<float>(scaleNote(root + 12, mode, degree))), velocity, rng.range(0.25f, 0.75f), rng);
    }

    void onStep() {
        step = (step + 1) % 32;
        if (step == 0) {
            ++chordIndex;
            setChord();
        }
        const int chordDegree = kProgressions[mode][((chordIndex % 4) + 4) % 4];
        if (step % 8 == 0 || (intensity > 0.55f && step % 8 == 6)) {
            bassFreq = midiToHz(static_cast<float>(scaleNote(root - 12, mode, chordDegree)));
            bassEnv = 0.9f;
        }
        // Improvised melody: a random walk through the scale that prefers chord tones.
        float pluckChance = 0.14f + 0.45f * intensity;
        if (step % 2 == 0 && rng.chance(pluckChance)) {
            melody += rng.rangeInt(-2, 2);
            melody = clamp(melody, 3, 16);
            int rel = (melody - chordDegree) % 7;
            if (rel < 0) rel += 7;
            if (rel % 2 == 1 && rng.chance(0.6f)) melody += rng.chance(0.5f) ? 1 : -1;
            triggerPluck(melody, 0.35f + 0.25f * intensity);
            if (rng.chance(0.15f)) triggerPluck(melody + 7, 0.15f);
        }
        if (intensity > 0.3f && (step % 8 == 0 || (intensity > 0.7f && (step == 6 || step == 22)))) {
            kickEnv = 1.0f;
            kickPhase = 0.0f;
            kickFreq = 130.0f;
        }
        if (intensity > 0.5f && step % 4 == 2) hatEnv = 0.6f;
    }
};

MusicSynth::MusicSynth() : state_(std::make_unique<State>()) { state_->setChord(); }

MusicSynth::~MusicSynth() = default;

void MusicSynth::setMood(int root, int mode, float intensity) {
    root_.store(root);
    mode_.store(clamp(mode, 0, kModes - 1));
    intensity_.store(clamp(intensity, 0.0f, 1.0f));
}

void MusicSynth::setAmbient(float fire, float water, float lava) {
    fire_.store(clamp(fire, 0.0f, 1.0f));
    water_.store(clamp(water, 0.0f, 1.0f));
    lava_.store(clamp(lava, 0.0f, 1.0f));
}

MusicEngine::MusicEngine() {
    SetAudioStreamBufferSizeDefault(4096);
    stream_ = LoadAudioStream(kRate, 32, 2);
    if (!IsAudioStreamValid(stream_)) return;
    g_engine.store(this);
    SetAudioStreamCallback(stream_, &MusicEngine::streamCallback);
    PlayAudioStream(stream_);
    started_ = true;
}

MusicEngine::~MusicEngine() {
    if (started_) {
        StopAudioStream(stream_);
        g_engine.store(nullptr);
        UnloadAudioStream(stream_);
    }
}

void MusicEngine::streamCallback(void* buffer, unsigned int frames) {
    MusicEngine* e = g_engine.load();
    auto* out = static_cast<float*>(buffer);
    if (!e) {
        for (unsigned i = 0; i < frames * 2; ++i) out[i] = 0.0f;
        return;
    }
    e->synth_.render(out, frames);
}

void MusicSynth::render(float* out, unsigned int frames) {
    State& s = *state_;
    const float volume = volume_.load();
    const int wantRoot = root_.load(), wantMode = mode_.load();
    const float wantIntensity = intensity_.load();
    const float wantFire = fire_.load(), wantWater = water_.load(), wantLava = lava_.load();

    for (unsigned int i = 0; i < frames; ++i) {
        if (--s.stepCounter <= 0) {
            // Mood changes land on the next bar line so transitions stay musical.
            if ((s.step + 1) % 16 == 0 && (wantRoot != s.root || wantMode != s.mode)) {
                s.root = wantRoot;
                s.mode = wantMode;
                // Restart the phrase so the next step is a downbeat that sets the new chord.
                s.chordIndex = -1;
                s.step = -1;
            }
            s.onStep();
            s.stepCounter = s.samplesPerStep;
        }
        s.intensity += (wantIntensity - s.intensity) * 0.00002f;
        s.fire += (wantFire - s.fire) * 0.0002f;
        s.water += (wantWater - s.water) * 0.0002f;
        s.lava += (wantLava - s.lava) * 0.0002f;

        // Pad: three chord tones, two detuned saws each (left/right).
        float padL = 0.0f, padR = 0.0f;
        for (int v = 0; v < 3; ++v) {
            float& f = s.padFreq[static_cast<size_t>(v)];
            f += (s.padTarget[static_cast<size_t>(v)] - f) * 0.0004f;
            for (int d = 0; d < 2; ++d) {
                float& ph = s.padPhase[static_cast<size_t>(v * 2 + d)];
                ph += f * (d ? 1.0035f : 0.9965f) / kRate;
                ph -= std::floor(ph);
                (d ? padR : padL) += ph * 2.0f - 1.0f;
            }
        }
        s.lfoPhase += 0.07f / kRate;
        if (s.lfoPhase > 1.0f) s.lfoPhase -= 1.0f;
        float cutoff = 380.0f + 420.0f * (0.5f + 0.5f * std::sin(s.lfoPhase * kTau)) + s.intensity * 700.0f;
        padL = s.padFilterL.process(padL, cutoff, 0.6f) * 0.045f;
        padR = s.padFilterR.process(padR, cutoff * 1.03f, 0.6f) * 0.045f;

        s.dronePhase += s.droneFreq / kRate;
        s.dronePhase -= std::floor(s.dronePhase);
        float drone = std::sin(s.dronePhase * kTau) * 0.045f * (0.8f + 0.2f * std::sin(s.lfoPhase * kTau * 3.0f));

        s.bassPhase += s.bassFreq / kRate;
        s.bassPhase -= std::floor(s.bassPhase);
        float bass = (1.0f - 4.0f * std::fabs(s.bassPhase - 0.5f)) * s.bassEnv * 0.16f;
        s.bassEnv *= 0.99985f;

        float pluckL = 0.0f, pluckR = 0.0f;
        for (Pluck& p : s.plucks) {
            if (!p.active) continue;
            float v = p.tick();
            pluckL += v * (1.0f - p.pan);
            pluckR += v * p.pan;
        }
        pluckL *= 0.5f;
        pluckR *= 0.5f;

        float drums = 0.0f;
        if (s.kickEnv > 0.001f) {
            s.kickFreq = 45.0f + (s.kickFreq - 45.0f) * 0.9993f;
            s.kickPhase += s.kickFreq / kRate;
            s.kickPhase -= std::floor(s.kickPhase);
            drums += std::sin(s.kickPhase * kTau) * s.kickEnv * 0.32f;
            s.kickEnv *= 0.99975f;
        }
        if (s.hatEnv > 0.001f) {
            float n = s.rng.range(-1.0f, 1.0f);
            s.hatLp += (n - s.hatLp) * 0.5f;
            drums += (n - s.hatLp) * s.hatEnv * 0.05f;
            s.hatEnv *= 0.9985f;
        }

        // Ambient beds.
        float amb = 0.0f;
        if (s.fire > 0.01f) {
            if (s.rng.chance(0.0009f * s.fire * 4.0f)) s.crackle = s.rng.range(0.4f, 1.0f);
            float n = s.rng.range(-1.0f, 1.0f);
            s.fireLp += (n - s.fireLp) * 0.08f;
            amb += s.fireLp * s.fire * 0.08f + n * s.crackle * 0.12f * s.fire;
            s.crackle *= 0.993f;
        }
        if (s.water > 0.01f) {
            float n = s.rng.range(-1.0f, 1.0f);
            s.waterMod += s.rng.range(-0.001f, 0.001f);
            s.waterMod = clamp(s.waterMod, 0.02f, 0.25f);
            s.waterLp += (n - s.waterLp) * s.waterMod;
            s.waterLp2 += (s.waterLp - s.waterLp2) * 0.3f;
            amb += (s.waterLp - s.waterLp2) * s.water * 0.5f;
        }
        if (s.lava > 0.01f) {
            float n = s.rng.range(-1.0f, 1.0f);
            s.lavaLp += (n - s.lavaLp) * 0.004f;
            amb += s.lavaLp * s.lava * 0.9f;
        }

        // Echo on the plucks, then a shared cave reverb.
        size_t readPos = (s.delayPos + s.delayL.size() - s.delayLen) % s.delayL.size();
        float echoL = s.delayL[readPos], echoR = s.delayR[readPos];
        s.delayL[s.delayPos] = pluckL + echoR * 0.42f;
        s.delayR[s.delayPos] = pluckR + echoL * 0.42f;
        s.delayPos = (s.delayPos + 1) % s.delayL.size();

        float dryL = padL + drone + bass + pluckL + echoL * 0.5f + drums + amb;
        float dryR = padR + drone + bass + pluckR + echoR * 0.5f + drums + amb;
        float send = (padL + padR) * 0.5f + (pluckL + pluckR) * 0.6f + drone * 0.3f;
        float revL = 0.0f, revR = 0.0f;
        for (size_t c = 0; c < 4; ++c) {
            revL += s.combL[c].process(send);
            revR += s.combR[c].process(send);
        }
        for (size_t a = 0; a < 2; ++a) {
            revL = s.apL[a].process(revL);
            revR = s.apR[a].process(revR);
        }
        float l = (dryL + revL * 0.22f) * volume;
        float r = (dryR + revR * 0.22f) * volume;
        out[i * 2] = std::tanh(l);
        out[i * 2 + 1] = std::tanh(r);
    }
}

} // namespace pm
