#include "game/Spells.hpp"

#include <array>
#include <cstdio>

namespace pm {

namespace {

using namespace projflag;

struct SpellTable {
    std::array<SpellDef, kSpellCount> defs{};

    SpellDef& proj(SpellId id, const char* name, const char* desc, float mana, float delay, int tier, int price,
                   const ProjectileDef& p) {
        SpellDef& d = defs[static_cast<size_t>(id)];
        d = SpellDef{name, desc, SpellType::Projectile, mana, delay, 0.0f, tier, price, 0, 0.0f, false, p, {}};
        return d;
    }
    SpellDef& mod(SpellId id, const char* name, const char* desc, float mana, float delay, int tier, int price,
                  const ModifierDef& m) {
        SpellDef& d = defs[static_cast<size_t>(id)];
        d = SpellDef{name, desc, SpellType::Modifier, mana, delay, 0.0f, tier, price, 0, 0.0f, false, {}, m};
        return d;
    }
    SpellDef& multi(SpellId id, const char* name, const char* desc, float mana, int count, float spread, int tier,
                    int price) {
        SpellDef& d = defs[static_cast<size_t>(id)];
        d = SpellDef{name, desc, SpellType::Multicast, mana, 0.0f, 0.0f, tier, price, count, spread, false, {}, {}};
        return d;
    }

