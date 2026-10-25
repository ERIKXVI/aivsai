#include "audio/SfxSynth.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/Math.hpp"
#include "core/Rng.hpp"

namespace pm {

namespace {

enum class Osc { Square, Saw, Sine, Triangle, Noise };

/// One synthesized layer. Frequencies sweep exponentially from start to end.
struct Layer {
    Osc osc = Osc::Square;
    float start = 0.0f;        ///< seconds offset into the effect
    float length = 0.2f;       ///< seconds
    float freqA = 440.0f, freqB = 440.0f;
    float attack = 0.005f;     ///< seconds
    float decayPower = 1.5f;   ///< envelope curve after the attack
    float volume = 0.5f;
    float duty = 0.5f;
    float vibratoHz = 0.0f, vibratoDepth = 0.0f; ///< depth as a frequency ratio
    float lowpassA = 1.0f, lowpassB = 1.0f;      ///< one-pole coefficients (1 = open)
    float highpass = 0.0f;                       ///< one-pole highpass coefficient
    float crackle = 0.0f;                        ///< random amplitude gating amount
    float stepAt = -1.0f;                        ///< pitch jumps to freqB at this fraction
};

class Synth {
public:
    explicit Synth(float seconds) : buffer_(static_cast<size_t>(seconds * kSfxSampleRate), 0.0f) {}

    void add(const Layer& l) {
        const int begin = static_cast<int>(l.start * kSfxSampleRate);
        const int count = static_cast<int>(l.length * kSfxSampleRate);
        float phase = 0.0f, lp = 0.0f, hpPrev = 0.0f, hpOut = 0.0f;
        float noiseValue = 0.0f, noisePhase = 0.0f;
        float crackleGain = 1.0f;
        for (int i = 0; i < count; ++i) {
            const int idx = begin + i;
            if (idx < 0 || idx >= static_cast<int>(buffer_.size())) break;
            const float t = static_cast<float>(i) / static_cast<float>(count);
            const float time = static_cast<float>(i) / kSfxSampleRate;

            float freq = l.stepAt >= 0.0f ? (t < l.stepAt ? l.freqA : l.freqB)
                                          : l.freqA * std::pow(l.freqB / l.freqA, t);
            if (l.vibratoHz > 0.0f) freq *= 1.0f + std::sin(time * kTau * l.vibratoHz) * l.vibratoDepth;
            phase += freq / kSfxSampleRate;
            phase -= std::floor(phase);

            float s = 0.0f;
            switch (l.osc) {
            case Osc::Square: s = phase < l.duty ? 1.0f : -1.0f; break;
            case Osc::Saw: s = phase * 2.0f - 1.0f; break;
            case Osc::Sine: s = std::sin(phase * kTau); break;
            case Osc::Triangle: s = 1.0f - 4.0f * std::fabs(phase - 0.5f); break;
            case Osc::Noise:
                noisePhase += freq / kSfxSampleRate;
                if (noisePhase >= 1.0f) {
                    noisePhase -= std::floor(noisePhase);
                    noiseValue = rng_.range(-1.0f, 1.0f);
                }
                s = noiseValue;
                break;
            }

            float cutoff = lerp(l.lowpassA, l.lowpassB, t);
            lp += (s - lp) * cutoff;
            s = lp;
            if (l.highpass > 0.0f) {
                hpOut = l.highpass * (hpOut + s - hpPrev);
                hpPrev = s;
                s = hpOut;
            }
            if (l.crackle > 0.0f && (i % 220) == 0) crackleGain = rng_.chance(l.crackle) ? rng_.range(0.0f, 0.3f) : 1.0f;

            const float attackT = l.attack * kSfxSampleRate;
            float env = i < attackT ? static_cast<float>(i) / attackT : std::pow(1.0f - t, l.decayPower);
            buffer_[static_cast<size_t>(idx)] += s * env * l.volume * crackleGain;
        }
    }

