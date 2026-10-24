#pragma once
// Colour conversions shared by the renderers and the UI.

#include <cstdint>

#include "raylib.h"

namespace pm {

/// 0xRRGGBB to a raylib colour.
constexpr Color hexColor(uint32_t hex, uint8_t alpha = 255) {
    return Color{static_cast<unsigned char>((hex >> 16) & 0xFF), static_cast<unsigned char>((hex >> 8) & 0xFF),
                 static_cast<unsigned char>(hex & 0xFF), alpha};
}

} // namespace pm
