#pragma once
// Spell definitions. Spells are cards placed into wand slots: projectiles fly, modifiers
// alter every projectile of the cast, multicasts draw extra cards, and trigger spells carry
// the next card as a payload released on impact. All tuning lives in Spells.cpp.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/Math.hpp"
#include "sim/Material.hpp"

namespace pm {

enum class SpellType : uint8_t { Projectile, Modifier, Multicast };

enum class DamageType : uint8_t { Physical, Fire, Explosion, Ice, Electric, Acid, Toxic, Holy, Count };

/// Damage multiplier per DamageType: 1 = normal, 0 = immune, above 1 = weak.
using Resistances = std::array<float, static_cast<std::size_t>(DamageType::Count)>;
inline constexpr Resistances kNeutralResist = [] {
    Resistances r{};
    r.fill(1.0f);
    return r;
}();

enum class ProjVisual : uint8_t { Spark, Bolt, Orb, BigOrb, Bomb, Shard, Saw, Beam, Glob, Star };

enum class SpellId : uint8_t {
    None = 0,
    // Projectiles
    SparkBolt, SparkTrigger, MagicMissile, Fireball, FireBolt, BouncingBurst, EnergyOrb, DiggingBolt,
    Chainsaw, Bomb, Dynamite, Lightning, AcidGlob, IceShard, WaterOrb, OilOrb, LavaOrb, SporeBomb,
    MidasBolt, BlackHole, Meteor, Sawblade, StarShower, Plasma, HolyBomb, TeleportBolt, ArcBolt,
    // Modifiers
    DamageUp, SpeedUp, Homing, Piercing, Bouncy, Explosive, FireTrail, WaterTrail, OilTrail, AcidTrail,
    Heavy, Weightless, Accuracy, Critical, Incendiary, Frost, Larger, ManaRefund,
    // Multicasts
    DoubleCast, TripleCast, QuadScatter, Formation,
    // Later additions (ids are saved, so new spells are only ever appended)
    RimeTrail,
    Count
};

constexpr int kSpellCount = static_cast<int>(SpellId::Count);

namespace projflag {
constexpr uint32_t kHoming = 1u << 0;
constexpr uint32_t kPiercing = 1u << 1;
constexpr uint32_t kIncendiary = 1u << 2;  ///< ignites what it hits
constexpr uint32_t kFreezing = 1u << 3;    ///< freezes water, chills creatures
constexpr uint32_t kDigs = 1u << 4;        ///< carves terrain along its path
constexpr uint32_t kHitscan = 1u << 5;     ///< instant ray (lightning)
constexpr uint32_t kElectric = 1u << 6;    ///< electrifies conductive liquids on impact
constexpr uint32_t kGravityWell = 1u << 7; ///< pulls matter and creatures (black hole)
constexpr uint32_t kTeleport = 1u << 8;    ///< moves the caster to the impact point
constexpr uint32_t kTransmute = 1u << 9;   ///< turns solids it touches into gold ore
constexpr uint32_t kFromSky = 1u << 10;    ///< spawns above the target and falls (meteor)
constexpr uint32_t kRolls = 1u << 11;      ///< sticks to the ground and rolls (sawblade)
constexpr uint32_t kNoCollideWorld = 1u << 12;
} // namespace projflag

struct ProjectileDef {
    ProjVisual visual = ProjVisual::Spark;
    float speed = 300.0f;      ///< cells / s
    float lifetime = 1.0f;     ///< seconds
    float damage = 5.0f;
    DamageType damageType = DamageType::Physical;
    float gravity = 0.0f;      ///< multiplier of world gravity
    float drag = 0.0f;         ///< velocity fraction lost per second
    float radius = 1.0f;       ///< collision radius against creatures
    int bounces = 0;
    float explosionRadius = 0.0f;
    float explosionDamage = 0.0f;
    MatId spawnMat = mat::Empty; ///< material released on impact
    int spawnAmount = 0;         ///< cells
    MatId trailMat = mat::Empty;
    float trailRate = 0.0f;      ///< cells per tick
    float digRadius = 0.0f;
    uint8_t digPower = 0;        ///< max hardness carved
    float knockback = 40.0f;
    float lightRadius = 30.0f;
    uint32_t color = 0xffffff;
    uint32_t flags = 0;
    int chains = 0;              ///< further foes it leaps to after a hit (arc spark)
};

/// Changes applied by a modifier spell to every projectile in the cast.
struct ModifierDef {
    float damageAdd = 0.0f;
    float damageMul = 1.0f;
    float speedMul = 1.0f;
    float spreadAdd = 0.0f;      ///< degrees
    float lifetimeMul = 1.0f;
    float gravityAdd = 0.0f;
    float explosionAdd = 0.0f;
    float radiusMul = 1.0f;
    int bouncesAdd = 0;
    float critChance = 0.0f;
    MatId trailMat = mat::Empty;
    uint32_t addFlags = 0;
    bool zeroGravity = false;
};

struct SpellDef {
    const char* name;
    const char* description;
    SpellType type;
    float mana;
    float castDelay;    ///< seconds added to the wand's cast delay
    float recharge;     ///< seconds added to the wand's recharge time
    int tier;           ///< 0..4 — how deep it starts appearing
    int price;
    int multicast;      ///< cards drawn by a multicast spell
    float spread;       ///< extra spread for multicast fans (degrees)
    bool trigger;       ///< carries the next card as an impact payload
    ProjectileDef proj;
    ModifierDef mod;
};

const SpellDef& spellDef(SpellId id);
/// Description plus mana, damage and delay figures, for tooltips.
std::string spellDetails(SpellId id);
const char* damageTypeName(DamageType t);

} // namespace pm
