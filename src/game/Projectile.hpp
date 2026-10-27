#pragma once
// Projectiles launched by wands (player) and monsters.

#include <array>
#include <memory>

#include "game/Actor.hpp"
#include "game/Wand.hpp"

namespace pm {

struct Projectile {
    SpellId spell = SpellId::None;
    ProjectileDef def;
    Vec2 pos, prevPos, vel;
    float age = 0.0f;
    Team team = Team::Player;
    uint32_t ownerId = 0;
    const char* ownerName = nullptr; ///< the creature that cast it (monster shots), kept after it dies
    float critChance = 0.0f;
    int bouncesLeft = 0;
    int chainsLeft = 0;     ///< leaps still to make to further foes (seeded from def.chains)
    float damageMul = 1.0f; ///< caster's damage multiplier (perks, difficulty)
    Vec2 beamEnd;           ///< hitscan beams: end point for drawing
    float trailAccum = 0.0f;
    float hitCooldown = 0.0f;
    std::shared_ptr<CastShot> payload;
    static constexpr int kHitMemory = 8; ///< how many recent victims a projectile remembers
    std::array<uint32_t, kHitMemory> hitIds{};
    int hitCount = 0;
    bool dead = false;

    bool alreadyHit(uint32_t actorId) const {
        const int remembered = hitCount < kHitMemory ? hitCount : kHitMemory;
        for (int i = 0; i < remembered; ++i)
            if (hitIds[static_cast<size_t>(i)] == actorId) return true;
        return false;
    }
    void rememberHit(uint32_t actorId) {
        // A ring of the most recent victims: the oldest slot is reused each time.
        hitIds[static_cast<size_t>(hitCount % kHitMemory)] = actorId;
        ++hitCount;
    }
};

/// Resolves an instant (hitscan) projectile: traces the ray, applies hits and impact
/// effects, and leaves the projectile alive briefly so the beam can be drawn.
void fireHitscan(Level& level, Projectile& p, float range);
/// Advances one projectile by a tick: movement, collisions, trails and impact effects.
void updateProjectile(Level& level, Projectile& p, float dt);
void drawProjectile(const Projectile& p, DrawContext& ctx, uint32_t tick);

} // namespace pm
