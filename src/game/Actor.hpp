#pragma once
// Base class for every creature: the player, monsters and companions. Owns the physics
// body, health, damage resistances and status effects driven by the materials touched.

#include <array>
#include <cstdint>
#include <string>

#include "game/Body.hpp"
#include "game/Spells.hpp"

namespace pm {

class Level;
class SpriteBank;
class SceneRenderer;

enum class Team : uint8_t { Player, Monster };

struct DamageInfo {
    float amount = 0.0f;
    DamageType type = DamageType::Physical;
    Vec2 knockback;           ///< velocity added to the victim
    Team source = Team::Monster;
    bool crit = false;
    bool silent = false;      ///< damage over time: no flash, sound or number
    const char* cause = nullptr; ///< what dealt it, for the death screen (static string)
};

struct StatusEffects {
    float burning = 0.0f;     ///< seconds left on fire
    float wet = 0.0f;         ///< soaked: can't ignite, conducts electricity
    float oiled = 0.0f;       ///< burns longer
    float poisoned = 0.0f;
    float chilled = 0.0f;     ///< slowed
    float shocked = 0.0f;     ///< brief stun / visual
    float hasted = 0.0f;      ///< quickened by a draught of quicksilver
    float fury = 0.0f;        ///< spells strike harder after a draught of ichor
};

struct DrawContext {
    const SpriteBank& sprites;
    SceneRenderer& scene;
    float alpha;   ///< tick interpolation factor
    float time;    ///< seconds, for animation
};

class Actor {
public:
    virtual ~Actor() = default;

    virtual void tick(Level& level) = 0;
    virtual void drawAlbedo(DrawContext& ctx) const = 0;
    virtual void drawEmission(DrawContext& ctx) const { (void)ctx; }
    virtual void addLights(DrawContext& ctx) const { (void)ctx; }

    /// Applies damage after resistances. Returns the damage actually dealt.
    float applyDamage(Level& level, const DamageInfo& info);

    bool alive() const { return !dead; }
    Vec2 center() const { return body.pos; }
    Rectf bounds() const { return body.bounds(); }

    Body body;
    float hp = 10.0f;
    float maxHp = 10.0f;
    Team team = Team::Monster;
    StatusEffects status;
    std::array<float, static_cast<int>(DamageType::Count)> resist{1, 1, 1, 1, 1, 1, 1, 1};
    float hurtFlash = 0.0f;
    float invulnerable = 0.0f;
    float contactDamageCooldown = 0.0f;
    bool dead = false;
    bool flying = false;
    uint32_t id = 0;
    const char* lastHitCause = nullptr; ///< cause of the most recent damage taken
    bool lastHitSelf = false;           ///< ...and whether it came from the actor's own side
    DamageType lastHitType = DamageType::Physical;

protected:
    /// Burning, liquids, contact damage and status timers. Call once per tick.
    void updateStatus(Level& level, const Environment& env);
    virtual void onDamaged(Level& level, const DamageInfo& info, float dealt) { (void)level, (void)info, (void)dealt; }
    virtual void onKilled(Level& level, const DamageInfo& info) { (void)level, (void)info; }
    /// Scale applied to burn damage (players burn more slowly than monsters).
    virtual float burnDps() const { return 10.0f; }
};

/// What killed an actor, for the death screen: "Slain by a Fire Imp", "Undone by lava",
/// "Undone by your own Bomb". Empty if nothing was recorded.
std::string deathCause(const Actor& victim);
/// A one-line tip for the death screen on how to survive what killed the actor next time,
/// or nullptr if there is nothing useful to say.
const char* deathAdvice(const Actor& victim);

} // namespace pm
