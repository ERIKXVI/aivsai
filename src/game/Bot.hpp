#pragma once
// A simple autopilot used for automated play-testing (--autoplay) and the attract mode:
// it follows the level's main path, fights what it sees and digs when stuck.

#include "game/Input.hpp"

namespace pm {

class Level;

class Bot {
public:
    PlayerInput think(const Level& level);

private:
    size_t waypoint_ = 1;
    float stuckTimer_ = 0.0f;
    float lastProgress_ = 1e9f;
    float jumpHold_ = 0.0f;
    int digTicks_ = 0;
    int tick_ = 0;
};

} // namespace pm