    SpellTable() {
        defs[0] = SpellDef{"Empty", "", SpellType::Modifier, 0, 0, 0, 99, 0, 0, 0, false, {}, {}};

        // ---- Projectiles ------------------------------------------------------------------
        {
            ProjectileDef p;
            p.visual = ProjVisual::Spark;
            p.speed = 480;
            p.lifetime = 0.55f;
            p.damage = 6;
            p.color = 0xa8d8ff;
            p.lightRadius = 22;
            proj(SpellId::SparkBolt, "Spark Bolt", "A quick, cheap bolt of raw magic.", 5, 0.05f, 0, 60, p);
            proj(SpellId::SparkTrigger, "Spark Trigger", "A spark bolt that casts the next spell where it lands.", 10,
                 0.05f, 1, 150, p)
                .trigger = true;
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::Orb;
            p.speed = 320;
            p.lifetime = 1.2f;
            p.damage = 4;
            p.explosionRadius = 7;
            p.explosionDamage = 22;
            p.color = 0xc080ff;
            p.lightRadius = 36;
            proj(SpellId::MagicMissile, "Magic Missile", "A volatile orb that bursts on impact.", 45, 0.45f, 1, 180, p);
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::BigOrb;
            p.speed = 270;
            p.lifetime = 1.6f;
            p.gravity = 0.12f;
            p.damage = 8;
            p.damageType = DamageType::Fire;
            p.explosionRadius = 10;
            p.explosionDamage = 28;
            p.trailMat = mat::Fire;
            p.trailRate = 0.6f;
            p.color = 0xff7a28;
            p.lightRadius = 48;
            p.flags = kIncendiary;
            proj(SpellId::Fireball, "Fireball", "Explodes into flame. Mind the oil.", 55, 0.4f, 1, 200, p);
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::Bolt;
            p.speed = 400;
            p.lifetime = 0.9f;
            p.gravity = 0.35f;
            p.damage = 10;
            p.damageType = DamageType::Fire;
            p.explosionRadius = 3;
            p.explosionDamage = 6;
            p.color = 0xffa040;
            p.lightRadius = 30;
            p.flags = kIncendiary;
            proj(SpellId::FireBolt, "Firebolt", "A dart of fire that sets things alight.", 22, 0.15f, 0, 110, p);
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::Spark;
            p.speed = 430;
            p.lifetime = 1.4f;
            p.damage = 4;
            p.bounces = 10;
            p.color = 0x7cff86;
            p.lightRadius = 20;
            proj(SpellId::BouncingBurst, "Bouncing Burst", "Ricochets off walls again and again.", 6, -0.03f, 0, 80, p);
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::BigOrb;
            p.speed = 150;
            p.lifetime = 2.2f;
            p.damage = 16;
            p.radius = 3;
            p.color = 0x5ce4ff;
            p.lightRadius = 44;
            p.flags = kPiercing;
            proj(SpellId::EnergyOrb, "Energy Sphere", "A slow sphere that passes through foes.", 30, 0.25f, 1, 160, p);
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::Bolt;
            p.speed = 320;
            p.lifetime = 0.22f;
            p.damage = 2;
            p.digRadius = 3.5f;
            p.digPower = 125;
            p.color = 0xe0c890;
            p.lightRadius = 16;
            p.flags = kDigs | kPiercing;
            proj(SpellId::DiggingBolt, "Digging Bolt", "Carves a short tunnel through soft rock.", 0, 0.0f, 0, 60, p);
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::Saw;
            p.speed = 30;
            p.lifetime = 0.07f;
            p.damage = 9;
            p.digRadius = 5.0f;
            p.digPower = 150;
            p.color = 0xffe0a0;
            p.lightRadius = 14;
            p.flags = kDigs | kPiercing;
            proj(SpellId::Chainsaw, "Chainsaw", "Shreds whatever is right in front of you. Very fast.", 1, -0.17f, 1, 140,
                 p);
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::Bomb;
            p.speed = 170;
            p.lifetime = 2.6f;
            p.gravity = 1.0f;
            p.damage = 0;
            p.bounces = 3;
            p.explosionRadius = 22;
            p.explosionDamage = 80;
            p.color = 0xffc040;
            p.lightRadius = 16;
            proj(SpellId::Bomb, "Bomb", "A heavy charge with a long fuse. Clears the way.", 30, 0.9f, 0, 120, p)
                .recharge = 0.4f;
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::Bomb;
            p.speed = 280;
            p.lifetime = 1.4f;
            p.gravity = 0.8f;
            p.bounces = 2;
            p.explosionRadius = 13;
            p.explosionDamage = 45;
            p.color = 0xff5030;
            p.lightRadius = 18;
            proj(SpellId::Dynamite, "Dynamite", "Thrown stick of explosive; short fuse.", 20, 0.45f, 1, 130, p);
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::Beam;
            p.speed = 0;
            p.lifetime = 0.12f;
            p.damage = 32;
            p.damageType = DamageType::Electric;
            p.explosionRadius = 5;
            p.explosionDamage = 10;
            p.color = 0xd0e8ff;
            p.lightRadius = 70;
            p.flags = kHitscan | kElectric;
            proj(SpellId::Lightning, "Lightning Bolt", "Instant. Electrifies water and everything in it.", 70, 0.5f, 3, 320,
                 p);
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::Glob;
            p.speed = 250;
            p.lifetime = 2.0f;
            p.gravity = 0.6f;
            p.damage = 6;
            p.damageType = DamageType::Acid;
            p.spawnMat = mat::Acid;
            p.spawnAmount = 45;
            p.color = 0x8cff3c;
            p.lightRadius = 22;
            proj(SpellId::AcidGlob, "Acid Glob", "Splashes acid that eats through stone and flesh.", 30, 0.25f, 1, 150, p);
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::Shard;
            p.speed = 420;
            p.lifetime = 0.9f;
            p.gravity = 0.1f;
            p.damage = 12;
            p.damageType = DamageType::Ice;
            p.color = 0xb4f2ff;
            p.lightRadius = 22;
            p.flags = kFreezing;
            proj(SpellId::IceShard, "Ice Shard", "Freezes liquids and chills enemies.", 15, 0.1f, 1, 130, p);
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::Glob;
            p.speed = 230;
            p.lifetime = 2.0f;
            p.gravity = 0.5f;
            p.damage = 2;
            p.spawnMat = mat::Water;
            p.spawnAmount = 140;
            p.color = 0x4a90ff;
            p.lightRadius = 14;
            proj(SpellId::WaterOrb, "Water Orb", "A sphere of water. Douses fire, cools lava.", 15, 0.2f, 0, 70, p);
            p.spawnMat = mat::Oil;
            p.spawnAmount = 110;
            p.color = 0x6a5030;
            proj(SpellId::OilOrb, "Oil Orb", "Slick, black and very flammable.", 15, 0.2f, 1, 80, p);
            p.spawnMat = mat::Lava;
            p.spawnAmount = 70;
            p.color = 0xff6418;
            p.lightRadius = 40;
            proj(SpellId::LavaOrb, "Lava Orb", "Molten rock. Use with great care.", 80, 0.5f, 3, 260, p);
            p.spawnMat = mat::Spore;
            p.spawnAmount = 60;
            p.color = 0xb090f0;
            p.damageType = DamageType::Toxic;
            p.lightRadius = 22;
            proj(SpellId::SporeBomb, "Spore Pod", "Bursts into glowing spores that sprout fungus.", 25, 0.3f, 2, 140, p);
            p.spawnMat = mat::Midas;
            p.spawnAmount = 16;
            p.color = 0xffd040;
            p.damageType = DamageType::Holy;
            p.lightRadius = 40;
            p.flags = kTransmute;
            proj(SpellId::MidasBolt, "Midas Touch", "Transmutes stone into gold ore. The alchemist's dream.", 140, 0.8f, 3,
                 420, p)
                .recharge = 1.0f;
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::BigOrb;
            p.speed = 60;
            p.lifetime = 3.0f;
            p.damage = 6;
            p.radius = 6;
            p.digRadius = 9.0f;
            p.digPower = 200;
            p.color = 0x2a1040;
            p.lightRadius = 30;
            p.flags = kGravityWell | kDigs | kPiercing;
            proj(SpellId::BlackHole, "Black Hole", "Devours matter and drags foes in.", 180, 0.8f, 3, 380, p);
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::BigOrb;
            p.speed = 360;
            p.lifetime = 3.0f;
            p.gravity = 0.6f;
            p.damage = 20;
            p.explosionRadius = 28;
            p.explosionDamage = 110;
            p.trailMat = mat::Fire;
            p.trailRate = 2.0f;
            p.color = 0xff5a1a;
            p.lightRadius = 70;
            p.flags = kFromSky | kIncendiary;
            proj(SpellId::Meteor, "Meteor", "Calls a burning rock down from the ceiling.", 200, 1.2f, 4, 520, p).recharge =
                1.0f;
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::Saw;
            p.speed = 220;
            p.lifetime = 4.0f;
            p.gravity = 1.0f;
            p.damage = 14;
            p.radius = 3;
            p.digRadius = 2.5f;
            p.digPower = 90;
            p.color = 0xd8d8e8;
            p.lightRadius = 16;
            p.flags = kRolls | kPiercing | kDigs;
            proj(SpellId::Sawblade, "Sawblade", "Rolls along the ground cutting everything.", 40, 0.35f, 2, 220, p);
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::Star;
            p.speed = 300;
            p.lifetime = 1.4f;
            p.damage = 9;
            p.damageType = DamageType::Holy;
            p.color = 0xfff0a0;
            p.lightRadius = 30;
            p.flags = kHoming;
            proj(SpellId::StarShower, "Glitter Star", "A seeking star that leaves a shimmering wake.", 18, 0.12f, 2, 180, p);
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::Bolt;
            p.speed = 700;
            p.lifetime = 0.5f;
            p.damage = 18;
            p.damageType = DamageType::Fire;
            p.trailMat = mat::Plasma;
            p.trailRate = 1.0f;
            p.color = 0x9ab8ff;
            p.lightRadius = 40;
            p.flags = kPiercing;
            proj(SpellId::Plasma, "Plasma Lance", "A searing piercing beam of plasma.", 50, 0.3f, 3, 300, p);
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::BigOrb;
            p.speed = 140;
            p.lifetime = 3.0f;
            p.gravity = 0.4f;
            p.bounces = 2;
            p.damage = 0;
            p.radius = 3;
            p.explosionRadius = 46;
            p.explosionDamage = 220;
            p.color = 0xfff8e0;
            p.lightRadius = 80;
            proj(SpellId::HolyBomb, "Holy Bomb", "Annihilates everything nearby. Including you.", 300, 2.0f, 4, 650, p)
                .recharge = 2.0f;
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::Spark;
            p.speed = 620;
            p.lifetime = 0.55f;
            p.damage = 0;
            p.color = 0x80ffe0;
            p.lightRadius = 28;
            p.flags = kTeleport;
            proj(SpellId::TeleportBolt, "Teleport Bolt", "Blinks you to wherever it lands.", 40, 0.2f, 1, 170, p);
        }
        {
            ProjectileDef p;
            p.visual = ProjVisual::Spark;
            p.speed = 520;
            p.lifetime = 0.6f;
            p.damage = 11;
            p.damageType = DamageType::Electric;
            p.color = 0xf0f0ff;
            p.lightRadius = 30;
            p.flags = kElectric;
            p.chains = 3;
            proj(SpellId::ArcBolt, "Arc Spark", "An electric spark that leaps between foes.", 20, 0.12f, 2, 200, p);
        }

        // ---- Modifiers -----------------------------------------------------------------
        {
            ModifierDef m;
            m.damageAdd = 8;
            mod(SpellId::DamageUp, "Damage Plus", "Every projectile in the cast hits harder.", 5, 0.05f, 0, 90, m);
        }
        {
            ModifierDef m;
            m.speedMul = 1.9f;
            mod(SpellId::SpeedUp, "Speed Up", "Projectiles fly much faster.", 3, 0.0f, 0, 60, m);
        }
        {
            ModifierDef m;
            m.addFlags = kHoming;
            mod(SpellId::Homing, "Homing", "Projectiles seek the nearest foe.", 25, 0.05f, 1, 190, m);
        }
        {
            ModifierDef m;
            m.addFlags = kPiercing;
            m.damageMul = 0.85f;
            mod(SpellId::Piercing, "Piercing Shot", "Projectiles pass through creatures.", 40, 0.1f, 2, 200, m);
        }
        {
            ModifierDef m;
            m.bouncesAdd = 8;
            mod(SpellId::Bouncy, "Bounce", "Projectiles ricochet off terrain.", 5, 0.0f, 0, 70, m);
        }
        {
            ModifierDef m;
            m.explosionAdd = 7;
            mod(SpellId::Explosive, "Explosive Payload", "Projectiles explode on impact.", 30, 0.15f, 1, 160, m);
        }
        {
            ModifierDef m;
            m.trailMat = mat::Fire;
            mod(SpellId::FireTrail, "Fire Trail", "Leaves a trail of flame.", 10, 0.0f, 1, 100, m);
            m.trailMat = mat::Water;
            mod(SpellId::WaterTrail, "Water Trail", "Leaves a trail of water.", 5, 0.0f, 0, 60, m);
            m.trailMat = mat::Oil;
            mod(SpellId::OilTrail, "Oil Trail", "Leaves a trail of oil.", 5, 0.0f, 1, 70, m);
            m.trailMat = mat::Acid;
            mod(SpellId::AcidTrail, "Acid Trail", "Leaves a dripping trail of acid.", 15, 0.0f, 2, 140, m);
            // Solid trails are laid along the whole path: rime hardens into a bridge of ice.
            m.trailMat = mat::Ice;
            mod(SpellId::RimeTrail, "Rime Trail", "Leaves a bridge of ice along its path.", 15, 0.0f, 1, 120, m);
        }
        {
            ModifierDef m;
            m.gravityAdd = 1.0f;
            m.damageAdd = 7;
            m.speedMul = 0.8f;
            mod(SpellId::Heavy, "Heavy Shot", "Heavier, slower and harder hitting.", 5, 0.1f, 0, 60, m);
        }
        {
            ModifierDef m;
            m.zeroGravity = true;
            m.lifetimeMul = 1.6f;
            mod(SpellId::Weightless, "Weightless", "Projectiles ignore gravity and fly further.", 5, 0.0f, 1, 80, m);
        }
        {
            ModifierDef m;
            m.spreadAdd = -12.0f;
            mod(SpellId::Accuracy, "Focus", "Tightens the spread of the cast.", 2, 0.0f, 0, 40, m);
        }
        {
            ModifierDef m;
            m.critChance = 0.3f;
            mod(SpellId::Critical, "Keen Edge", "30% chance for triple damage.", 8, 0.0f, 1, 150, m);
        }
        {
            ModifierDef m;
            m.addFlags = kIncendiary;
            mod(SpellId::Incendiary, "Incendiary", "Projectiles ignite what they touch.", 10, 0.0f, 1, 110, m);
        }
        {
            ModifierDef m;
            m.addFlags = kFreezing;
            mod(SpellId::Frost, "Frost", "Projectiles freeze liquids and chill foes.", 12, 0.0f, 2, 130, m);
        }
        {
            ModifierDef m;
            m.radiusMul = 2.0f;
            m.damageAdd = 4;
            m.explosionAdd = 3;
            mod(SpellId::Larger, "Enlarge", "Bigger projectiles, bigger blasts.", 15, 0.05f, 2, 150, m);
        }
        {
            ModifierDef m;
            mod(SpellId::ManaRefund, "Mana Siphon", "Restores mana when cast. Slows the wand slightly.", -35, 0.12f, 2, 170,
                m);
        }

        // ---- Multicasts ----------------------------------------------------------------
        multi(SpellId::DoubleCast, "Double Spell", "Casts the next two spells together.", 0, 2, 4.0f, 0, 100);
        multi(SpellId::TripleCast, "Triple Spell", "Casts the next three spells together.", 2, 3, 10.0f, 1, 180);
        multi(SpellId::QuadScatter, "Quad Scatter", "Casts the next four spells in a wide spray.", 4, 4, 34.0f, 2, 220);
        multi(SpellId::Formation, "Trinity Fan", "Casts the next three spells in a precise fan.", 2, 3, 0.0f, 2, 200);
    }
};

const SpellTable& table() {
    static const SpellTable t;
    return t;
}

} // namespace

const SpellDef& spellDef(SpellId id) { return table().defs[static_cast<size_t>(id)]; }

std::string spellDetails(SpellId id) {
    const SpellDef& d = spellDef(id);
    std::string body = d.description;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "  Mana %d.", static_cast<int>(d.mana));
    body += buf;
    if (d.type == SpellType::Projectile) {
        if (d.proj.damage > 0.0f) {
            std::snprintf(buf, sizeof(buf), " Damage %d (%s).", static_cast<int>(d.proj.damage), damageTypeName(d.proj.damageType));
            body += buf;
        }
        if (d.proj.explosionRadius > 0.0f) {
            std::snprintf(buf, sizeof(buf), " Blast %d.", static_cast<int>(d.proj.explosionDamage));
            body += buf;
        }
    }
    if (d.castDelay != 0.0f) {
        std::snprintf(buf, sizeof(buf), " Delay %+.2fs.", d.castDelay);
        body += buf;
    }
    return body;
}

const char* damageTypeName(DamageType t) {
    static constexpr const char* kNames[] = {"Physical", "Fire", "Explosion", "Ice", "Electric", "Acid", "Toxic", "Holy"};
    static_assert(std::size(kNames) == static_cast<size_t>(DamageType::Count), "a name for every DamageType");
    return kNames[static_cast<int>(t)];
}

} // namespace pm
