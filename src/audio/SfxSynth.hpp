#pragma once
// Procedural sound-effect synthesis in the spirit of sfxr: oscillators with pitch sweeps,
// envelopes, noise and filters, layered per effect. No audio files ship with the game.

#include "audio/Audio.hpp"
#include "raylib.h"

namespace pm {

constexpr int kSfxSampleRate = 44100;

/// Renders the given effect into a mono 16-bit wave (caller unloads it).
Wave synthesizeSfx(Sfx sfx);

} // namespace pm
