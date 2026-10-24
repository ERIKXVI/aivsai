#pragma once
// Monsters. Each type is a data entry (EnemyDef) plus one of a handful of behaviours.

#include <vector>

#include "core/Rng.hpp"
#include "game/Actor.hpp"
#include "render/Sprites.hpp"

namespace pm {

enum class EnemyType : uint8_t {
    Slime,
    FrostSlime,
    Bat,
    Ghoul,
    Sapper,
    Imp,
    Wraith,
    Shroom,
    Turret,
    Beetle,
    Worm,
    Golem,
    Companion,
    Boss,
    Count
};

enum class Behaviour : uint8_t { Hopper, Walker, Flyer, Caster, Spore, Turret, Kamikaze, Burrower, Brute, Companion, Boss };

struct EnemyDef {
    const char* name;
    SpriteId sprite;
    Behaviour behaviour;
    float hp;
    float halfW, halfH;
    float speed;            ///< cells / s
    float contactDamage;
    DamageType contactType;
    SpellId attack;         ///< ranged attack spell (None = melee only)
    float attackCooldown;   ///< seconds
    float sight;            ///< detection range in cells
    MatId blood;            ///< material spilled when hurt
    int gold;               ///< gold dropped on death
    bool flying;
    uint32_t glow;          ///< light colour (0 = none)
    std::array<float, static_cast<int>(DamageType::Count)> resist;
};

const EnemyDef& enemyDef(EnemyType type);

/// Picks a monster appropriate for the biome / ground or air slot.
EnemyType pickEnemy(int levelIndex, bool air, float difficulty, Rng& rng);

class Enemy final : public Actor {
public:
    Enemy(EnemyType type, Vec2 pos, uint32_t id, float hpScale);

    void tick(Level& level) override;
    void drawAlbedo(DrawContext& ctx) const override;
    void drawEmission(DrawContext& ctx) const override;
    void addLights(DrawContext& ctx) const override;

    EnemyType type() const { return type_; }
    const EnemyDef& def() const { return *def_; }
    bool aggro() const { return aggro_; }
    /// Boss phase, 0-based; meaningful for the boss only.
    int bossPhase() const { return phase_; }

    // Worm body segments (world positions), head first.
    std::vector<Vec2> segments;

protected:
    void onDamaged(Level& level, const DamageInfo& info, float dealt) override;
    void onKilled(Level& level, const DamageInfo& info) override;
    float burnDps() const override { return 12.0f; }

private:
    void think(Level& level);
    void tickHopper(Level& level, Vec2 toPlayer, bool sees);
    void tickWalker(Level& level, Vec2 toPlayer, bool sees);
    void tickFlyer(Level& level, Vec2 toPlayer, bool sees);
    void tickCaster(Level& level, Vec2 toPlayer, bool sees);
    void tickSpore(Level& level, Vec2 toPlayer, bool sees);
    void tickTurret(Level& level, Vec2 toPlayer, bool sees);
    void tickKamikaze(Level& level, Vec2 toPlayer, bool sees);
    void tickBurrower(Level& level, Vec2 toPlayer, bool sees);
    void tickBrute(Level& level, Vec2 toPlayer, bool sees);
    void tickCompanion(Level& level);
    void tickBoss(Level& level, Vec2 toPlayer, bool sees);
    void fireAt(Level& level, Vec2 target, SpellId spell, float spreadRad = 0.05f);
    void contactPlayer(Level& level);
    int animFrame(float time) const;
    void drawBoss(DrawContext& ctx, Vec2 p, Color tint) const;

    EnemyType type_;
    const EnemyDef* def_;
    bool aggro_ = false;
    bool facingRight_ = true;
    float attackTimer_ = 0.0f;
    float stateTimer_ = 0.0f;
    float wanderDir_ = 1.0f;
    float animTime_ = 0.0f;
    float lostSight_ = 0.0f;
    Vec2 home_;
    Vec2 lastSeen_;
    int phase_ = 0;
    float special_ = 0.0f;
};

} // namespace pm
