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
    const char* cause = nullptr; ///< reported on the death screen
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

struct Shockwave {
    Vec2 pos;
    float maxRadius;
    float strength;
    float age;
    float duration;
};

/// A runestone basin that grants a perk once enough of the craved material is poured in.
struct Transmuter {
    Vec2 pos;          ///< centre of the basin floor
    MatId wants = 0;
    int required = 120;
    int progress = 0;
    bool complete = false;
};

struct Lantern {
    Vec2 pos;
    float phase;
    float fallSpeed = 0.0f; ///< > 0 once its ceiling is gone and it drops
    bool shattered = false;
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
    const RunState& run() const { return run_; }
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
    const std::vector<Transmuter>& transmuters() const { return transmuters_; }
    const std::vector<Shockwave>& shockwaves() const { return shockwaves_; }
    /// Brief freeze-frame after heavy impacts; returns true while the level should pause.
    bool consumeHitStop() {
        if (hitStop_ <= 0) return false;
        --hitStop_;
        heldSinceTick_ = true;
        return true;
    }
    void addHitStop(int ticks) { hitStop_ = std::min(4, std::max(hitStop_, ticks)); }
    /// True from a hit-stop until the level steps again, so rendering holds the frozen frame
    /// instead of interpolating back towards the previous tick when the last frozen tick ends.
    bool hitStopActive() const { return hitStop_ > 0 || heldSinceTick_; }
    float time() const { return time_; }
    /// Spells picked up on this stage that went to the satchel rather than onto a wand.
    int spellsToSatchel() const { return spellsToSatchel_; }
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
                      bool hurtsOwnTeam, const char* cause = nullptr);
    /// Name of the creature with this id (for death causes), or nullptr.
    const char* actorName(uint32_t id) const;
    void bleed(Vec2 pos, MatId blood, int amount, Vec2 dir);
    Actor* nearestHostile(Vec2 pos, Team team, float range, uint32_t excludeId = 0);
    bool lineOfSight(Vec2 a, Vec2 b) const;
    /// Hanging lanterns within `radius` of the segment a-b are knocked loose: they drop and
    /// shatter into burning oil where they land.
    void knockLanterns(Vec2 a, Vec2 b, float radius);
    Enemy& spawnEnemy(EnemyType type, Vec2 pos);
    void spawnPickup(Pickup p) { pickups_.push_back(std::move(p)); }
    void dropLoot(Vec2 pos, int tier, bool chest);
    uint32_t nextId() { return nextId_++; }
    void onEnemyKilled(Enemy& e);
    /// Applies a perk to the run and rebuilds the player's derived stats, with fanfare.
    void grantPerk(PerkId perk, const char* reason);
    /// A random perk the player can still benefit from.
    PerkId rollPerk(const std::vector<PerkId>& exclude);
    /// Testing aid: moves the player to the next main-path waypoint beyond its position.
    void debugWarpAhead();
    /// Testing aid: places the player at a position.
    void debugTeleport(Vec2 pos);
    void onPlayerDeath();

private:
    void populate();
    void updateEntities(const PlayerInput& input, float dt);
    void handleSimEvents();
    void updatePickups(float dt);
    void interact();
    void updateEffects(float dt);
    /// Props cut loose by the world changing around them: lanterns drop, trees come apart.
    void updateLanterns(float dt);
    void updateTrees();
    void updateIntensity();
    void updateTransmuters();
    /// Biome ambience: drifting dust, ice glitter, spores, embers, fireflies.
    void spawnAmbience();

    LevelLayout layout_;
    std::vector<size_t> treeCellCounts_; ///< per tree: wood and leaf cells left at the last check
    RunState& run_;
    Audio& audio_;
    ThreadPool& pool_;
    Rng rng_;
    Rng fxRng_; ///< cosmetic randomness, kept apart so decoration never shifts gameplay
    std::unique_ptr<Player> player_;
    std::vector<std::unique_ptr<Enemy>> enemies_;
    std::vector<Projectile> projectiles_;
    std::vector<Projectile> pendingProjectiles_;
    std::vector<Pickup> pickups_;
    ParticleSystem particles_;
    std::vector<LightFlash> flashes_;
    std::vector<FloatText> floatTexts_;
    std::vector<Lantern> lanterns_;
    std::vector<Transmuter> transmuters_;
    std::vector<Shockwave> shockwaves_;
    int hitStop_ = 0;
    bool heldSinceTick_ = false; ///< a hit-stop tick was consumed and the level has not stepped since
    PlayerInput input_;
    float time_ = 0.0f;
    int spellsToSatchel_ = 0;
    uint32_t ticks_ = 0;
    float shake_ = 0.0f;
    float intensity_ = 0.0f;
    float hurtPulse_ = 0.0f;
    float exitHold_ = 0.0f;
    uint32_t nextId_ = 1;
    LevelEvent event_ = LevelEvent::None;
    bool playerDeathHandled_ = false;
    bool exitTriggered_ = false;
    size_t debugWarpIndex_ = 0;
};

} // namespace pm
