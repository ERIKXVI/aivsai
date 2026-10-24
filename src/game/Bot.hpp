#pragma once
// A simple autopilot used for automated play-testing (--autoplay): it navigates towards the
// exit portal with a flow field, fights what it sees, browses the sanctum and digs when
// stuck.

#include "game/Input.hpp"
#include "gen/Navigation.hpp"

namespace pm {

class Level;

class Bot {
public:
    PlayerInput think(const Level& level);
    /// True when the bot has made no progress for a long time (tests may warp it ahead).
    bool hopelesslyStuck() const { return noProgress_ > 25.0f; }
    void clearStuck() { noProgress_ = 0.0f; bestDistance_ = 1 << 30; }

private:
    NavGrid nav_;
    int replanTimer_ = 0;
    float stuckTimer_ = 0.0f;
    float noProgress_ = 0.0f;
    int bestDistance_ = 1 << 30;
    float jumpHold_ = 0.0f;
    int digTicks_ = 0;
    int tick_ = 0;
};

} // namespace pm
