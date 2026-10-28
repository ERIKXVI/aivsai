#pragma once
// A single simulated pixel. Kept tiny (6 bytes) because the world holds millions of them.

#include <cstdint>

namespace pm {

using MatId = uint8_t;

namespace cellflag {
constexpr uint8_t kMoved = 1u << 0;      ///< moved ahead of the scan this tick; skip once
constexpr uint8_t kBurning = 1u << 1;    ///< flammable cell currently on fire
constexpr uint8_t kFlowRight = 1u << 2;  ///< liquid sideways-flow memory (reduces jitter)
constexpr uint8_t kElectric = 1u << 3;   ///< conductive cell carrying a charge this tick
constexpr uint8_t kRime = 1u << 4;       ///< laid by a solid spell trail; the alchemist's spells pass through it
constexpr uint8_t kPlayerMade = 1u << 5; ///< spawned by a player spell; a reaction product keeps the mark of the cell it replaces (either's, when both reagents are used up)
} // namespace cellflag

struct Cell {
    MatId mat = 0;     ///< material id (see sim/Material.hpp)
    uint8_t var = 0;   ///< per-cell variation: palette shade and behaviour jitter
    uint8_t life = 0;  ///< generic countdown: gas lifetime, burn time, charge time...
    uint8_t flags = 0; ///< cellflag bits
    int8_t vx = 0;     ///< powders: sideways momentum; liquids: restlessness counter
    int8_t vy = 0;     ///< vertical velocity in 1/8 cells/tick (falling)

    constexpr bool empty() const { return mat == 0; }
    constexpr bool burning() const { return (flags & cellflag::kBurning) != 0; }
};

static_assert(sizeof(Cell) == 6, "Cell layout should stay compact");

} // namespace pm
