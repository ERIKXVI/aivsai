#pragma once
// Audio service: procedurally synthesized sound effects with positional panning/volume,
// generative music and ambient material loops. Safe to use when no audio device exists.

#include <array>
#include <atomic>
#include <memory>

#include "core/Math.hpp"
#include "raylib.h"

namespace pm {

enum class Sfx : uint8_t {
    CastSpark,
    CastBolt,
    CastFire,
    CastHeavy,
    CastMagic,
    CastLightning,
    Dig,
    ExplosionSmall,
    ExplosionBig,
    Hit,
    EnemyHurt,
    EnemyDie,
    PlayerHurt,
    PlayerDie,
    Jump,
    Land,
    Splash,
    Pickup,
    Coin,
    Potion,
    Drink,
    ChestOpen,
    Portal,
    Perk,
    UiClick,
    UiHover,
    UiDeny,
    Sizzle,
    Freeze,
    Squish,
    Roar,
    Count
};

class MusicEngine;

struct AmbientLevels {
    float fire = 0.0f;  ///< 0..1 amount of fire near the listener
    float water = 0.0f; ///< flowing liquid
    float lava = 0.0f;
};

class Audio {
public:
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool available() const { return available_; }

    /// Plays a sound; `pitch` is a multiplier, `volume` 0..1.
    void play(Sfx sfx, float volume = 1.0f, float pitch = 1.0f, float pan = 0.5f);
    /// Plays a sound emitted at a world position relative to the listener.
    void playAt(Sfx sfx, Vec2 pos, float volume = 1.0f, float pitch = 1.0f);
    void setListener(Vec2 pos) { listener_ = pos; }

    void setMasterVolume(float v);
    void setSfxVolume(float v) { sfxVolume_ = v; }
    void setMusicVolume(float v);

    /// Music control (biome mood and combat intensity).
    void setMusicMood(int root, int mode, float intensity);
    void setAmbient(const AmbientLevels& levels);
    void update(float dt);

private:
    bool available_ = false;
    float sfxVolume_ = 0.8f;
    Vec2 listener_;
    static constexpr int kVoices = 4; ///< round-robin aliases per sound
    std::array<std::array<Sound, kVoices>, static_cast<int>(Sfx::Count)> sounds_{};
    std::array<int, static_cast<int>(Sfx::Count)> nextVoice_{};
    std::array<float, static_cast<int>(Sfx::Count)> lastPlayed_{};
    double time_ = 0.0;
    std::unique_ptr<MusicEngine> music_;
};

} // namespace pm
