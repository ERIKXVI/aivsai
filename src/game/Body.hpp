#pragma once
// Axis-aligned physics body that moves through the cell world. Handles pixel-terrain
// collision with automatic step-up (so creatures walk over bumpy ground), liquids
// (buoyancy and drag) and getting un-buried when sand pours over it.

#include "core/Math.hpp"
#include "sim/Material.hpp"

namespace pm {

class World;

/// What a body is currently touching, sampled once per tick.
struct Environment {
    float submerged = 0.0f;     ///< fraction of the body inside liquid
    MatId liquid = 0;           ///< dominant liquid
    float contactDps = 0.0f;    ///< strongest damaging material touched (damage per second)
    MatId contactMat = 0;
    bool fire = false;          ///< touching fire, lava or burning cells
    bool extinguishing = false; ///< touching water or similar, other than the actor's own blood
    bool water = false;         ///< touching water or brine (the Doused feat), even when it is the actor's own blood
    bool healing = false;
    bool sticky = false;        ///< touching sticky matter (slime): creatures wade through it at kStickySpeed
    bool acid = false;
};

struct Body {
    Vec2 pos;      ///< centre of the box, in cells
    Vec2 prevPos;  ///< position at the previous tick (render interpolation)
    Vec2 vel;      ///< cells per second
    float halfW = 3.0f, halfH = 5.0f;
    float gravityScale = 1.0f;
    float stepHeight = 3.0f;
    float bounce = 0.0f;   ///< restitution on impact
    float friction = 0.0f; ///< ground friction applied by moveBody (items)
    bool collides = true;

    bool onGround = false;
    bool hitWall = false;
    bool hitCeiling = false;
    float impactSpeed = 0.0f; ///< speed lost against terrain this tick

    Rectf bounds() const { return {pos.x - halfW, pos.y - halfH, halfW * 2.0f, halfH * 2.0f}; }
    Vec2 feet() const { return {pos.x, pos.y + halfH}; }
    Vec2 interpolated(float alpha) const { return lerp(prevPos, pos, alpha); }
};

constexpr float kGravity = 540.0f; // cells / s^2
/// Speed multiplier for a creature wading through sticky matter (Environment::sticky).
constexpr float kStickySpeed = 0.45f;

/// True if any collision-solid cell overlaps the box centred at `c`.
bool boxBlocked(const World& world, Vec2 c, float halfW, float halfH);

/// Integrates gravity and velocity for one tick and resolves terrain collisions.
void moveBody(Body& body, const World& world, float dt);

/// Samples materials overlapping the body. Touching `ownBlood`, the matter the creature bleeds,
/// does not put out its flames (or the blood each hit spills would); wading deep in it still does.
Environment sampleEnvironment(const Body& body, const World& world, MatId ownBlood = mat::Empty);

/// Pushes a body out of terrain it is stuck in. Loose powder that buries the body is
/// shoved aside into free space above it. Returns true if the body is now free.
bool unbury(Body& body, World& world);

} // namespace pm
