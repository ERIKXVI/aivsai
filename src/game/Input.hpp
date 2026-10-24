#pragma once
// Player intent for one simulation tick, decoupled from the physical devices so the game
// can be driven by keyboard/mouse, a gamepad or the test bot.

#include "core/Math.hpp"

namespace pm {

struct PlayerInput {
    float moveX = 0.0f;      ///< -1..1
    bool jumpPressed = false;
    bool jumpHeld = false;
    bool downHeld = false;
    Vec2 aim;                ///< world position the player aims at
    bool castHeld = false;
    bool sprayHeld = false;  ///< pour the flask
    bool absorbHeld = false; ///< suck material into the flask
    bool drinkPressed = false;
    bool interactPressed = false;
    int selectWand = -1;     ///< direct wand selection, -1 = none
    int cycleWand = 0;       ///< +1 / -1 from the mouse wheel
};

} // namespace pm
