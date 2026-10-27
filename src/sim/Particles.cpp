#include "sim/Particles.hpp"

#include <algorithm>
#include <cmath>

#include "render/WorldRenderer.hpp"
#include "sim/World.hpp"

namespace pm {

namespace {
constexpr float kParticleGravity = 420.0f;
}

void ParticleSystem::addMatter(Vec2 pos, Vec2 vel, const Cell& cell, float life) {
    if (particles_.size() >= kMaxParticles) return;
    Particle p;
    p.pos = p.prevPos = pos;
    p.vel = vel;
    p.cell = cell;
    p.cell.flags &= static_cast<uint8_t>(~cellflag::kMoved);
    p.life = p.maxLife = life;
    p.flags = partflag::kMatter;
    p.drag = 0.4f;
    particles_.push_back(p);
}

void ParticleSystem::addVisual(Vec2 pos, Vec2 vel, Rgba color, Rgba glow, float life, float gravity, uint8_t flags) {
    if (particles_.size() >= kMaxParticles) return;
    Particle p;
    p.pos = p.prevPos = pos;
    p.vel = vel;
    p.color = color;
    p.glow = glow;
    p.life = p.maxLife = life;
    p.gravity = gravity;
    p.flags = flags;
    p.drag = 1.2f;
    particles_.push_back(p);
}

void ParticleSystem::burst(Vec2 pos, int count, float speed, Rgba color, Rgba glow, float life, Rng& rng,
                           float gravity, uint8_t flags) {
    for (int i = 0; i < count; ++i) {
        Vec2 v = fromAngle(rng.angle(), speed * rng.range(0.3f, 1.0f));
        addVisual(pos, v, color, glow, life * rng.range(0.5f, 1.0f), gravity, flags);
    }
}

void ParticleSystem::deposit(World& world, const Particle& p) {
    auto settle = [&](int tx, int ty) {
        if (!world.inBounds(tx, ty) || !isVacant(world.at(tx, ty).mat)) return false;
        world.set(tx, ty, p.cell);
        return true;
    };
    // Settle into the nearest empty cell around the landing point.
    const int x = floorToInt(p.pos.x), y = floorToInt(p.pos.y);
    static constexpr int kOffsets[][2] = {{0, 0}, {0, -1}, {-1, 0}, {1, 0}, {-1, -1}, {1, -1}, {0, -2}, {-2, 0}, {2, 0}};
    for (const auto& o : kOffsets)
        if (settle(x + o[0], y + o[1])) return;
    // Buried (launched inside terrain, or the cells around it filled up): retrace the way it flew
    // this tick, then look straight up, rather than destroy the matter.
    constexpr int kSearch = 6;
    const Vec2 back = p.prevPos - p.pos;
    const float dist = back.length();
    const Vec2 dir = back.normalized();
    for (int i = 1; i <= kSearch && static_cast<float>(i - 1) < dist; ++i) {
        const Vec2 q = p.pos + dir * std::min(static_cast<float>(i), dist);
        if (settle(floorToInt(q.x), floorToInt(q.y))) return;
    }
    for (int i = 3; i <= kSearch + 2; ++i) // the offsets above already tried two cells up
        if (settle(x, y - i)) return;
}

void ParticleSystem::update(World& world, float dt) {
    for (size_t i = 0; i < particles_.size();) {
        Particle& p = particles_[i];
        p.prevPos = p.pos;
        p.life -= dt;
        bool dead = p.life <= 0.0f;
        if (!dead) {
            if (!(p.flags & partflag::kNoGravity)) p.vel.y += kParticleGravity * p.gravity * dt;
            p.vel *= 1.0f / (1.0f + p.drag * dt);
            // March in sub-cell steps so fast particles don't tunnel through walls.
            Vec2 delta = p.vel * dt;
            int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::fabs(delta.x), std::fabs(delta.y)))));
            Vec2 step = delta / static_cast<float>(steps);
            for (int s = 0; s < steps; ++s) {
                Vec2 next = p.pos + step;
                int nx = floorToInt(next.x), ny = floorToInt(next.y);
                MatId m = world.matAt(nx, ny);
                const bool blocked = !isPassable(m);
                if (p.flags & partflag::kMatter) {
                    // Matter lands on anything that is not air, gas or fire (liquids absorb it).
                    if (blocked) {
                        deposit(world, p);
                        dead = true;
                        break;
                    }
                } else if (blocked && isSolidForCollision(m)) {
                    if (p.flags & partflag::kDieOnHit) {
                        dead = true;
                        break;
                    }
                    p.vel = p.vel * -0.3f;
                    break;
                }
                p.pos = next;
            }
            if (!world.inBounds(floorToInt(p.pos.x), floorToInt(p.pos.y))) dead = true;
        } else if (p.flags & partflag::kMatter) {
            deposit(world, p);
        }
        if (dead) {
            particles_[i] = particles_.back();
            particles_.pop_back();
        } else {
            ++i;
        }
    }
}

void ParticleSystem::draw(WorldRenderer& layer, float alpha, uint32_t tick) const {
    for (const Particle& p : particles_) {
        const Vec2 at = lerp(p.prevPos, p.pos, alpha);
        int x = floorToInt(at.x), y = floorToInt(at.y);
        if (p.flags & partflag::kMatter) {
            layer.stamp(x, y, cellColor(p.cell, x, y, tick), cellEmission(p.cell, x, y, tick));
            continue;
        }
        Rgba c = p.color, g = p.glow;
        if (p.flags & partflag::kFade) {
            float f = clamp(p.life / p.maxLife, 0.0f, 1.0f);
            c.a = static_cast<uint8_t>(c.a * f);
            g.r = static_cast<uint8_t>(g.r * f);
            g.g = static_cast<uint8_t>(g.g * f);
            g.b = static_cast<uint8_t>(g.b * f);
        }
        if (c.a < 8) continue;
        layer.stamp(x, y, c, g);
        if (p.flags & partflag::kStreak) {
            Vec2 back = at - p.vel.normalized() * 1.5f;
            layer.stamp(floorToInt(back.x), floorToInt(back.y), c, g);
        }
    }
}

} // namespace pm
