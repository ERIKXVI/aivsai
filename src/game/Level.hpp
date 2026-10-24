#pragma once
// A level being played: the simulated world plus everything living in it. Gameplay code
// talks to the level through its effects API (explode, dig, castShot, ...), which keeps
// the cell simulation, creatures, particles, sounds and screen effects in sync.

#include <memory>
#include <string>
#include <vector>

#include "audio/Audio.hpp"
#include "core/Rng.hpp"
#include "game/Enemy.hpp"
#include "game/Input.hpp"
#include "game/Pickup.hpp"
#include "game/Player.hpp"
#include "game/Projectile.hpp"
#include "game/Run.hpp"
#include "gen/LevelGenerator.hpp"
#include "sim/Particles.hpp"
#include "sim/World.hpp"

namespace pm {

class ThreadPool;

struct ExplosionSpec {
    float radius = 10.0f;
    float damage = 30.0f;
    uint8_t power = 140;       ///< hardest material destroyed
    bool incendiary = false;
    Team team = Team::Player;
    float knockback = 260.0f;
    bool hurtsOwner = true;    ///< player explosions also hurt the player
};

struct LightFlash {
    Vec2 pos;
    float r, g, b;
    float radius;
    float life, maxLife;
};

struct FloatText {
    Vec2 pos;
    std::string text;
    uint32_t color;
    float life;
    float scale;
};

struct Lantern {
    Vec2 pos;
    float phase;
};

enum class LevelEvent : uint8_t { None, ExitReached, PlayerDied };

class Level {
public:
    Level(LevelLayout layout, RunState& run, Audio& audio, ThreadPool& pool);
    ~Level();

    Level(const Level&) = delete;
    Level& operator=(const Level&) = delete;

    void tick(const PlayerInput& input);

    // --- Access -----------------------------------------------------------------------
    World& world() { return *layout_.world; }
    const World& world() const { return *layout_.world; }
    const LevelLayout& layout() const { return layout_; }
    BiomeId biome() const { return layout_.biome; }
    bool isSanctum() const { return layout_.biome == BiomeId::Sanctum; }
    Player& player() { return *player_; }
    const Player& player() const { return *player_; }
    RunState& run() { return run_; }
    Audio& audio() { return audio_; }
    Rng& rng() { return rng_; }
    ParticleSystem& particles() { return particles_; }
    const ParticleSystem& particles() const { return particles_; }
    std::vector<std::unique_ptr<Enemy>>& enemies() { return enemies_; }
    const std::vector<std::unique_ptr<Enemy>>& enemies() const { return enemies_; }
    std::vector<Projectile>& projectiles() { return projectiles_; }
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    std::vector<Pickup>& pickups() { return pickups_; }
    const std::vector<Pickup>& pickups() const { return pickups_; }
    const std::vector<LightFlash>& flashes() const { return flashes_; }
    const std::vector<FloatText>& floatTexts() const { return floatTexts_; }
    const std::vector<Lantern>& lanterns() const { return lanterns_; }
    float time() const { return time_; }
    uint32_t ticks() const { return ticks_; }
    float shake() const { return shake_; }
    float combatIntensity() const { return intensity_; }
    Vec2 exitPos() const { return layout_.exitPos; }
    LevelEvent pollEvent();
    /// Pickup the player could interact with right now (or nullptr).
    const Pickup* focusedPickup() const;
    const Enemy* boss() const;
    float playerHurtPulse() const { return hurtPulse_; }

    // --- Effects API --------------------------------------------------------------------
    void castShot(const CastShot& shot, Vec2 origin, float angle, Team team, uint32_t ownerId, float damageMul);
    void launch(const ProjectileLaunch& launch, Vec2 origin, float angle, Team team, uint32_t ownerId, float damageMul);
    /// Queues a projectile; it joins the simulation after the current projectile update.
    void addProjectile(const Projectile& p) { pendingProjectiles_.push_back(p); }
    void explode(Vec2 pos, const ExplosionSpec& spec);
    void dig(Vec2 pos, float radius, uint8_t power, bool debris);
    void spawnBlob(Vec2 pos, MatId mat, int amount, Vec2 vel);
    void electrify(Vec2 pos, float damage, Team team);
    void freezeArea(Vec2 pos, float radius);
    void transmuteArea(Vec2 pos, float radius);
    void addFlash(Vec2 pos, uint32_t color, float intensity, float radius, float duration);
    void addShake(float amount) { shake_ = std::min(1.0f, shake_ + amount); }
    void addFloatText(Vec2 pos, const std::string& text, uint32_t color, float scale = 1.0f);
    void damageRadius(Vec2 pos, float radius, float damage, DamageType type, Team team, float knockback,
                      bool hurtsOwnTeam);
    void bleed(Vec2 pos, MatId blood, int amount, Vec2 dir);
    Actor* nearestHostile(Vec2 pos, Team team, float range, uint32_t excludeId = 0);
    bool lineOfSight(Vec2 a, Vec2 b) const;
    Enemy& spawnEnemy(EnemyType type, Vec2 pos);
    void spawnPickup(Pickup p) { pickups_.push_back(std::move(p)); }
    void dropLoot(Vec2 pos, int tier, bool chest);
    uint32_t nextId() { return nextId_++; }
    void onEnemyKilled(Enemy& e);
    void onPlayerDeath();

private:
    void populate();
    void handleSimEvents();
    void updatePickups(float dt);
    void interact();
    void updateEffects(float dt);
    void updateIntensity();

    LevelLayout layout_;
    RunState& run_;
    Audio& audio_;
    ThreadPool& pool_;
    Rng rng_;
    std::unique_ptr<Player> player_;
    std::vector<std::unique_ptr<Enemy>> enemies_;
    std::vector<Projectile> projectiles_;
    std::vector<Projectile> pendingProjectiles_;
    std::vector<Pickup> pickups_;
    ParticleSystem particles_;
    std::vector<LightFlash> flashes_;
    std::vector<FloatText> floatTexts_;
    std::vector<Lantern> lanterns_;
    PlayerInput input_;
    float time_ = 0.0f;
    uint32_t ticks_ = 0;
    float shake_ = 0.0f;
    float intensity_ = 0.0f;
    float hurtPulse_ = 0.0f;
    float exitHold_ = 0.0f;
    uint32_t nextId_ = 1;
    LevelEvent event_ = LevelEvent::None;
    bool playerDeathHandled_ = false;
};

} // namespace pm
