// Monster data and spawn tables.

#include "game/Enemy.hpp"

#include <cstring>

#include "game/Run.hpp"

namespace pm {

namespace {

using R = std::array<float, static_cast<int>(DamageType::Count)>;
// Resistance order: Physical, Fire, Explosion, Ice, Electric, Acid, Toxic, Holy.
constexpr R kNormal{1, 1, 1, 1, 1, 1, 1, 1};

// clang-format off
const EnemyDef kEnemies[static_cast<int>(EnemyType::Count)] = {
    {"Slime",          SpriteId::Slime,      Behaviour::Hopper,    22, 5.0f, 3.5f,  55,  8, DamageType::Physical, SpellId::None,      1.3f, 150, mat::Slime,  6, false, 0,        {1, 1.2f, 1, 1, 1, 0.5f, 0, 1}},
    {"Frost Slime",    SpriteId::Slime,      Behaviour::Hopper,    32, 5.0f, 3.5f,  60, 10, DamageType::Ice,      SpellId::None,      1.1f, 160, mat::Brine,  9, false, 0x80d0ff, {1, 2.0f, 1, 0, 1, 0.7f, 0.5f, 1}, 0x96d2ff},
    {"Cave Bat",       SpriteId::Bat,        Behaviour::Flyer,     12, 5.0f, 3.0f, 100,  6, DamageType::Physical, SpellId::None,      0.0f, 190, mat::Blood,  4, true,  0,        kNormal},
    {"Ghoul Miner",    SpriteId::Ghoul,      Behaviour::Walker,    42, 3.5f, 6.0f,  40, 12, DamageType::Physical, SpellId::None,      1.0f, 170, mat::Blood, 12, false, 0xffe090, {0.9f, 1.2f, 1, 1, 1, 1, 1, 1.5f}},
    {"Sapper",         SpriteId::Ghoul,      Behaviour::Walker,    36, 3.5f, 6.0f,  36, 10, DamageType::Physical, SpellId::Dynamite,  3.2f, 210, mat::Blood, 18, false, 0xffe090, {0.9f, 1.2f, 0.7f, 1, 1, 1, 1, 1.5f}, 0xffdca0},
    {"Fire Imp",       SpriteId::Imp,        Behaviour::Caster,    28, 4.0f, 5.0f,  72,  8, DamageType::Fire,     SpellId::FireBolt,  1.9f, 180, mat::Ichor, 15, true,  0xff8030, {1, 0, 1, 2.0f, 1, 1, 1, 1}},
    {"Frost Wraith",   SpriteId::Wraith,     Behaviour::Caster,    34, 4.5f, 6.0f,  60,  8, DamageType::Ice,      SpellId::IceShard,  2.1f, 180, mat::Water, 16, true,  0x80e0ff, {0.8f, 1.6f, 1, 0, 1, 1, 0, 1.5f}},
    {"Spore Shroom",   SpriteId::Shroom,     Behaviour::Spore,     30, 5.0f, 6.0f,   0,  5, DamageType::Toxic,    SpellId::None,      2.8f, 100, mat::Spore,  8, false, 0xa070ff, {1, 1.6f, 1, 1, 1, 1, 0, 1}},
    {"Crystal Sentinel",SpriteId::Turret,    Behaviour::Turret,    46, 5.0f, 6.5f,   0,  6, DamageType::Physical, SpellId::SparkBolt, 1.2f, 220, mat::Salt,  20, false, 0x60e0ff, {0.7f, 1, 1.2f, 1, 0.5f, 1, 0, 1}},
    {"Bomb Beetle",    SpriteId::Beetle,     Behaviour::Kamikaze,  14, 5.0f, 3.0f,  62,  0, DamageType::Explosion,SpellId::None,      0.0f, 160, mat::Gunpowder, 8, false, 0xffb040, kNormal},
    {"Rock Worm",      SpriteId::WormHead,   Behaviour::Burrower,  95, 4.5f, 4.5f,  70, 16, DamageType::Physical, SpellId::None,      0.0f, 170, mat::Blood, 30, true,  0,        {0.9f, 1, 0.8f, 1, 1, 1.2f, 1, 1}},
    {"Basalt Golem",   SpriteId::Golem,      Behaviour::Brute,    170, 6.5f, 8.0f,  26, 22, DamageType::Physical, SpellId::None,      3.0f, 190, mat::Gravel, 45, false, 0xff8c3c, {0.6f, 0.5f, 0.7f, 1.2f, 1, 1.5f, 0, 1}},
    {"Homunculus",     SpriteId::Homunculus, Behaviour::Companion, 70, 4.0f, 5.0f,  80,  0, DamageType::Physical, SpellId::SparkBolt, 0.55f, 160, mat::Blood, 0, false, 0,        kNormal},
    {"The Rebis",      SpriteId::Boss,       Behaviour::Boss,    2200, 20.0f, 18.0f, 45, 25, DamageType::Physical, SpellId::Fireball,  2.4f, 400, mat::Ichor, 400, true, 0xffc860, {1, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 1}},
    // Stage guardians: giants of their stage's kind, keeping a lair off the route.
    {"The Black Toad", SpriteId::Slime,      Behaviour::Hopper,   420, 10.0f, 8.5f, 64, 16, DamageType::Physical, SpellId::None,      1.0f, 210, mat::Slime, 120, false, 0,       {1, 1.2f, 0.8f, 1, 1, 0.5f, 0, 1},      0x3a3048, 2, 1, true},
    {"The White Wraith",SpriteId::Wraith,    Behaviour::Caster,   480,  8.0f, 12.0f, 58, 12, DamageType::Ice,     SpellId::IceShard,  1.6f, 220, mat::Steam, 140, true, 0xc8f0ff, {0.8f, 1.8f, 1, 0, 1, 1, 0, 1.5f},     0xf4f8ff, 2, 5, true},
    {"The Mine King",  SpriteId::Ghoul,      Behaviour::Walker,   650,  7.0f, 12.5f, 40, 18, DamageType::Physical, SpellId::Dynamite, 2.6f, 220, mat::Blood, 180, false, 0xffd060, {0.8f, 1.2f, 0.5f, 1, 1, 1, 1, 1.5f},  0xffd27a, 2, 3, true},
    {"The Red Colossus",SpriteId::Golem,     Behaviour::Brute,    900, 12.0f, 15.5f, 30, 26, DamageType::Physical, SpellId::Fireball, 3.4f, 220, mat::Gravel, 220, false, 0xff6020, {0.6f, 0, 0.6f, 1.4f, 1, 1.4f, 0, 1}, 0xff9a7a, 2, 1, true},
    {"Mimic",          SpriteId::Mimic,      Behaviour::Mimic,     50, 5.5f, 4.0f,  78, 12, DamageType::Physical, SpellId::None,      0.8f, 170, mat::Blood, 25, false, 0,        {1, 1.3f, 1, 1, 1, 1, 1, 1}},
};
// clang-format on

struct SpawnEntry {
    EnemyType type;
    float weight;
    float minDifficulty;
};

// Ground and air tables per stage.
const std::vector<SpawnEntry> kGround[5] = {
    {{EnemyType::Slime, 40, 0}, {EnemyType::Ghoul, 30, 0}, {EnemyType::Shroom, 15, 0.2f}, {EnemyType::Beetle, 8, 0.5f}},
    {{EnemyType::FrostSlime, 35, 0}, {EnemyType::Ghoul, 20, 0}, {EnemyType::Turret, 20, 0.2f}, {EnemyType::Golem, 8, 0.6f}},
    {{EnemyType::Sapper, 30, 0}, {EnemyType::Beetle, 25, 0}, {EnemyType::Shroom, 20, 0}, {EnemyType::Worm, 8, 0.4f}, {EnemyType::Slime, 10, 0}},
    {{EnemyType::Beetle, 25, 0}, {EnemyType::Golem, 20, 0.2f}, {EnemyType::Worm, 15, 0.2f}, {EnemyType::Turret, 15, 0}, {EnemyType::Sapper, 15, 0}},
    {{EnemyType::Golem, 25, 0}, {EnemyType::Turret, 20, 0}, {EnemyType::Worm, 15, 0}, {EnemyType::Sapper, 15, 0}, {EnemyType::Beetle, 15, 0}},
};
const std::vector<SpawnEntry> kAir[5] = {
    {{EnemyType::Bat, 85, 0}, {EnemyType::Imp, 15, 0.7f}},
    {{EnemyType::Wraith, 50, 0}, {EnemyType::Bat, 40, 0}},
    {{EnemyType::Bat, 40, 0}, {EnemyType::Imp, 45, 0}},
    {{EnemyType::Imp, 70, 0}, {EnemyType::Wraith, 25, 0}},
    {{EnemyType::Imp, 50, 0}, {EnemyType::Wraith, 50, 0}},
};

} // namespace

const EnemyDef& enemyDef(EnemyType type) { return kEnemies[static_cast<int>(type)]; }

Color enemyTint(EnemyType type) {
    const uint32_t t = enemyDef(type).tint;
    return Color{static_cast<unsigned char>(t >> 16), static_cast<unsigned char>(t >> 8), static_cast<unsigned char>(t), 255};
}

EnemyType guardianFor(int levelIndex) {
    static constexpr EnemyType kGuardians[] = {EnemyType::BlackToad, EnemyType::WhiteWraith, EnemyType::MineKing,
                                               EnemyType::RedColossus};
    return kGuardians[clamp(levelIndex, 0, 3)];
}

int guardianStage(EnemyType type) {
    for (int i = 0; i < 4; ++i)
        if (guardianFor(i) == type) return i;
    return -1;
}

uint32_t essenceColor(int stage) {
    static constexpr uint32_t kColors[] = {0x7ad070, 0xc8f0ff, 0xffd060, 0xff6a48};
    return kColors[clamp(stage, 0, 3)];
}

const char* guardianEpithet(EnemyType type) {
    switch (type) {
    case EnemyType::BlackToad: return "Keeper of the Mire";
    case EnemyType::WhiteWraith: return "Keeper of the Frozen Hall";
    case EnemyType::MineKing: return "Keeper of the Deep Seams";
    case EnemyType::RedColossus: return "Keeper of the Forge";
    default: return "";
    }
}

static_assert(static_cast<size_t>(EnemyType::Count) <= kMaxCreatureTypes, "raise kMaxCreatureTypes");

std::optional<EnemyType> enemyNamed(const char* name) {
    if (name == nullptr) return std::nullopt;
    for (int i = 0; i < static_cast<int>(EnemyType::Count); ++i)
        if (std::strcmp(name, kEnemies[static_cast<size_t>(i)].name) == 0) return static_cast<EnemyType>(i);
    return std::nullopt;
}

EnemyType pickEnemy(int levelIndex, bool air, float difficulty, Rng& rng) {
    const auto& table = (air ? kAir : kGround)[clamp(levelIndex, 0, 4)];
    float total = 0.0f;
    for (const SpawnEntry& e : table)
        if (difficulty >= e.minDifficulty) total += e.weight;
    float r = rng.uniform() * total;
    for (const SpawnEntry& e : table) {
        if (difficulty < e.minDifficulty) continue;
        if (r < e.weight) return e.type;
        r -= e.weight;
    }
    return table.front().type;
}

} // namespace pm
