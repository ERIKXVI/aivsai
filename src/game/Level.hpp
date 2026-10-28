#pragma once
// A level being played: the simulated world plus everything living in it. Gameplay code
// talks to the level through its effects API (explode, dig, castShot, ...), which keeps
// the cell simulation, creatures, particles, sounds and screen effects in sync.

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "audio/Audio.hpp"
#include "core/Rng.hpp"
#include "game/Enemy.hpp"
#include "game/Feats.hpp"
#include "game/Input.hpp"
#include "game/Pickup.hpp"
#include "game/Player.hpp"
#include "game/Projectile.hpp"
#include "game/Run.hpp"
#include "game/StageEvents.hpp"
#include "gen/LevelGenerator.hpp"
#include "sim/Particles.hpp"
#include "sim/World.hpp"

namespace pm {

class ThreadPool;
struct SpriteFrame;

struct ExplosionSpec {
    float radius = 10.0f;
    float damage = 30.0f;
    uint8_t power = 140;       ///< hardest material destroyed
    bool incendiary = false;
    Team team = Team::Player;
    float knockback = 260.0f;
    bool hurtsOwner = true;    ///< player explosions also hurt the player
    const char* cause = nullptr; ///< reported on the death screen
    bool crit = false;         ///< a critical blast: treble damage to all but the caster's side
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
    uint32_t owner = 0; ///< for damage numbers: the actor hit (0: plain text)
    float value = 0.0f; ///< ...the damage summed so far
    bool crit = false;  ///< ...and whether any of it was critical
    float pop = 0.0f;   ///< 1 right after it grew, easing to 0: drawn larger
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
    uint32_t id = 0; ///< stable light key, assigned when the level is built
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
    /// A critical hit lands: a one-tick freeze (at most every fifth of a second, so critical
    /// wands do not stutter), a small flash and a bright ring.
    void critPunch(Vec2 pos);
    /// True from a hit-stop until the level steps again, so rendering holds the frozen frame
    /// instead of interpolating back towards the previous tick when the last frozen tick ends.
    bool hitStopActive() const { return hitStop_ > 0 || heldSinceTick_; }
    float time() const { return time_; }
    /// Cosmetic randomness: decoration drawn from it never shifts gameplay.
    Rng& fxRng() { return fxRng_; }
    /// Feats earned since the last call; the scene records and announces them.
    std::vector<FeatId> takeFeats() { return std::exchange(feats_, {}); }
    void earnFeat(FeatId feat) { feats_.push_back(feat); }
    /// Spells picked up on this stage that went to the satchel rather than onto a wand.
    int spellsToSatchel() const { return spellsToSatchel_; }
    /// The spell most recently picked up into the satchel (None before the first).
    SpellId lastToSatchel() const { return lastToSatchel_; }
    uint32_t ticks() const { return ticks_; }
    float shake() const { return shake_; }
    float combatIntensity() const { return intensity_; }
    Vec2 exitPos() const { return layout_.exitPos; }
    LevelEvent pollEvent();
    /// Pickup the player could interact with right now (or nullptr).
    const Pickup* focusedPickup() const;
    const Enemy* boss() const;
    /// The nearest living lair guardian within `radius` of `pos`, if any.
    const Enemy* nearestGuardian(Vec2 pos, float radius) const;
    /// Whose health the HUD shows across the top: the Rebis, or a guardian in a fight nearby.
    const Enemy* bossBarTarget() const;
    float playerHurtPulse() const { return hurtPulse_; }
    StageEvents& events() { return events_; }
    const StageEvents& events() const { return events_; }

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
    /// A slain creature bursts into the pixels of its sprite.
    void shatter(const Enemy& e);
    /// A creature the world slew (fire, lava, acid, a blast, a cave-in, another creature) is named
    /// on screen, counted, chained with other such kills and paid a little gold.
    void creditAlchemy(const Enemy& e);
    /// A damage number over actor `owner`. Hits landing within a moment of the last one add up
    /// into the same number, which pops, instead of stacking a column of small ones.
    void addDamageNumber(uint32_t owner, Vec2 pos, float amount, bool crit);
    /// Hurts every creature within `radius` of `pos`, less towards the rim. A critical blast (`crit`)
    /// trebles the damage to those not on `team`; the caster's own side is never hit harder.
    void damageRadius(Vec2 pos, float radius, float damage, DamageType type, Team team, float knockback,
                      bool hurtsOwnTeam, const char* cause = nullptr, bool crit = false);
    /// Name of the creature with this id (for death causes), or nullptr.
    const char* actorName(uint32_t id) const;
    void bleed(Vec2 pos, MatId blood, int amount, Vec2 dir);
    /// Nearest living, undisguised actor not on `team` within `range` of `pos` (skipping
    /// `excludeId`), or nullptr.
    const Actor* nearestHostile(Vec2 pos, Team team, float range, uint32_t excludeId = 0) const;
    Actor* nearestHostile(Vec2 pos, Team team, float range, uint32_t excludeId = 0) {
        return const_cast<Actor*>(std::as_const(*this).nearestHostile(pos, team, range, excludeId));
    }
    bool lineOfSight(Vec2 a, Vec2 b) const;
    /// Hanging lanterns within `radius` of the segment a-b are knocked loose: they drop and
    /// shatter into burning oil where they land.
    void knockLanterns(Vec2 a, Vec2 b, float radius);
    Enemy& spawnEnemy(EnemyType type, Vec2 pos);
    void spawnPickup(Pickup p) { pickups_.push_back(std::move(p)); }
    /// A chest's treasure (or a mimic's): coins and a find, two with Lucky Day. It is rolled from the chest's own
    /// `lootSeed`, never the level's stream, so a replayed seed holds the same treasure however the stage went.
    void dropLoot(Vec2 pos, int tier, uint32_t lootSeed);
    uint32_t nextId() { return nextId_++; }
    void onEnemyKilled(Enemy& e);
    /// Applies a perk to the run and rebuilds the player's derived stats, with fanfare. A Homunculus
    /// joins at once. Never call it while iterating enemies(): it may spawn one.
    void grantPerk(PerkId perk, const char* reason);
    /// A slain keeper's essence: two perk altars in its lair, its signature gift and one other.
    void offerEssence(const Enemy& keeper);
    static constexpr int kEssenceAltars = 1; ///< altar group of a keeper's essence (the sanctum's is 0)
    /// A random perk the player can still benefit from.
    PerkId rollPerk(const std::vector<PerkId>& exclude);
    /// Testing aid: moves the player to the next main-path waypoint beyond its position.
    void debugWarpAhead();
    /// Testing aid: places the player at a position.
    void debugTeleport(Vec2 pos);
    void onPlayerDeath();

private:
    void populate();
    Affix rollAffix();
    /// The Rebis is slain: the creatures' shots fizzle and every creature still hostile dissolves (no credit, gold,
    /// feat or gameplay randomness), and nothing can undo the alchemist for the rest of the stage.
    void completeGreatWork();
    /// The float text tagged `owner` whose life is at least `minLife`, or else a new one so tagged at `pos` (its text and look
    /// are the caller's to set).
    FloatText& taggedFloatText(uint32_t owner, Vec2 pos, float minLife = 0.0f);
    /// Bursts up to `budget` of a sprite frame's pixels (drawn at `topLeft`, `scale`, `flip`,
    /// tinted) outward from `centre` and along `away`. Cosmetic randomness only.
    void shatterPixels(const SpriteFrame& frame, Vec2 topLeft, float scale, bool flip, Vec2 centre, Rgba tint, Vec2 away,
                       size_t budget);
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
    StageEvents events_;
    int hitStop_ = 0;
    uint32_t lastCritPunch_ = 0; ///< tick of the last critical-hit freeze
    int alchemyChain_ = 0;          ///< world kills in the current chain reaction
    uint32_t alchemyChainTick_ = 0; ///< tick of the chain's latest kill
    int alchemyBonuses_ = 0;        ///< bonus gold drops paid this stage
    bool heldSinceTick_ = false; ///< a hit-stop tick was consumed and the level has not stepped since
    PlayerInput input_;
    float time_ = 0.0f;
    int spellsToSatchel_ = 0;
    SpellId lastToSatchel_ = SpellId::None;
    std::vector<FeatId> feats_;
    std::array<float, 3> recentKills_{-9.0f, -9.0f, -9.0f}; ///< times of the last three kills (Carnage)
    size_t killRing_ = 0;
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
