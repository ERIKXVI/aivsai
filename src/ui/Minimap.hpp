#pragma once
// The alchemist's map: a fog-of-war picture of the level at one pixel per 4x4 cells,
// revealed around the player as they explore. Shown as a small corner view and as a
// full-screen map with markers for the portal, vaults and transmutation circles.

#include <vector>

#include "core/Math.hpp"
#include "raylib.h"

namespace pm {

class Level;

class Minimap {
public:
    static constexpr int kBlock = 4; ///< world cells per map pixel

    Minimap() = default;
    ~Minimap();
    Minimap(const Minimap&) = delete;
    Minimap& operator=(const Minimap&) = delete;

    /// Starts a fresh, unexplored map for a new level (needs the GL context).
    void reset(const Level& level);
    /// Reveals the surroundings of the player; call once per tick.
    void update(const Level& level);
    /// Reveals the whole level (testing).
    void revealAll(const Level& level);
    /// Small map anchored at the top-right corner below `top` (screen pixels).
    void drawCorner(const Level& level, float screenW, float top, float time) const;
    /// Full-screen map overlay; `gamepad` picks the closing prompt.
    void drawFull(const Level& level, float screenW, float screenH, float time, bool gamepad) const;

private:
    bool explored(int bx, int by) const;
    void sample(const Level& level, int bx, int by);
    bool exploredNear(Vec2 worldPos) const;
    void drawMarkers(const Level& level, Rectangle dest, Rectangle src, float time, float markerSize) const;

    int bw_ = 0, bh_ = 0;
    std::vector<uint8_t> explored_;
    std::vector<Color> pixels_;
    std::vector<Color> scratch_;
    Texture2D texture_{};
    int tick_ = 0;
};

} // namespace pm
