#include "audio/Audio.hpp"

#include <cmath>

#include "audio/Music.hpp"
#include "audio/SfxSynth.hpp"

namespace pm {

Audio::Audio() {
    available_ = IsAudioDeviceReady();
    if (!available_) return;
    for (int i = 0; i < static_cast<int>(Sfx::Count); ++i) {
        Wave wave = synthesizeSfx(static_cast<Sfx>(i));
        if (wave.frameCount == 0) continue;
        sounds_[i][0] = LoadSoundFromWave(wave);
        for (int v = 1; v < kVoices; ++v) sounds_[i][v] = LoadSoundAlias(sounds_[i][0]);
        UnloadWave(wave);
    }
    music_ = std::make_unique<MusicEngine>();
}

Audio::~Audio() {
    music_.reset();
    if (!available_ || !IsAudioDeviceReady()) return;
    for (auto& voices : sounds_) {
        for (int v = 1; v < kVoices; ++v)
            if (voices[v].frameCount) UnloadSoundAlias(voices[v]);
        if (voices[0].frameCount) UnloadSound(voices[0]);
    }
}

void Audio::play(Sfx sfx, float volume, float pitch, float pan) {
    if (!available_) return;
    const int i = static_cast<int>(sfx);
    // Avoid machine-gunning: soften a repeat of the same sound within 25 ms.
    constexpr double kRetriggerWindow = 0.025;
    if (time_ - lastPlayed_[i] < kRetriggerWindow) volume *= 0.4f;
    lastPlayed_[i] = time_;
    Sound& s = sounds_[i][nextVoice_[i]];
    nextVoice_[i] = (nextVoice_[i] + 1) % kVoices;
    if (s.frameCount == 0) return;
    SetSoundVolume(s, clamp(volume * sfxVolume_, 0.0f, 1.0f));
    SetSoundPitch(s, clamp(pitch, 0.25f, 4.0f));
    SetSoundPan(s, clamp(pan, 0.0f, 1.0f));
    PlaySound(s);
}

void Audio::playAt(Sfx sfx, Vec2 pos, float volume, float pitch) {
    Vec2 d = pos - listener_;
    float dist = d.length();
    float atten = clamp(1.0f - dist / 420.0f, 0.0f, 1.0f);
    if (atten <= 0.01f) return;
    // raylib pan: 1 = left, 0 = right.
    float pan = clamp(0.5f - d.x / 500.0f, 0.15f, 0.85f);
    play(sfx, volume * atten * atten, pitch, pan);
}

void Audio::setMasterVolume(float v) {
    if (available_) SetMasterVolume(clamp(v, 0.0f, 1.0f));
}

void Audio::setMusicVolume(float v) {
    if (music_) music_->setVolume(v);
}

void Audio::setMusicMood(int root, int mode, float intensity) {
    if (music_) music_->setMood(root, mode, intensity);
}

void Audio::setAmbient(const AmbientLevels& levels) {
    if (music_) music_->setAmbient(levels.fire, levels.water, levels.lava);
}

void Audio::update(float dt) {
    time_ += dt;
}

} // namespace pm