    Wave finish() {
        // Soft clip and convert to 16-bit.
        Wave w{};
        w.frameCount = static_cast<unsigned int>(buffer_.size());
        w.sampleRate = kSfxSampleRate;
        w.sampleSize = 16;
        w.channels = 1;
        auto* data = static_cast<short*>(std::malloc(buffer_.size() * sizeof(short)));
        for (size_t i = 0; i < buffer_.size(); ++i) {
            float v = std::tanh(buffer_[i] * 1.2f);
            data[i] = static_cast<short>(v * 30000.0f);
        }
        w.data = data;
        return w;
    }

private:
    std::vector<float> buffer_;
    Rng rng_{0x5EEDu};
};

Layer tone(Osc osc, float len, float fa, float fb, float vol) {
    Layer l;
    l.osc = osc;
    l.length = len;
    l.freqA = fa;
    l.freqB = fb;
    l.volume = vol;
    return l;
}

Wave build(Sfx sfx) {
    switch (sfx) {
    case Sfx::CastSpark: {
        Synth s(0.12f);
        Layer a = tone(Osc::Square, 0.1f, 1400, 2200, 0.25f);
        a.duty = 0.3f;
        a.decayPower = 2.5f;
        a.lowpassA = a.lowpassB = 0.5f;
        s.add(a);
        Layer n = tone(Osc::Noise, 0.05f, 8000, 4000, 0.12f);
        s.add(n);
        return s.finish();
    }
    case Sfx::CastBolt: {
        Synth s(0.2f);
        Layer a = tone(Osc::Saw, 0.18f, 900, 260, 0.3f);
        a.lowpassA = 0.6f;
        a.lowpassB = 0.15f;
        a.decayPower = 2.0f;
        s.add(a);
        return s.finish();
    }
    case Sfx::CastFire: {
        Synth s(0.35f);
        Layer n = tone(Osc::Noise, 0.32f, 3000, 900, 0.5f);
        n.attack = 0.04f;
        n.lowpassA = 0.25f;
        n.lowpassB = 0.05f;
        s.add(n);
        Layer w = tone(Osc::Sine, 0.25f, 260, 110, 0.3f);
        s.add(w);
        return s.finish();
    }
    case Sfx::CastHeavy: {
        Synth s(0.4f);
        Layer a = tone(Osc::Saw, 0.35f, 220, 55, 0.4f);
        a.lowpassA = 0.3f;
        a.lowpassB = 0.05f;
        s.add(a);
        Layer n = tone(Osc::Noise, 0.12f, 2000, 500, 0.25f);
        n.lowpassA = n.lowpassB = 0.2f;
        s.add(n);
        return s.finish();
    }
    case Sfx::CastMagic: {
        Synth s(0.3f);
        Layer a = tone(Osc::Sine, 0.28f, 660, 1320, 0.3f);
        a.vibratoHz = 18.0f;
        a.vibratoDepth = 0.04f;
        s.add(a);
        Layer b = tone(Osc::Triangle, 0.2f, 990, 1980, 0.15f);
        b.start = 0.04f;
        s.add(b);
        return s.finish();
    }
    case Sfx::CastLightning: {
        Synth s(0.45f);
        Layer n = tone(Osc::Noise, 0.42f, 12000, 3000, 0.6f);
        n.crackle = 0.4f;
        n.highpass = 0.7f;
        n.decayPower = 1.2f;
        s.add(n);
        Layer z = tone(Osc::Saw, 0.3f, 120, 60, 0.3f);
        z.crackle = 0.3f;
        s.add(z);
        return s.finish();
    }
    case Sfx::Dig: {
        Synth s(0.1f);
        Layer n = tone(Osc::Noise, 0.08f, 2500, 1200, 0.35f);
        n.lowpassA = n.lowpassB = 0.3f;
        n.decayPower = 2.0f;
        s.add(n);
        return s.finish();
    }
    case Sfx::ExplosionSmall:
    case Sfx::ExplosionBig: {
        const bool big = sfx == Sfx::ExplosionBig;
        const float len = big ? 1.3f : 0.55f;
        Synth s(len);
        Layer n = tone(Osc::Noise, len, big ? 1800.0f : 3000.0f, 200, big ? 0.9f : 0.7f);
        n.attack = 0.002f;
        n.lowpassA = big ? 0.35f : 0.5f;
        n.lowpassB = 0.02f;
        n.decayPower = big ? 2.2f : 2.6f;
        s.add(n);
        Layer thump = tone(Osc::Sine, len * 0.7f, big ? 110.0f : 160.0f, 32, big ? 0.9f : 0.6f);
        thump.decayPower = 1.8f;
        s.add(thump);
        Layer crack = tone(Osc::Noise, 0.08f, 9000, 6000, 0.35f);
        s.add(crack);
        return s.finish();
    }
    case Sfx::Hit: {
        Synth s(0.08f);
        Layer a = tone(Osc::Square, 0.07f, 320, 120, 0.3f);
        a.decayPower = 2.0f;
        s.add(a);
        Layer n = tone(Osc::Noise, 0.04f, 5000, 2000, 0.2f);
        s.add(n);
        return s.finish();
    }
    case Sfx::EnemyHurt: {
        Synth s(0.14f);
        Layer a = tone(Osc::Saw, 0.12f, 520, 260, 0.28f);
        a.lowpassA = a.lowpassB = 0.35f;
        a.vibratoHz = 40.0f;
        a.vibratoDepth = 0.1f;
        s.add(a);
        return s.finish();
    }
    case Sfx::EnemyDie: {
        Synth s(0.45f);
        Layer a = tone(Osc::Saw, 0.4f, 360, 50, 0.32f);
        a.lowpassA = 0.4f;
        a.lowpassB = 0.08f;
        a.vibratoHz = 25.0f;
        a.vibratoDepth = 0.08f;
        s.add(a);
        Layer n = tone(Osc::Noise, 0.3f, 1500, 300, 0.3f);
        n.lowpassA = n.lowpassB = 0.2f;
        s.add(n);
        return s.finish();
    }
    case Sfx::PlayerHurt: {
        Synth s(0.25f);
        Layer a = tone(Osc::Square, 0.22f, 280, 110, 0.32f);
        a.duty = 0.35f;
        a.vibratoHz = 30.0f;
        a.vibratoDepth = 0.12f;
        a.lowpassA = a.lowpassB = 0.4f;
        s.add(a);
        return s.finish();
    }
    case Sfx::PlayerDie: {
        Synth s(1.4f);
        Layer a = tone(Osc::Saw, 1.3f, 330, 35, 0.4f);
        a.vibratoHz = 6.0f;
        a.vibratoDepth = 0.06f;
        a.lowpassA = 0.4f;
        a.lowpassB = 0.05f;
        a.decayPower = 1.0f;
        s.add(a);
        Layer b = tone(Osc::Sine, 1.2f, 220, 30, 0.3f);
        b.start = 0.1f;
        s.add(b);
        return s.finish();
    }
    case Sfx::Jump: {
        Synth s(0.1f);
        Layer a = tone(Osc::Square, 0.09f, 260, 520, 0.15f);
        a.duty = 0.25f;
        a.lowpassA = a.lowpassB = 0.3f;
        s.add(a);
        return s.finish();
    }
    case Sfx::Land: {
        Synth s(0.08f);
        Layer n = tone(Osc::Noise, 0.07f, 900, 300, 0.25f);
        n.lowpassA = n.lowpassB = 0.15f;
        n.decayPower = 2.5f;
        s.add(n);
        return s.finish();
    }
    case Sfx::Splash: {
        Synth s(0.35f);
        Layer n = tone(Osc::Noise, 0.32f, 6000, 1500, 0.35f);
        n.highpass = 0.6f;
        n.lowpassA = 0.6f;
        n.lowpassB = 0.2f;
        s.add(n);
        Layer b = tone(Osc::Sine, 0.12f, 500, 900, 0.15f);
        s.add(b);
        return s.finish();
    }
    case Sfx::Pickup: {
        Synth s(0.18f);
        Layer a = tone(Osc::Sine, 0.16f, 880, 1320, 0.3f);
        a.stepAt = 0.35f;
        s.add(a);
        return s.finish();
    }
    case Sfx::Coin: {
        Synth s(0.22f);
        Layer a = tone(Osc::Square, 0.2f, 1046, 1568, 0.18f);
        a.stepAt = 0.25f;
        a.duty = 0.5f;
        a.lowpassA = a.lowpassB = 0.5f;
        s.add(a);
        return s.finish();
    }
    case Sfx::Potion: {
        Synth s(0.35f);
        for (int i = 0; i < 4; ++i) {
            Layer b = tone(Osc::Sine, 0.07f, 400.0f + i * 90.0f, 900.0f + i * 140.0f, 0.2f);
            b.start = i * 0.07f;
            s.add(b);
        }
        return s.finish();
    }
    case Sfx::Drink: {
        Synth s(0.5f);
        for (int i = 0; i < 3; ++i) {
            Layer g = tone(Osc::Sine, 0.12f, 340, 180, 0.35f);
            g.start = i * 0.15f;
            s.add(g);
        }
        return s.finish();
    }
    case Sfx::ChestOpen: {
        Synth s(0.7f);
        Layer creak = tone(Osc::Saw, 0.3f, 90, 160, 0.2f);
        creak.lowpassA = creak.lowpassB = 0.2f;
        creak.vibratoHz = 22.0f;
        creak.vibratoDepth = 0.15f;
        s.add(creak);
        const float notes[] = {784.0f, 988.0f, 1175.0f, 1568.0f};
        for (int i = 0; i < 4; ++i) {
            Layer c = tone(Osc::Triangle, 0.3f, notes[i], notes[i], 0.18f);
            c.start = 0.25f + i * 0.08f;
            s.add(c);
        }
        return s.finish();
    }
    case Sfx::Portal: {
        Synth s(1.4f);
        Layer a = tone(Osc::Sine, 1.3f, 160, 1300, 0.3f);
        a.attack = 0.3f;
        a.vibratoHz = 7.0f;
        a.vibratoDepth = 0.05f;
        a.decayPower = 0.8f;
        s.add(a);
        Layer b = tone(Osc::Triangle, 1.2f, 240, 1950, 0.15f);
        b.attack = 0.3f;
        b.start = 0.1f;
        s.add(b);
        Layer n = tone(Osc::Noise, 1.2f, 3000, 8000, 0.08f);
        n.attack = 0.5f;
        n.highpass = 0.8f;
        s.add(n);
        return s.finish();
    }
    case Sfx::Perk: {
        Synth s(1.0f);
        const float notes[] = {523.0f, 659.0f, 784.0f, 1046.0f, 1318.0f};
        for (int i = 0; i < 5; ++i) {
            Layer c = tone(Osc::Triangle, 0.6f, notes[i], notes[i], 0.16f);
            c.start = i * 0.07f;
            c.vibratoHz = 5.0f;
            c.vibratoDepth = 0.01f;
            s.add(c);
        }
        return s.finish();
    }
    case Sfx::UiClick: {
        Synth s(0.04f);
        Layer a = tone(Osc::Square, 0.035f, 1300, 900, 0.15f);
        a.lowpassA = a.lowpassB = 0.4f;
        s.add(a);
        return s.finish();
    }
    case Sfx::UiHover: {
        Synth s(0.03f);
        Layer a = tone(Osc::Sine, 0.025f, 1100, 1300, 0.08f);
        s.add(a);
        return s.finish();
    }
    case Sfx::UiDeny: {
        Synth s(0.2f);
        for (int i = 0; i < 2; ++i) {
            Layer a = tone(Osc::Square, 0.07f, 190, 170, 0.2f);
            a.start = i * 0.09f;
            a.lowpassA = a.lowpassB = 0.3f;
            s.add(a);
        }
        return s.finish();
    }
    case Sfx::Sizzle: {
        Synth s(0.5f);
        Layer n = tone(Osc::Noise, 0.48f, 12000, 9000, 0.25f);
        n.highpass = 0.85f;
        n.crackle = 0.25f;
        n.attack = 0.03f;
        s.add(n);
        return s.finish();
    }
    case Sfx::Freeze: {
        Synth s(0.4f);
        Layer a = tone(Osc::Sine, 0.35f, 2100, 3200, 0.15f);
        a.vibratoHz = 30.0f;
        a.vibratoDepth = 0.03f;
        s.add(a);
        Layer n = tone(Osc::Noise, 0.25f, 10000, 6000, 0.12f);
        n.highpass = 0.8f;
        s.add(n);
        return s.finish();
    }
    case Sfx::Squish: {
        Synth s(0.16f);
        Layer n = tone(Osc::Noise, 0.14f, 1200, 400, 0.3f);
        n.lowpassA = n.lowpassB = 0.12f;
        s.add(n);
        Layer b = tone(Osc::Sine, 0.12f, 180, 90, 0.3f);
        s.add(b);
        return s.finish();
    }
    case Sfx::Rumble: {
        // The mountain shifting: a long, low growl of grinding stone.
        Synth s(2.0f);
        Layer n = tone(Osc::Noise, 1.9f, 420, 110, 0.7f);
        n.attack = 0.4f;
        n.lowpassA = n.lowpassB = 0.04f;
        n.decayPower = 1.2f;
        s.add(n);
        Layer low = tone(Osc::Sine, 1.8f, 48, 34, 0.6f);
        low.attack = 0.3f;
        low.vibratoHz = 7.0f;
        low.vibratoDepth = 0.15f;
        s.add(low);
        return s.finish();
    }
    case Sfx::Roar: {
        Synth s(1.5f);
        Layer a = tone(Osc::Saw, 1.4f, 95, 55, 0.5f);
        a.attack = 0.15f;
        a.vibratoHz = 9.0f;
        a.vibratoDepth = 0.08f;
        a.lowpassA = 0.25f;
        a.lowpassB = 0.08f;
        a.decayPower = 1.0f;
        s.add(a);
        Layer n = tone(Osc::Noise, 1.3f, 900, 300, 0.4f);
        n.attack = 0.1f;
        n.lowpassA = n.lowpassB = 0.1f;
        s.add(n);
        return s.finish();
    }
    case Sfx::Heartbeat: {
        // Lub-dub: two low thumps.
        Synth s(0.45f);
        Layer lub = tone(Osc::Sine, 0.14f, 70, 42, 0.55f);
        lub.decayPower = 2.5f;
        s.add(lub);
        Layer dub = tone(Osc::Sine, 0.12f, 62, 38, 0.4f);
        dub.start = 0.2f;
        dub.decayPower = 2.5f;
        s.add(dub);
        return s.finish();
    }
    case Sfx::Count: break;
    }
    return Wave{};
}

} // namespace

Wave synthesizeSfx(Sfx sfx) { return build(sfx); }

} // namespace pm
