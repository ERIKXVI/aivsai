#include "game/Body.hpp"

#include <cmath>

#include "sim/World.hpp"

namespace pm {

bool boxBlocked(const World& world, Vec2 c, float halfW, float halfH) {
    const int x0 = floorToInt(c.x - halfW), x1 = floorToInt(c.x + halfW - 0.001f);
    const int y0 = floorToInt(c.y - halfH), y1 = floorToInt(c.y + halfH - 0.001f);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (isSolidForCollision(world.matAt(x, y))) return true;
    return false;
}

void moveBody(Body& b, const World& world, float dt) {
    b.hitWall = b.hitCeiling = false;
    b.impactSpeed = 0.0f;
    const bool wasGrounded = b.onGround;
    b.vel.y += kGravity * b.gravityScale * dt;

    if (!b.collides) {
        b.pos += b.vel * dt;
        b.onGround = false;
        return;
    }

    // Horizontal sweep in half-cell increments with step-up over small ledges.
    float dx = b.vel.x * dt;
    while (std::fabs(dx) > 1e-4f) {
        float step = clamp(dx, -0.5f, 0.5f);
        Vec2 next{b.pos.x + step, b.pos.y};
        if (!boxBlocked(world, next, b.halfW, b.halfH)) {
            b.pos = next;
        } else {
            bool stepped = false;
            if (wasGrounded && b.stepHeight > 0.0f) {
                for (float s = 1.0f; s <= b.stepHeight; s += 1.0f) {
                    Vec2 up{next.x, next.y - s};
                    if (!boxBlocked(world, up, b.halfW, b.halfH)) {
                        b.pos = up;
                        stepped = true;
                        break;
                    }
                }
            }
            if (!stepped) {
                b.hitWall = true;
                b.impactSpeed = std::max(b.impactSpeed, std::fabs(b.vel.x));
                b.vel.x = -b.vel.x * b.bounce;
                break;
            }
        }
        dx -= step;
    }

    // Vertical sweep.
    float dy = b.vel.y * dt;
    b.onGround = false;
    while (std::fabs(dy) > 1e-4f) {
        float step = clamp(dy, -0.5f, 0.5f);
        Vec2 next{b.pos.x, b.pos.y + step};
        if (!boxBlocked(world, next, b.halfW, b.halfH)) {
            b.pos = next;
            dy -= step;
            continue;
        }
        b.impactSpeed = std::max(b.impactSpeed, std::fabs(b.vel.y));
        if (step > 0.0f) {
            b.onGround = true;
            // Snap onto the blocking row so bodies rest exactly on the surface.
            float row = std::floor(next.y + b.halfH - 0.001f);
            float snapped = row - b.halfH;
            if (snapped > b.pos.y && !boxBlocked(world, {b.pos.x, snapped}, b.halfW, b.halfH)) b.pos.y = snapped;
        } else {
            b.hitCeiling = true;
            float row = std::floor(next.y - b.halfH);
            float snapped = row + 1.0f + b.halfH;
            if (snapped < b.pos.y && !boxBlocked(world, {b.pos.x, snapped}, b.halfW, b.halfH)) b.pos.y = snapped;
        }
        b.vel.y = -b.vel.y * b.bounce;
        if (std::fabs(b.vel.y) < 20.0f) b.vel.y = 0.0f;
        break;
    }

    // Grounded check for bodies resting without vertical motion.
    if (!b.onGround && b.vel.y >= 0.0f &&
        boxBlocked(world, {b.pos.x, b.pos.y + 0.6f}, b.halfW, b.halfH)) {
        b.onGround = true;
        if (b.vel.y > 0.0f) b.vel.y = 0.0f;
    }

    if (b.onGround && b.friction > 0.0f) b.vel.x = approach(b.vel.x, 0.0f, b.friction * dt);
}

Environment sampleEnvironment(const Body& b, const World& world) {
    Environment env;
    const int x0 = floorToInt(b.pos.x - b.halfW) - 1, x1 = floorToInt(b.pos.x + b.halfW);
    const int y0 = floorToInt(b.pos.y - b.halfH) - 1, y1 = floorToInt(b.pos.y + b.halfH);
    int total = 0, liquidCount = 0;
    int counts[kMaterialCount] = {};
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (!world.inBounds(x, y)) continue;
            const Cell& c = world.at(x, y);
            const bool inner = x > x0 && y > y0; // the 1-cell margin only counts for contact
            if (inner) ++total;
            if (c.mat == mat::Empty) continue;
            const MaterialDef& d = matDef(c.mat);
            if (d.phase == Phase::Liquid && inner) {
                ++liquidCount;
                ++counts[c.mat];
            }
            if (c.burning() || (d.flags & matflag::kHot)) env.fire = true;
            if (d.flags & matflag::kExtinguisher) env.extinguishing = true;
            if (d.flags & matflag::kHealing) env.healing = true;
            if (d.flags & matflag::kSticky) env.sticky = true;
            if (d.flags & matflag::kCorrosive) env.acid = true;
            if (d.contactDamage > env.contactDps) {
                env.contactDps = d.contactDamage;
                env.contactMat = c.mat;
            }
        }
    }
    if (total > 0) env.submerged = static_cast<float>(liquidCount) / static_cast<float>(total);
    int best = 0;
    for (int m = 0; m < kMaterialCount; ++m)
        if (counts[m] > best) {
            best = counts[m];
            env.liquid = static_cast<MatId>(m);
        }
    return env;
}

bool unbury(Body& b, World& world) {
    if (!boxBlocked(world, b.pos, b.halfW, b.halfH)) return true;
    // First try to pop free by moving a little.
    static constexpr Vec2 kOffsets[] = {{0, -1}, {0, -2}, {0, -3}, {-1, 0}, {1, 0}, {-2, -1}, {2, -1},
                                        {0, -4}, {-3, 0}, {3, 0}, {0, -5}, {0, -6}, {-4, -2}, {4, -2}};
    for (Vec2 o : kOffsets) {
        if (!boxBlocked(world, b.pos + o, b.halfW, b.halfH)) {
            b.pos += o;
            return true;
        }
    }
    // Still buried: shove overlapping powder upwards into the nearest free cells.
    const int x0 = floorToInt(b.pos.x - b.halfW), x1 = floorToInt(b.pos.x + b.halfW - 0.001f);
    const int y0 = floorToInt(b.pos.y - b.halfH), y1 = floorToInt(b.pos.y + b.halfH - 0.001f);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (!world.inBounds(x, y)) continue;
            Cell c = world.at(x, y);
            if (phaseOf(c.mat) != Phase::Powder) continue;
            world.clear(x, y);
            for (int ty = y0 - 1; ty > y0 - 24; --ty) {
                if (world.inBounds(x, ty) && world.at(x, ty).mat == mat::Empty) {
                    world.set(x, ty, c);
                    break;
                }
            }
        }
    }
    return !boxBlocked(world, b.pos, b.halfW, b.halfH);
}

} // namespace pm
