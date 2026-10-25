#include "game/Bot.hpp"

#include <cmath>

#include "core/Timing.hpp"
#include "game/Level.hpp"

namespace pm {

PlayerInput Bot::think(const Level& level) {
    PlayerInput in;
    ++tick_;
    const Player& pl = level.player();
    const Vec2 pos = pl.body.pos;
    const Vec2 goal = level.exitPos() - Vec2{0.0f, 8.0f};

    // Replan the flow field every couple of seconds; the world keeps changing.
    if (--replanTimer_ <= 0) {
        nav_.build(level.world());
        nav_.computeDistances(goal);
        replanTimer_ = 120;
    }

    Vec2 target = goal;
    Vec2 step;
    const int distance = nav_.distanceAt(pos);
    if (distance != NavGrid::kUnreachable && nav_.nextStep(pos, step)) target = step;

    // Progress tracking: if the distance stops shrinking, dig towards the goal.
    if (distance != NavGrid::kUnreachable && distance < bestDistance_) {
        bestDistance_ = distance;
        stuckTimer_ = 0.0f;
        noProgress_ = 0.0f;
    } else {
        stuckTimer_ += kTickDt;
        noProgress_ += kTickDt;
    }
    if (stuckTimer_ > 3.0f) {
        digTicks_ = 60;
        stuckTimer_ = 0.0f;
    }

    const Vec2 to = target - pos;
    in.moveX = std::fabs(to.x) > 2.0f ? (to.x > 0.0f ? 1.0f : -1.0f) : 0.0f;
    if (pl.body.hitWall || to.y < -6.0f) jumpHold_ = 0.35f;
    jumpHold_ -= kTickDt;
    in.jumpHeld = jumpHold_ > 0.0f;
    in.jumpPressed = in.jumpHeld && pl.body.onGround;

    in.aim = pos + Vec2{to.x >= 0.0f ? 40.0f : -40.0f, 0.0f};
    in.selectWand = 0;
    if (const Actor* foe = level.nearestHostile(pos, Team::Player, 150.0f)) {
        // Keep a little distance from explosions of our own making.
        if ((foe->body.pos - pos).length() > 18.0f && level.lineOfSight(pos, foe->body.pos)) {
            in.aim = foe->body.pos;
            in.castHeld = true;
        }
    }
    if (digTicks_ > 0) {
        --digTicks_;
        in.selectWand = 1;
        in.aim = pos + (goal - pos).normalized() * 30.0f;
        in.castHeld = (tick_ % 4) != 0;
    }
    in.interactPressed = (tick_ % 20) == 0;

    // Drink healing potions when hurt, and water when burning, as the hints teach.
    const MatId flask = level.run().flask.mat;
    const bool heal = pl.hp < pl.maxHp * 0.4f && flask == mat::Vitae;
    const bool douse = pl.status.burning > 0.5f && (flask == mat::Water || flask == mat::Brine);
    in.drinkPressed = (heal || douse) && (tick_ % 30) == 0;
    return in;
}

} // namespace pm
