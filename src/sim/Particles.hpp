#pragma once
// Free-flying particles. Matter particles (debris, splashes, sprayed liquid) fly ballistic
// arcs and settle back into the grid where they land; visual particles (sparks, embers,
// glitter) just glow and fade. Particles are stamped into the world layer for drawing.

#include <vector>

#include "core/Math.hpp"
#include "core/Rng.hpp"
#include "sim/Cell.hpp"
#include "sim/Material.hpp"

namespace pm {

class World;
class WorldRenderer;

namespace partflag {
constexpr uint8_t kMatter = 1u << 0;    ///< carries a cell and deposits it on landing
constexpr uint8_t kNoGravity = 1u << 1;
constexpr uint8_t kFade = 1u << 2;      ///< alpha fades with remaining life
constexpr uint8_t kDieOnHit = 1u << 3;  ///< visual particle that vanishes on contact
constexpr uint8_t kStreak = 1u << 4;    ///< draws a short streak along velocity
} // namespace partflag

struct Particle {
    Vec2 pos;
    Vec2 prevPos;   ///< position a tick ago, for drawing between ticks
    Vec2 vel;
    Cell cell;      ///< matter payload (kMatter)
    Rgba color;
    Rgba glow;      ///< emission (rgb), stamped into the emission layer
    float life = 1.0f;
    float maxLife = 1.0f;
    float gravity = 1.0f;
    float drag = 0.0f;
    uint8_t flags = 0;
};

class ParticleSystem {
public:
    static constexpr size_t kMaxParticles = 24000;

    void clear() { particles_.clear(); }
    size_t count() const { return particles_.size(); }

    /// Launches a matter particle carrying `cell` (which keeps its burning state etc.).
    void addMatter(Vec2 pos, Vec2 vel, const Cell& cell, float life = 4.0f);
    /// Launches a purely visual particle.
    void addVisual(Vec2 pos, Vec2 vel, Rgba color, Rgba glow, float life, float gravity = 0.4f,
                   uint8_t flags = partflag::kFade | partflag::kDieOnHit);
    /// Radial burst of sparks.
    void burst(Vec2 pos, int count, float speed, Rgba color, Rgba glow, float life, Rng& rng, float gravity = 0.3f);

    void update(World& world, float dt);
    /// Stamps every particle `alpha` of the way from its previous to its current position;
    /// `tick` animates burning matter.
    void draw(WorldRenderer& layer, float alpha = 1.0f, uint32_t tick = 0) const;

private:
    void deposit(World& world, const Particle& p);
    std::vector<Particle> particles_;
};

} // namespace pm
