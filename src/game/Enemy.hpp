#pragma once
// Monsters. Each type is a data entry (EnemyDef) plus one of a handful of behaviours.

#include <optional>
#include <vector>

#include "core/Rng.hpp"
#include "game/Actor.hpp"
#include "render/Sprites.hpp"

namespace pm {

// Values index the kill counts kept in saves and profiles: only ever append.
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
    // Stage guardians, one per lair on stages I-IV.
    BlackToad,
    WhiteWraith,
    MineKing,
    RedColossus,
    Count
};

/// An elite's trait on top of its toughness, changing how the fight plays: a Burning elite
/// trails fire and shrugs it off, a Swift one moves and strikes half again as fast, and a
/// Gilded one bleeds gold dust and pays out treble.
enum class Affix : uint8_t { None, Burning, Swift, Gilded, Count };
const char* affixName(Affix affix);

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
    // Optional extras for recoloured kin and giants:
    uint32_t tint = 0xffffff; ///< colour multiplied over the (often shared) sprite
    int scale = 1;            ///< sprite drawn at this integer scale
    int volley = 1;           ///< projectiles per ranged attack, fanned out
    bool guardian = false;    ///< a lair's guardian: optional, grants a perk when slain
};

const EnemyDef& enemyDef(EnemyType type);
/// Colour a creature's shared sprite is tinted with (Frost Slimes and Sappers reuse the
/// Slime and Ghoul art), for the world and for portraits.
Color enemyTint(EnemyType type);
/// The guardian whose lair lies on this stage.
EnemyType guardianFor(int levelIndex);
/// The creature with this display name (e.g. a recorded death cause), if there is one.
std::optional<EnemyType> enemyNamed(const char* name);

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
    /// Elites are rare, tougher variants that glow, carry an affix and always drop treasure.
    void makeElite(Affix affix);
    bool elite() const { return elite_; }
    Affix affix() const { return affix_; }
    /// Movement speed (cells / s) and attack cooldown (s), after a Swift elite's haste or an
    /// enraged guardian's fury.
    float moveSpeed() const { return def_->speed * (affix_ == Affix::Swift ? 1.5f : 1.0f) * (enraged_ ? 1.3f : 1.0f); }
    float attackCooldown() const {
        return def_->attackCooldown * (affix_ == Affix::Swift ? 0.67f : 1.0f) * (enraged_ ? 0.7f : 1.0f);
    }
    bool guardian() const { return def_->guardian; }
    /// A guardian below half health fights faster and harder.
    bool enraged() const { return enraged_; }
    float damageScale() const { return elite_ ? 1.35f : 1.0f; }
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
    /// What every guardian adds to its kind's behaviour: rage, landing shockwaves, a brood.
    void tickGuardian(Level& level, Vec2 toPlayer, bool sees);
    void fireAt(Level& level, Vec2 target, SpellId spell, float spreadRad = 0.05f);
    /// Ranged attacks are telegraphed: when `wantsToFire` (and the player is within view range)
    /// a short glowing wind-up starts, and this returns true on the tick it completes.
    bool readyToFire(Level& level, bool wantsToFire);
    void contactPlayer(Level& level);
    int animFrame(float time) const;
    void drawBoss(DrawContext& ctx, Vec2 p, Color tint) const;

    EnemyType type_;
    const EnemyDef* def_;
    bool aggro_ = false;
    bool facingRight_ = true;
    float attackTimer_ = 0.0f;
    float windup_ = 0.0f; ///< seconds left in a telegraphed attack's wind-up
    float stateTimer_ = 0.0f;
    float wanderDir_ = 1.0f;
    float animTime_ = 0.0f;
    float lostSight_ = 0.0f;
    Vec2 home_;
    Vec2 lastSeen_;
    int phase_ = 0;
    float special_ = 0.0f;
    bool elite_ = false;
    Affix affix_ = Affix::None;
    // Guardian state.
    bool enraged_ = false;
    bool airborne_ = false;
    float fallSpeed_ = 0.0f;  ///< fastest descent of the current leap, for the landing quake
    float guardTimer_ = 0.0f; ///< the colossus' fire between slams
    int broods_ = 0;          ///< slime broods the toad has spawned
};

} // namespace pm
