#include "game/Bot.hpp"

#include <cmath>

#include "core/Timing.hpp"
#include "game/Level.hpp"

namespace pm {

PlayerInput Bot::think(const Level& level) {
    PlayerInput in;
    ++tick_;
    const Player& pl = level.player();
    const auto& path = level.layout().mainPath;
    const Vec2 pos = pl.body.pos;

    Vec2 target = level.exitPos() - Vec2{0.0f, 8.0f};
    if (!path.empty() && waypoint_ < path.size()) {
        target = path[waypoint_];
        if ((target - pos).length() < 24.0f) ++waypoint_;
    }
    if (level.isSanctum()) target = level.exitPos() - Vec2{0.0f, 8.0f};

    const Vec2 to = target - pos;
    in.moveX = std::fabs(to.x) > 3.0f ? (to.x > 0.0f ? 1.0f : -1.0f) : 0.0f;

    // Progress tracking: if we stop getting closer, dig towards the target.
    float dist = to.length();
    if (dist < lastProgress_ - 2.0f) {
        lastProgress_ = dist;
        stuckTimer_ = 0.0f;
    } else {
        stuckTimer_ += kTickDt;
    }
    if (stuckTimer_ > 2.5f) {
        digTicks_ = 90;
        stuckTimer_ = 0.0f;
        lastProgress_ = dist;
    }

    // Climb: jump into walls and levitate towards higher targets.
    if (pl.body.hitWall || to.y < -12.0f) jumpHold_ = 0.4f;
    jumpHold_ -= kTickDt;
    in.jumpHeld = jumpHold_ > 0.0f;
    in.jumpPressed = in.jumpHeld && pl.body.onGround;

    in.aim = target;
    in.selectWand = 0;
    if (const Actor* foe = const_cast<Level&>(level).nearestHostile(pos, Team::Player, 160.0f)) {
        if (level.lineOfSight(pos, foe->body.pos)) {
            in.aim = foe->body.pos;
            in.castHeld = true;
        }
    }
    if (digTicks_ > 0) {
        --digTicks_;
        in.selectWand = 1;
        in.aim = pos + to.normalized() * 30.0f;
        in.castHeld = true;
    }
    // Grab whatever is lying around.
    in.interactPressed = (tick_ % 30) == 0;
    return in;
}

} // namespace pm
