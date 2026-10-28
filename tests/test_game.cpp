// Game-rule tests: spells and wand decks, loadouts and their unlocks, run saves, records and the end and death screens,
// and rules played out in a Level: lanterns, guardians, elites and the boss, feats, the Alchemical Codex, elixirs,
// mimics, stage events, ordeals, loot tables and shops, and kill credit.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "app/Persistence.hpp"
#include "app/RunRecords.hpp"
#include "audio/Audio.hpp"
#include "core/ThreadPool.hpp"
#include "core/Timing.hpp"
#include "game/Actor.hpp"
#include "game/Body.hpp"
#include "game/Codex.hpp"
#include "game/Enemy.hpp"
#include "game/Feats.hpp"
#include "game/Level.hpp"
#include "gen/Biome.hpp"
#include "gen/LevelGenerator.hpp"
#include "sim/World.hpp"
#include "game/Loadouts.hpp"
#include "game/Player.hpp"
#include "game/Run.hpp"
#include "game/Spells.hpp"
#include "game/Wand.hpp"
#include "scenes/Hints.hpp"
#include "tests/TestFramework.hpp"

using namespace pm;

namespace {

CastShot castOnce(std::vector<SpellId> slots) {
    std::vector<int> order;
    int pos = 0;
    return buildShot(slots, order, pos, 1);
}

/// The smallest concrete actor, for exercising Actor's shared logic.
struct Dummy final : Actor {
    void tick(Level&) override {}
    void drawAlbedo(DrawContext&) const override {}
};

RunState freshRun(uint32_t seed, Difficulty d) {
    initMaterials();
    return newRun(seed, d, false);
}

/// A fresh run standing in a stage's sanctum, with the worker pool and silent audio a Level needs.
struct SanctumRun {
    explicit SanctumRun(uint32_t seed, int stage = 0, Difficulty difficulty = Difficulty::Adept)
        : run(freshRun(seed, difficulty)), level(generateSanctum(stage, seed, pool), run, audio, pool) {}
    SanctumRun(const SanctumRun&) = delete;
    SanctumRun& operator=(const SanctumRun&) = delete;
    ThreadPool pool{2};
    Audio audio;
    RunState run;
    Level level;
};

} // namespace

TEST_CASE("a spell's figures read as one line") {
    const std::string fire = spellStats(SpellId::FireBolt);
    CHECK(fire.find("Damage ") == 0);
    CHECK(fire.find("(Fire)  Mana") != std::string::npos);
    CHECK(spellStats(SpellId::DamageUp).find("Mana") == 0); // a modifier has no damage of its own
    CHECK(spellStats(SpellId::Bomb).find("Blast") != std::string::npos);
}

TEST_CASE("every spell is fully described") {
    for (int i = 1; i < kSpellCount; ++i) {
        const SpellDef& d = spellDef(static_cast<SpellId>(i));
        CHECK(d.name != nullptr && std::strlen(d.name) > 0);
        CHECK(d.description != nullptr && std::strlen(d.description) > 0);
        CHECK(d.tier >= 0 && d.tier <= 4);
        CHECK(d.price > 0);
        CHECK(d.mana >= 0.0f || d.type == SpellType::Modifier); // negative = mana refund
        if (d.type == SpellType::Projectile) {
            CHECK(d.proj.lifetime > 0.0f);
            CHECK(d.proj.speed > 0.0f || (d.proj.flags & projflag::kHitscan));
        }
        if (d.type == SpellType::Multicast) CHECK(d.multicast >= 2);
    }
}

TEST_CASE("wand decks cast modifiers, multicasts and triggers") {
    // A modifier boosts the projectile that follows it.
    CastShot plain = castOnce({SpellId::SparkBolt});
    CastShot boosted = castOnce({SpellId::DamageUp, SpellId::SparkBolt});
    REQUIRE(plain.projectiles.size() == 1 && boosted.projectiles.size() == 1);
    CHECK(boosted.projectiles[0].def.damage > plain.projectiles[0].def.damage);
    CHECK(boosted.mana > plain.mana);

    // A multicast draws several cards into one cast.
    CastShot multi = castOnce({SpellId::DoubleCast, SpellId::SparkBolt, SpellId::MagicMissile, SpellId::Bomb});
    CHECK(multi.projectiles.size() == 2);

    // A trigger carries the next card as its impact payload.
    CastShot trig = castOnce({SpellId::SparkTrigger, SpellId::Bomb});
    REQUIRE(trig.projectiles.size() == 1);
    REQUIRE(trig.projectiles[0].payload != nullptr);
    CHECK(trig.projectiles[0].payload->projectiles.size() == 1);

    // Casting walks the deck and recharges once it is exhausted.
    Wand w = makeStarterWand(0);
    Rng rng(1);
    int casts = 0;
    for (int i = 0; i < 3; ++i) {
        CastOutcome o = w.tryCast(rng);
        casts += o.status == CastStatus::Cast ? 1 : 0;
        w.cooldown = 0.0f;
    }
    CHECK(casts == 3);
    CHECK(w.recharging);
}

TEST_CASE("a booster dropped onto a spell goes in front of it") {
    std::vector<SpellId> deck{SpellId::SparkBolt, SpellId::None, SpellId::FireBolt, SpellId::None};
    CHECK(trailingBooster(deck) == SpellId::None);
    REQUIRE(insertSpell(deck, 0, SpellId::DamageUp));
    const std::vector<SpellId> boosted{SpellId::DamageUp, SpellId::SparkBolt, SpellId::FireBolt, SpellId::None};
    CHECK(deck == boosted);
    CHECK(castOnce(deck).projectiles[0].def.damage > castOnce({SpellId::SparkBolt}).projectiles[0].def.damage);
    // Only an empty slot further along makes room; a full tail leaves the deck alone.
    REQUIRE(insertSpell(deck, 1, SpellId::Homing));
    CHECK(!insertSpell(deck, 0, SpellId::SpeedUp));
    const std::vector<SpellId> full{SpellId::DamageUp, SpellId::Homing, SpellId::SparkBolt, SpellId::FireBolt};
    CHECK(deck == full);
    // A modifier or multicast at the end of the deck has nothing to work on.
    const std::vector<SpellId> dangling{SpellId::SparkBolt, SpellId::DoubleCast, SpellId::None};
    CHECK(trailingBooster(dangling) == SpellId::DoubleCast);
    CHECK(trailingBooster(std::vector<SpellId>(3, SpellId::None)) == SpellId::None);
}

TEST_CASE("a trailing modifier is flagged only when its cast has nothing to boost") {
    // Cast alone it fires nothing; drawn into the last cast with a projectile, it boosts that.
    const std::vector<SpellId> pair{SpellId::SparkBolt, SpellId::DamageUp};
    CHECK(trailingBooster(pair) == SpellId::DamageUp);
    CHECK(trailingBooster(pair, 2) == SpellId::None);
    CHECK(trailingBooster({SpellId::DoubleCast, SpellId::SparkBolt, SpellId::DamageUp}) == SpellId::None);
    // A trigger's empty payload swallows it; once the trigger carries a spell, a second draw boosts the trigger.
    CHECK(trailingBooster({SpellId::SparkTrigger, SpellId::DamageUp}) == SpellId::DamageUp);
    CHECK(trailingBooster({SpellId::SparkTrigger, SpellId::SparkBolt, SpellId::DamageUp}, 2) == SpellId::None);
}

TEST_CASE("the guided first edit takes a worthy satchel spell to the hardest-hitting wand with room") {
    RunState run = newRun(1, Difficulty::Adept, false);
    run.wands = {makeStarterWand(0), makeStarterWand(1)}; // Rod: 3 Spark Bolts and an empty slot; Delver's: Digging Bolt, 2 empty
    run.inventory.fill(SpellId::None);
    run.inventory[2] = SpellId::DamageUp;
    WandGuide g = pickWandGuide(run);
    CHECK(g.active());
    CHECK(g.from == 2);
    CHECK(g.booster);
    CHECK(g.wand == 0); // Spark Bolts outhit the Digging Bolt
    CHECK(g.slot == 0); // in front of the Rod's first attack spell
    run.wands[0].shuffle = true; // a shuffle wand takes no booster...
    CHECK(!pickWandGuide(run).active()); // ...nor the Delver's Wand: its Digging Bolt is no attack to boost
    run.wands[1].slots = {SpellId::DiggingBolt, SpellId::SparkBolt, SpellId::None};
    g = pickWandGuide(run);
    CHECK(g.wand == 1);
    CHECK(g.slot == 1); // in front of the Spark Bolt, not the Digging Bolt
    run.wands[1] = makeStarterWand(1);
    run.wands[0].shuffle = false;
    run.inventory[2] = SpellId::SparkBolt; // an attack spell goes into an empty slot
    g = pickWandGuide(run);
    CHECK(!g.booster);
    CHECK(g.wand == 0);
    CHECK(g.slot == 3);
    run.wands[0].slots[3] = SpellId::SparkBolt;
    run.wands[1].slots = {SpellId::DiggingBolt, SpellId::SparkBolt, SpellId::SparkBolt};
    CHECK(!pickWandGuide(run).active()); // no room on any wand
}

TEST_CASE("the Apprentice's first satchel card varies with the seed") {
    std::vector<SpellId> seen;
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        const RunState run = newRun(seed, Difficulty::Adept, false);
        CHECK(guideCandidate(run.inventory[0])); // always one the guided first edit can teach with
        if (std::find(seen.begin(), seen.end(), run.inventory[0]) == seen.end()) seen.push_back(run.inventory[0]);
    }
    CHECK(seen.size() >= 3);
    // Other kits pack their own satchel, and the Grimoire never hears of the card.
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        const RunState storm = newRun(seed, Difficulty::Adept, false, LoadoutId::Stormcaller);
        CHECK(!((storm.spellsSeen >> static_cast<unsigned>(SpellId::Critical)) & 1ull));
        CHECK(!((storm.spellsSeen >> static_cast<unsigned>(SpellId::IceShard)) & 1ull));
    }
}

TEST_CASE("every loadout builds a consistent kit") {
    for (int d = 0; d < kDifficultyCount; ++d) {
        for (int l = 0; l < kLoadoutCount; ++l) {
            RunState run = newRun(1234u + static_cast<uint32_t>(l), difficultyFromIndex(d), false, static_cast<LoadoutId>(l));
            CHECK(run.loadout == static_cast<LoadoutId>(l));
            CHECK(run.wands.size() == 2);
            for (const Wand& w : run.wands) {
                CHECK(static_cast<int>(w.slots.size()) == w.capacity);
                CHECK(w.spellCount() > 0);
                CHECK(w.mana > 0.0f && w.mana <= w.manaMax);
                for (SpellId s : w.slots)
                    if (s != SpellId::None) CHECK((run.spellsSeen >> static_cast<unsigned>(s)) & 1ull);
            }
            CHECK(run.maxHp > 0.0f && run.hp == run.maxHp);
            // Every kit can blast through brick and basalt: a bomb in the satchel (or a quarry wand).
            const bool bomb = std::find(run.inventory.begin(), run.inventory.end(), SpellId::Bomb) != run.inventory.end();
            CHECK(bomb || static_cast<LoadoutId>(l) == LoadoutId::Geomancer);
            CHECK(run.flask.amount >= 0 && run.flask.amount <= Flask::kCapacity);
        }
    }
    // Hermeticist wands are rolled from the seed: same seed, same wand.
    RunState a = newRun(77u, Difficulty::Adept, false, LoadoutId::Hermeticist);
    RunState b = newRun(77u, Difficulty::Adept, false, LoadoutId::Hermeticist);
    CHECK(a.wands[0].slots == b.wands[0].slots);
    // ...and the 'wand of power' always strikes: some projectile that hurts (no digging or blinking bolt).
    for (uint32_t seed = 1; seed <= 300; ++seed) {
        const RunState h = newRun(seed, Difficulty::Adept, false, LoadoutId::Hermeticist);
        const auto& slots = h.wands[0].slots;
        const bool strikes = std::any_of(slots.begin(), slots.end(), [](SpellId s) {
            if (s == SpellId::None || s == SpellId::DiggingBolt || s == SpellId::Chainsaw || s == SpellId::TeleportBolt) return false;
            const SpellDef& d = spellDef(s);
            return d.type == SpellType::Projectile && (d.proj.damage >= 6.0f || d.proj.explosionRadius > 0.0f);
        });
        if (!strikes) std::printf("    Hermeticist seed %u starts without an attack\n", seed);
        CHECK(strikes);
    }
}

TEST_CASE("found attack spells go straight onto the active wand") {
    initMaterials(); // the filter reads what a spell's spilled matter does
    RunState run = newRun(5u, Difficulty::Adept, false, LoadoutId::Apprentice);
    // The digging wand only digs; its bomb waits in the satchel.
    for (SpellId s : run.wands[1].slots) CHECK(s != SpellId::Bomb);
    CHECK(std::find(run.inventory.begin(), run.inventory.end(), SpellId::Bomb) != run.inventory.end());

    run.activeWand = 0;
    CHECK(run.wands[0].slots[3] == SpellId::None);
    CHECK(run.canAutoSlot(SpellId::FireBolt)); // what the pickup prompt promises
    CHECK(run.trySlotIntoActiveWand(SpellId::FireBolt));
    CHECK(run.wands[0].slots[3] == SpellId::FireBolt);
    CHECK((run.spellsSeen >> static_cast<unsigned>(SpellId::FireBolt)) & 1ull);
    // The rod is full now, so the next spell is left for the satchel.
    CHECK(!run.canAutoSlot(SpellId::IceShard));
    CHECK(!run.trySlotIntoActiveWand(SpellId::IceShard));
    // Explosives, modifiers and teleports are never slotted unasked.
    run.activeWand = 1;
    CHECK(!run.trySlotIntoActiveWand(SpellId::Bomb));
    CHECK(!run.trySlotIntoActiveWand(SpellId::DamageUp));
    CHECK(!run.trySlotIntoActiveWand(SpellId::TeleportBolt));
    CHECK(!run.trySlotIntoActiveWand(SpellId::AcidGlob)); // spills acid at the caster's feet
    CHECK(!run.trySlotIntoActiveWand(SpellId::Fireball));
    CHECK(run.trySlotIntoActiveWand(SpellId::IceShard));
}

TEST_CASE("loadouts unlock through milestones") {
    Milestones fresh;
    CHECK(loadoutUnlocked(LoadoutId::Apprentice, fresh));
    for (int l = 1; l < kLoadoutCount; ++l) {
        CHECK(!loadoutUnlocked(static_cast<LoadoutId>(l), fresh));
        CHECK(!loadoutRequirement(static_cast<LoadoutId>(l), fresh).empty());
    }
    Milestones veteran{5, 1, 40, 1000, 2};
    for (int l = 0; l < kLoadoutCount; ++l) CHECK(loadoutUnlocked(static_cast<LoadoutId>(l), veteran));
    // The profile's lifetime counters feed the same fields (all ints, so a swap would compile).
    Profile p;
    p.deepest = 5;
    p.victories = 1;
    p.chestsOpened = 40;
    p.totalKills = 1000;
    p.creatureKills[static_cast<size_t>(EnemyType::MineKing)] = 2;
    p.creatureKills[static_cast<size_t>(EnemyType::Ghoul)] = 30; // not a guardian
    const Milestones m = milestonesOf(p);
    CHECK(m.deepest == 5 && m.victories == 1 && m.chestsOpened == 40 && m.totalKills == 1000 && m.guardiansSlain == 2);
    // The Hermeticist opens to a guardian's slayer, before any victory...
    Milestones slayer;
    slayer.guardiansSlain = 1;
    CHECK(loadoutUnlocked(LoadoutId::Hermeticist, slayer));
    // ...and stays open for a victor from before, who earned it under the old rule.
    Profile victor;
    victor.victories = 1;
    CHECK(loadoutUnlocked(LoadoutId::Hermeticist, milestonesOf(victor)));
}

TEST_CASE("suspended runs survive a save and load") {
    RunState run = newRun(4242u, Difficulty::Archmage, false, LoadoutId::Hermeticist);
    run.levelIndex = 3;
    run.inSanctum = true;
    run.gold = 777;
    run.hp = 41.5f;
    run.activeWand = 1;
    run.inventory[5] = SpellId::Lightning;
    run.flask.mat = mat::Vitae;
    run.flask.amount = 333;
    run.perks[static_cast<size_t>(PerkId::Vitality)] = 2;
    run.perksSeen = (1u << static_cast<unsigned>(PerkId::Vitality)) | (1u << static_cast<unsigned>(PerkId::SecondLife)); // one spent
    run.stats.kills = 57;
    run.stats.time = 1234.5f;
    run.killsByType[3] = 9;
    run.wands[0].name = "Staff of the Rebis";
    run.ordeal = 4;
    run.essences = 0b0101;
    run.stats.alchemicalKills = 6;

    const auto path = std::filesystem::temp_directory_path() / "pm_test_run.sav";
    writeRunSave(run, path);
    const std::optional<RunState> loaded = readRunSave(path);
    REQUIRE(loaded.has_value());
    const RunState& r = *loaded;
    CHECK(r.seed == run.seed && r.difficulty == run.difficulty && r.loadout == run.loadout && r.ordeal == 4 && r.essences == 0b0101);
    CHECK(r.levelIndex == 3 && r.inSanctum && r.gold == 777 && r.activeWand == 1);
    CHECK(r.hp == run.hp && r.maxHp == run.maxHp);
    CHECK(r.wands.size() == run.wands.size());
    for (size_t i = 0; i < r.wands.size() && i < run.wands.size(); ++i) {
        CHECK(r.wands[i].name == run.wands[i].name);
        CHECK(r.wands[i].slots == run.wands[i].slots);
        CHECK(r.wands[i].castDelay == run.wands[i].castDelay && r.wands[i].manaMax == run.wands[i].manaMax);
    }
    CHECK(r.inventory == run.inventory);
    CHECK(r.flask.mat == mat::Vitae && r.flask.amount == 333);
    CHECK(r.perks == run.perks && r.perksSeen == run.perksSeen);
    CHECK(r.stats.kills == 57 && r.stats.time == run.stats.time && r.stats.alchemicalKills == 6);
    CHECK(r.killsByType == run.killsByType && r.spellsSeen == run.spellsSeen);

    // A save from before alchemical kills were counted (eight stats) and perks seen were kept
    // still loads; the perks it holds count as seen.
    {
        KeyValues kv = readKeyValues(path);
        std::string& stats = kv["stats"];
        stats.erase(stats.rfind(','));
        kv.erase("perks_seen");
        writeKeyValues(path, kv);
    }
    const std::optional<RunState> older = readRunSave(path);
    REQUIRE(older.has_value());
    uint32_t held = 0;
    for (size_t i = 0; i < run.perks.size(); ++i)
        if (run.perks[i] > 0) held |= 1u << i;
    CHECK(older->perksSeen == held);
    CHECK(older->stats.kills == 57 && older->stats.alchemicalKills == 0);

    // A damaged file is rejected rather than half-loaded.
    {
        std::ofstream out(path, std::ios::trunc);
        out << "version=1\nseed=12\nwands=2\nwand0=broken\n";
    }
    CHECK(!readRunSave(path).has_value());
    removeKeyValues(path);
    CHECK(!readRunSave(path).has_value()); // no save at all
}

TEST_CASE("the death screen names the killer and offers advice") {
    Dummy victim;
    victim.team = Team::Player;
    CHECK(deathCause(victim).empty());
    CHECK(deathAdvice(victim) == nullptr);

    victim.lastHitCause = enemyDef(EnemyType::Imp).name;
    CHECK(deathCause(victim) == "Slain by a Fire Imp");
    CHECK(deathAdvice(victim) != nullptr);

    victim.lastHitCause = "Lava";
    victim.lastHitType = DamageType::Fire;
    CHECK(deathCause(victim) == "Undone by lava");
    REQUIRE(deathAdvice(victim) != nullptr);
    CHECK(std::strstr(deathAdvice(victim), "obsidian") != nullptr);

    victim.lastHitCause = "Bomb";
    victim.lastHitSelf = true;
    CHECK(deathCause(victim) == "Undone by your own Bomb");
    REQUIRE(deathAdvice(victim) != nullptr);
    CHECK(std::strstr(deathAdvice(victim), "Your own spells") != nullptr);
}

TEST_CASE("the end screen shows the nearest locked path and personal bests") {
    Profile profile;
    profile.runs = 3;
    profile.deepest = 0;
    profile.chestsOpened = 5;
    profile.bestKills = 10;
    RunState run = newRun(9, Difficulty::Adept, false);
    run.levelIndex = 0;
    run.stats.kills = 14;
    run.stats.chestsOpened = 2;
    const RunRecord record = foldRun(profile, run, false, "Slain by a Fire Imp");
    // Chests 7/8 beats reaching the second stage (1/2) and slaying 14/100.
    CHECK(record.nearestPath.has_value());
    CHECK(record.nearestPath->name == "Geomancer");
    CHECK(record.nearestPath->was == 5 && record.nearestPath->now == 7 && record.nearestPath->target == 8);
    CHECK(std::find(record.records.begin(), record.records.end(), "Deepest descent yet") != record.records.end());
    CHECK(std::find(record.records.begin(), record.records.end(), "Most creatures slain in a run") != record.records.end());
    CHECK(profile.bestKills == 14 && profile.runs == 4 && profile.history.size() == 1);

    // A very first run sets every best, which is no news.
    Profile fresh;
    CHECK(foldRun(fresh, run, false, "Perished").records.empty());

    // Reaching the second stage opens the Pyromancer, named by id for the end screen's offer.
    run.levelIndex = 1;
    run.stats.chestsOpened = 0; // the Geomancer stays sealed (7 of 8 chests)
    const RunRecord deeper = foldRun(profile, run, false, "Slain by a Frost Wraith");
    CHECK(deeper.unlockedPaths.size() == 1 && deeper.unlockedPaths.front() == LoadoutId::Pyromancer);
}

TEST_CASE("chronicle entries survive encoding and reject garbage") {
    RunHistoryEntry e;
    e.seed = 0xDEADBEEFu;
    e.loadout = 3;
    e.difficulty = 2;
    e.depth = 3;
    e.sanctum = true;
    e.daily = true;
    e.time = 1234.0f;
    e.kills = 77;
    e.fate = "Undone by your own Bomb | twice";
    RunHistoryEntry back;
    CHECK(decodeHistoryEntry(encodeHistoryEntry(e), back));
    CHECK(back.seed == e.seed && back.loadout == 3 && back.difficulty == 2 && back.depth == 3);
    CHECK(back.sanctum && back.daily && !back.victory);
    CHECK(back.time == 1234.0f && back.kills == 77);
    CHECK(back.fate == e.fate);

    CHECK(!decodeHistoryEntry("", back));
    CHECK(!decodeHistoryEntry("1|2|3", back));
    CHECK(!decodeHistoryEntry("1|0|9|0|0|0|0|10|5|fate", back));   // difficulty out of range
    CHECK(!decodeHistoryEntry("1|0|1|0|0|0|0|ten|5|fate", back)); // not a number
}

TEST_CASE("hitscan lightning follows the aim") {
    initMaterials();
    ThreadPool pool(2);
    Audio audio; // no device in tests: plays nothing
    RunState run = newRun(9, Difficulty::Adept, false);
    LevelLayout layout = generateSanctum(0, 9u, pool);
    const Vec2 origin = layout.playerStart - Vec2{0.0f, 30.0f};
    Level level(std::move(layout), run, audio, pool);
    const CastShot shot = castOnce({SpellId::Lightning});
    CHECK(!shot.projectiles.empty());
    // Straight down: the beam must end below the caster, not off to the right.
    level.launch(shot.projectiles.front(), origin, kPi * 0.5f, Team::Player, 0, 1.0f);
    CHECK(!level.flashes().empty());
    const Vec2 end = level.flashes().back().pos;
    CHECK(end.y > origin.y + 5.0f);
    CHECK(std::fabs(end.x - origin.x) < 2.0f);
    // Straight left.
    level.launch(shot.projectiles.front(), origin, kPi, Team::Player, 0, 1.0f);
    CHECK(level.flashes().back().pos.x < origin.x - 5.0f);
}

TEST_CASE("rime-trailed bolts fly over their own ice and each other's, but not over plain ice") {
    SanctumRun s(15);
    Level& level = s.level;
    World& w = level.world();
    // A sealed corridor in the brick under the sanctum floor, far from the player, ending in plain ice.
    constexpr int kRow = 205, kStartX = 40, kWallX = 180;
    for (int y = kRow - 2; y <= kRow + 2; ++y) {
        for (int x = kStartX; x < kWallX; ++x) w.clear(x, y);
        w.place(kWallX, y, mat::Ice);
    }
    const CastShot shot = castOnce({SpellId::RimeTrail, SpellId::SparkBolt});
    REQUIRE(shot.projectiles.size() == 1);
    REQUIRE(shot.projectiles.front().def.trailMat == mat::Ice);
    // Two bolts from one tip, as a multicast fires them: the first lays ice across the second's path.
    const Vec2 tip{static_cast<float>(kStartX) + 10.5f, static_cast<float>(kRow) + 0.5f};
    level.launch(shot.projectiles.front(), tip, 0.0f, Team::Player, 0, 1.0f);
    level.launch(shot.projectiles.front(), tip, 0.0f, Team::Player, 0, 1.0f);
    for (int i = 0; i < 12; ++i) level.tick(PlayerInput{});
    CHECK(level.projectiles().size() == 2);
    for (const Projectile& p : level.projectiles()) CHECK(p.pos.x > tip.x + 60.0f);
    // A bridge of rime lies behind them...
    const Cell& laid = w.at(kStartX + 40, kRow);
    CHECK(laid.mat == mat::Ice);
    CHECK((laid.flags & cellflag::kRime) != 0);
    // ...and the plain ice ahead still stops them, long before their lifetime runs out.
    for (int i = 0; i < 14; ++i) level.tick(PlayerInput{});
    CHECK(level.projectiles().empty());
}

TEST_CASE("a rime trail never freezes into a creature it passes") {
    SanctumRun s(15);
    Level& level = s.level;
    World& w = level.world();
    // An open gallery in the brick under the sanctum floor, far from the player, with a slime in the bolt's path.
    constexpr int kRow = 225;
    for (int y = kRow - 8; y <= kRow + 8; ++y)
        for (int x = 40; x < 300; ++x) w.clear(x, y);
    const Enemy& slime = level.spawnEnemy(EnemyType::Slime, {140.0f, static_cast<float>(kRow) + 0.5f});
    const CastShot shot = castOnce({SpellId::RimeTrail, SpellId::SparkBolt});
    REQUIRE(shot.projectiles.size() == 1);
    Projectile bolt;
    bolt.spell = shot.projectiles.front().spell;
    bolt.def = shot.projectiles.front().def;
    bolt.pos = bolt.prevPos = {60.5f, static_cast<float>(kRow) + 0.5f};
    bolt.vel = {bolt.def.speed, 0.0f};
    bolt.rememberHit(slime.id); // it has struck the slime already and flies on through it, as a piercing bolt does
    for (int i = 0; i < 20 && !bolt.dead; ++i) updateProjectile(level, bolt, kTickDt);
    CHECK(bolt.pos.x > 200.0f);
    // The bridge runs up to the slime and on beyond it...
    CHECK(w.at(100, kRow).mat == mat::Ice);
    CHECK(w.at(180, kRow).mat == mat::Ice);
    // ...but not through it.
    const Rectf b = slime.bounds();
    int frozenInside = 0;
    for (int y = floorToInt(b.y); y <= floorToInt(b.y + b.h - 0.001f); ++y)
        for (int x = floorToInt(b.x); x <= floorToInt(b.x + b.w - 0.001f); ++x)
            frozenInside += w.at(x, y).mat == mat::Ice ? 1 : 0;
    CHECK(frozenInside == 0);
}

TEST_CASE("a sawblade rolls on along a floor too hard to cut") {
    SanctumRun s(15);
    Level& level = s.level;
    World& w = level.world();
    // A long corridor in the brick under the sanctum floor, far from the player, floored with rock.
    constexpr int kRow = 205, kStartX = 40, kEndX = 380;
    for (int x = kStartX; x < kEndX; ++x) {
        for (int y = kRow - 2; y <= kRow + 2; ++y) w.clear(x, y);
        w.place(x, kRow + 3, mat::Rock);
    }
    const CastShot shot = castOnce({SpellId::Sawblade});
    REQUIRE(shot.projectiles.size() == 1);
    REQUIRE(matDef(mat::Rock).hardness > shot.projectiles.front().def.digPower);
    // Launched level, just above the floor: it lands at once and must keep rolling, not stop dead.
    const Vec2 tip{static_cast<float>(kStartX) + 10.5f, static_cast<float>(kRow) + 2.5f};
    level.launch(shot.projectiles.front(), tip, 0.0f, Team::Player, 0, 1.0f);
    for (int i = 0; i < 60; ++i) level.tick(PlayerInput{}); // a second
    REQUIRE(level.projectiles().size() == 1);
    const Projectile& saw = level.projectiles().front();
    CHECK(saw.pos.x > tip.x + 100.0f);
    CHECK(saw.pos.y < static_cast<float>(kRow + 3)); // still on the floor, not in it
}

TEST_CASE("a sawblade climbs a floor that rises a cell at a time") {
    SanctumRun s(15);
    Level& level = s.level;
    World& w = level.world();
    // A corridor in the brick under the sanctum floor, far from the player, its rock floor rising a cell every 8 columns.
    constexpr int kBase = 245, kStartX = 40, kEndX = 340;
    auto floorRow = [&](int x) { return kBase - (x - kStartX) / 8; };
    for (int x = kStartX; x < kEndX; ++x) {
        for (int y = floorRow(x) - 6; y < floorRow(x); ++y) w.clear(x, y);
        w.place(x, floorRow(x), mat::Rock);
    }
    const CastShot shot = castOnce({SpellId::Sawblade});
    REQUIRE(shot.projectiles.size() == 1);
    REQUIRE(matDef(mat::Rock).hardness > shot.projectiles.front().def.digPower); // it cannot cut the rises away
    // Launched level just above the floor, a few columns short of the next rise.
    const Vec2 tip{static_cast<float>(kStartX) + 10.5f, static_cast<float>(floorRow(kStartX + 10)) - 0.5f};
    level.launch(shot.projectiles.front(), tip, 0.0f, Team::Player, 0, 1.0f);
    for (int i = 0; i < 60; ++i) level.tick(PlayerInput{}); // a second
    REQUIRE(level.projectiles().size() == 1);
    const Projectile& saw = level.projectiles().front();
    CHECK(saw.pos.x > tip.x + 100.0f); // up a dozen rises, never turned back at one
    const float under = static_cast<float>(floorRow(floorToInt(saw.pos.x)));
    CHECK(saw.pos.y < under);        // riding the floor...
    CHECK(saw.pos.y > under - 2.0f); // ...not tossed up off it
}

TEST_CASE("a sawblade rides a soft floor rather than burrowing into it") {
    SanctumRun s(15);
    Level& level = s.level;
    World& w = level.world();
    // A long corridor in the brick under the sanctum floor, far from the player, floored three deep with salt rock the
    // saw can cut.
    constexpr int kRow = 205, kStartX = 40, kEndX = 380;
    for (int x = kStartX; x < kEndX; ++x) {
        for (int y = kRow - 2; y <= kRow + 2; ++y) w.clear(x, y);
        for (int y = kRow + 3; y <= kRow + 5; ++y) w.place(x, y, mat::SaltRock);
    }
    const CastShot shot = castOnce({SpellId::Sawblade});
    REQUIRE(shot.projectiles.size() == 1);
    REQUIRE(matDef(mat::SaltRock).hardness <= shot.projectiles.front().def.digPower);
    const Vec2 tip{static_cast<float>(kStartX) + 10.5f, static_cast<float>(kRow) + 2.5f};
    level.launch(shot.projectiles.front(), tip, 0.0f, Team::Player, 0, 1.0f);
    for (int i = 0; i < 60; ++i) level.tick(PlayerInput{}); // a second
    REQUIRE(level.projectiles().size() == 1);
    const Projectile& saw = level.projectiles().front();
    CHECK(saw.pos.x > tip.x + 100.0f);
    CHECK(saw.pos.y < static_cast<float>(kRow + 3));                // on the floor, not sunk into it
    CHECK(w.at(floorToInt(tip.x), kRow + 3).mat == mat::SaltRock); // the floor it landed on is whole
}

TEST_CASE("a bomb rests where it lands until its long fuse runs out") {
    SanctumRun s(15);
    Level& level = s.level;
    World& w = level.world();
    // A corridor in the brick under the sanctum floor, well away from the player, floored with brick.
    constexpr int kRow = 205, kStartX = 200, kEndX = 420;
    for (int x = kStartX; x < kEndX; ++x)
        for (int y = kRow - 2; y <= kRow + 2; ++y) w.clear(x, y);
    const CastShot shot = castOnce({SpellId::Bomb});
    REQUIRE(shot.projectiles.size() == 1);
    REQUIRE((shot.projectiles.front().def.flags & projflag::kFused) != 0);
    // Tossed along the corridor from under its roof: it drops onto the floor, bounces, then rolls to a stop.
    const Vec2 tip{static_cast<float>(kStartX) + 10.5f, static_cast<float>(kRow) - 1.5f};
    level.launch(shot.projectiles.front(), tip, 0.0f, Team::Player, 0, 1.0f);
    for (int i = 0; i < 60; ++i) level.tick(PlayerInput{}); // a second
    REQUIRE(level.projectiles().size() == 1);
    const Projectile& bomb = level.projectiles().front();
    CHECK(bomb.pos.y > static_cast<float>(kRow + 2)); // lying on the floor...
    CHECK(bomb.pos.y < static_cast<float>(kRow + 3));
    CHECK(std::fabs(bomb.vel.x) < 1.0f);              // ...at rest
    const Vec2 rest = bomb.pos;
    for (int i = 0; i < 30; ++i) level.tick(PlayerInput{});
    REQUIRE(level.projectiles().size() == 1);
    CHECK((level.projectiles().front().pos - rest).length() < 0.5f); // still, not jittering or creeping
    // It bursts once the fuse (2.6 s) runs out, within three seconds of the throw.
    int ticks = 90;
    while (!level.projectiles().empty() && ticks < 180) {
        level.tick(PlayerInput{});
        ++ticks;
    }
    CHECK(level.projectiles().empty());
    CHECK(ticks > 150);
    CHECK(!level.shockwaves().empty());
}

TEST_CASE("a seeking shot through one foe turns for the next, never back for the one it struck") {
    SanctumRun s(15);
    Level& level = s.level;
    World& w = level.world();
    // An open gallery in the brick under the sanctum floor, far from the player: a slime in the shot's path and a second
    // well beyond it, off to one side.
    constexpr int kRow = 225;
    for (int y = kRow - 24; y <= kRow + 8; ++y)
        for (int x = 40; x < 300; ++x) w.clear(x, y);
    const Enemy& first = level.spawnEnemy(EnemyType::Slime, {140.0f, static_cast<float>(kRow) + 0.5f});
    const Enemy& second = level.spawnEnemy(EnemyType::Slime, {190.0f, static_cast<float>(kRow) - 15.5f});
    const CastShot shot = castOnce({SpellId::Homing, SpellId::EnergyOrb});
    REQUIRE(shot.projectiles.size() == 1);
    Projectile orb;
    orb.spell = shot.projectiles.front().spell;
    orb.def = shot.projectiles.front().def;
    REQUIRE((orb.def.flags & projflag::kHoming) != 0 && (orb.def.flags & projflag::kPiercing) != 0);
    orb.pos = orb.prevPos = {60.5f, static_cast<float>(kRow) + 0.5f};
    orb.vel = {orb.def.speed, 0.0f};
    for (int i = 0; i < 180 && !orb.dead; ++i) updateProjectile(level, orb, kTickDt);
    CHECK(first.hp < first.maxHp);
    CHECK(second.hp < second.maxHp); // it turned for the second slime instead of circling back to the first
}

TEST_CASE("an arc spark leaps on to a foe it has not struck, never back to one it has") {
    SanctumRun s(15);
    Level& level = s.level;
    World& w = level.world();
    // A corridor in the brick under the sanctum floor, far from the player, with three sentinels (which never stir from
    // the spot) on its floor: the second 8 cells past the first, the third 20.
    constexpr int kFloor = 230;
    for (int y = kFloor - 16; y < kFloor; ++y)
        for (int x = 40; x < 300; ++x) w.clear(x, y);
    const float standY = static_cast<float>(kFloor) - enemyDef(EnemyType::Turret).halfH;
    const Enemy& first = level.spawnEnemy(EnemyType::Turret, {150.0f, standY});
    const Enemy& second = level.spawnEnemy(EnemyType::Turret, {158.0f, standY});
    const Enemy& third = level.spawnEnemy(EnemyType::Turret, {170.0f, standY});
    const CastShot shot = castOnce({SpellId::ArcBolt});
    REQUIRE(shot.projectiles.size() == 1);
    REQUIRE(shot.projectiles.front().def.chains >= 2);
    level.launch(shot.projectiles.front(), {110.5f, standY}, 0.0f, Team::Player, 0, 1.0f);
    for (int i = 0; i < 20; ++i) level.tick(PlayerInput{});
    CHECK(first.hp < first.maxHp);
    CHECK(second.hp < second.maxHp);
    CHECK(third.hp < third.maxHp); // from the second it leapt on to the third, not back to the first
}

TEST_CASE("holding jump climbs on levitation once the jump's rise is spent") {
    SanctumRun s(16);
    Level& level = s.level;
    World& w = level.world();
    Player& pl = level.player();
    for (int i = 0; i < 30; ++i) level.tick(PlayerInput{}); // settle on the floor
    REQUIRE(pl.body.onGround);
    // Open air above, clear of the sanctum's ceiling.
    const int px = floorToInt(pl.body.pos.x), top = floorToInt(pl.body.pos.y) - 8;
    for (int y = std::max(1, top - 100); y <= top; ++y)
        for (int x = px - 6; x <= px + 6; ++x) w.clear(x, y);
    const float startY = pl.body.pos.y;
    PlayerInput hold;
    hold.jumpPressed = hold.jumpHeld = true;
    level.tick(hold);
    hold.jumpPressed = false;
    for (int i = 0; i < 59; ++i) level.tick(hold);
    // The jump alone rises about 23 cells; held for a second, levitation climbs well past that.
    CHECK(startY - pl.body.pos.y > 45.0f);
}

TEST_CASE("sanctum lanterns stay hung") {
    SanctumRun s(11, 1);
    Level& level = s.level;
    const size_t lanterns = level.lanterns().size();
    CHECK(lanterns > 0);
    for (int i = 0; i < 60; ++i) level.tick(PlayerInput{});
    CHECK(level.lanterns().size() == lanterns);
}

TEST_CASE("a spell through a lantern knocks it down to shatter") {
    SanctumRun s(11, 1);
    Level& level = s.level;
    const size_t lanterns = level.lanterns().size();
    CHECK(lanterns > 0);
    const Vec2 at = level.lanterns().front().pos;
    // A shot well below the lantern leaves it hanging.
    level.knockLanterns(at + Vec2{-20.0f, 20.0f}, at + Vec2{20.0f, 20.0f}, 2.5f);
    CHECK(level.lanterns().front().fallSpeed <= 0.0f);
    // One straight through its body drops it; it shatters on the floor below.
    level.knockLanterns(at + Vec2{-20.0f, 2.0f}, at + Vec2{20.0f, 2.0f}, 2.5f);
    CHECK(level.lanterns().front().fallSpeed > 0.0f);
    for (int i = 0; i < 180; ++i) level.tick(PlayerInput{});
    CHECK(level.lanterns().size() < lanterns);
}

TEST_CASE("elites carry an affix, rolled from the seed") {
    initMaterials();
    ThreadPool pool(4);
    Audio audio;
    auto elites = [&](uint32_t seed) {
        RunState run = newRun(seed, Difficulty::Archmage, false);
        run.levelIndex = 3;
        Level level(generateLevel(biomeForDepth(3), 3, seed, pool), run, audio, pool);
        std::vector<std::pair<int, int>> out;
        for (const auto& e : level.enemies())
            if (e->elite()) out.emplace_back(static_cast<int>(e->type()), static_cast<int>(e->affix()));
        return out;
    };
    const auto a = elites(21u), b = elites(21u);
    CHECK(!a.empty());
    CHECK(a == b);
    for (const auto& [type, affix] : a) CHECK(affix > static_cast<int>(Affix::None) && affix < static_cast<int>(Affix::Count));

    // Swift elites move and strike faster; Burning ones shrug off fire.
    Enemy swift(EnemyType::Ghoul, {0.0f, 0.0f}, 1, 1.0f), burning(EnemyType::Ghoul, {0.0f, 0.0f}, 2, 1.0f);
    const float baseSpeed = swift.moveSpeed(), baseCooldown = swift.attackCooldown();
    swift.makeElite(Affix::Swift);
    burning.makeElite(Affix::Burning);
    CHECK(swift.moveSpeed() > baseSpeed && swift.attackCooldown() < baseCooldown);
    CHECK(burning.resist[static_cast<size_t>(DamageType::Fire)] == 0.0f);
}

TEST_CASE("the final boss honours the difficulty, unlike the depth") {
    const float base = enemyDef(EnemyType::Boss).hp;
    const std::pair<Difficulty, float> tiers[] = {{Difficulty::Apprentice, 0.8f}, {Difficulty::Adept, 1.0f}, {Difficulty::Archmage, 1.3f}};
    for (const auto& [difficulty, k] : tiers) {
        SanctumRun s(11, 0, difficulty);
        Level& level = s.level;
        const Enemy& boss = level.spawnEnemy(EnemyType::Boss, level.player().body.pos + Vec2{60.0f, -10.0f});
        CHECK(std::fabs(boss.maxHp - base * k) < 0.5f);
    }
}

TEST_CASE("the Rebis' fall ends its brood and its spells, and nothing can undo the alchemist after it") {
    SanctumRun s(11);
    RunState& run = s.run;
    Level& level = s.level;
    Player& pl = level.player();
    Enemy& boss = level.spawnEnemy(EnemyType::Boss, pl.body.pos + Vec2{60.0f, -40.0f});
    Enemy& imp = level.spawnEnemy(EnemyType::Imp, pl.body.pos + Vec2{30.0f, -30.0f});
    const CastShot bolt = castOnce({SpellId::FireBolt});
    level.launch(bolt.projectiles.front(), boss.body.pos, 0.0f, Team::Monster, boss.id, 1.0f);
    level.tick(PlayerInput{}); // the bolt takes flight
    auto hostileShots = [&] {
        return std::count_if(level.projectiles().begin(), level.projectiles().end(),
                             [](const Projectile& p) { return p.team == Team::Monster && !p.dead; });
    };
    REQUIRE(hostileShots() > 0);
    const int kills = run.stats.kills;
    boss.applyDamage(level, {1.0e7f, DamageType::Physical, {}, Team::Player, false, false, "test"});
    CHECK(!boss.alive());
    CHECK(!imp.alive());                // the brood dissolves...
    CHECK(run.stats.kills == kills + 1); // ...for no credit
    CHECK(run.killsByType[static_cast<size_t>(EnemyType::Imp)] == 0);
    CHECK(hostileShots() == 0);
    // Even the damage over time that slips past a hit's grace no longer lands.
    CHECK(pl.applyDamage(level, {1.0e6f, DamageType::Fire, {}, Team::Monster, false, true, "flames"}) == 0.0f);
    CHECK(pl.alive());
    level.tick(PlayerInput{});
    CHECK(std::none_of(level.enemies().begin(), level.enemies().end(),
                       [](const auto& e) { return e->type() == EnemyType::Imp; })); // swept away with the dead
    CHECK(pl.untouchable > 60.0f);
}

TEST_CASE("guardians rage, the toad sheds its brood and the slayer earns a perk") {
    SanctumRun s(11);
    RunState& run = s.run;
    Level& level = s.level;
    for (int t = 0; t < kGuardianCount; ++t) CHECK(enemyDef(guardianFor(t)).guardian);
    Enemy& toad = level.spawnEnemy(EnemyType::BlackToad, level.player().body.pos + Vec2{40.0f, -10.0f});
    const uint32_t toadId = toad.id;
    auto find = [&]() -> Enemy* {
        for (auto& e : level.enemies())
            if (e->id == toadId) return e.get();
        return nullptr;
    };
    auto hurtTo = [&](float fraction) {
        Enemy* t = find();
        t->applyDamage(level, {t->hp - t->maxHp * fraction, DamageType::Physical, {}, Team::Player, false, true, "test"});
        level.tick(PlayerInput{});
    };
    const size_t before = level.enemies().size();
    hurtTo(0.6f);
    CHECK(level.enemies().size() == before + 2); // the first brood
    REQUIRE(find() != nullptr);
    CHECK(!find()->enraged());
    hurtTo(0.45f);
    REQUIRE(find() != nullptr);
    CHECK(find()->enraged());
    hurtTo(0.3f);
    CHECK(level.enemies().size() == before + 4); // and the second
    int perks = 0;
    for (uint8_t n : run.perks) perks += n;
    Enemy* t = find();
    REQUIRE(t != nullptr);
    t->applyDamage(level, {1.0e6f, DamageType::Physical, {}, Team::Player, false, false, "test"});
    // The essence is a choice: two altars, the toad's own gift among them, and nothing yet granted.
    std::vector<const Pickup*> altars;
    for (const Pickup& p : level.pickups())
        if (p.type == PickupType::PerkAltar && p.altarGroup == 1 && !p.dead) altars.push_back(&p); // the sanctum's are group 0
    REQUIRE(altars.size() == 2);
    CHECK((altars[0]->perk == PerkId::ToxicBlood || altars[1]->perk == PerkId::ToxicBlood));
    for (const Pickup* a : altars) CHECK(a->perk != PerkId::GlassCannon && a->perk != PerkId::PhoenixStep);
    int after = 0;
    for (uint8_t n : run.perks) after += n;
    CHECK(after == perks);
    // Claiming one grants it and clears the other.
    level.debugTeleport(altars[0]->body.pos);
    PlayerInput claim;
    claim.interactPressed = true;
    level.tick(claim);
    after = 0;
    for (uint8_t n : run.perks) after += n;
    CHECK(after == perks + 1);
    int standing = 0;
    for (const Pickup& p : level.pickups()) standing += p.type == PickupType::PerkAltar && p.altarGroup == 1 && !p.dead ? 1 : 0;
    CHECK(standing == 0);
}

TEST_CASE("feats are described and kills earn them") {
    for (int i = 0; i < kFeatCount; ++i) {
        const FeatDef& d = featDef(static_cast<FeatId>(i));
        CHECK(d.name && d.name[0] && d.description && d.description[0]);
        for (int j = 0; j < i; ++j) CHECK(std::strcmp(d.name, featDef(static_cast<FeatId>(j)).name) != 0);
    }
    SanctumRun s(11);
    Level& level = s.level;
    const Vec2 near = level.player().body.pos + Vec2{30.0f, -10.0f};
    auto kill = [&](EnemyType t) {
        Enemy& e = level.spawnEnemy(t, near);
        e.applyDamage(level, {1.0e6f, DamageType::Physical, {}, Team::Player, false, false, "test"});
    };
    kill(EnemyType::Slime);
    std::vector<FeatId> earned = level.takeFeats();
    CHECK(std::find(earned.begin(), earned.end(), FeatId::FirstBlood) != earned.end());
    CHECK(std::find(earned.begin(), earned.end(), FeatId::Carnage) == earned.end());
    kill(EnemyType::Slime);
    kill(EnemyType::BlackToad); // three kills in the same instant, the last a guardian
    earned = level.takeFeats();
    CHECK(std::find(earned.begin(), earned.end(), FeatId::Carnage) != earned.end());
    CHECK(std::find(earned.begin(), earned.end(), FeatId::BlackToad) != earned.end());
    CHECK(level.takeFeats().empty());
}

TEST_CASE("every codex entry is described, and names what its rule really does") {
    initMaterials();
    for (int i = 0; i < kCodexCount; ++i) {
        const auto e = static_cast<CodexEntry>(i);
        const CodexDef& d = codexDef(e);
        REQUIRE(d.name && d.name[0] && d.riddle && d.riddle[0] && d.flavour && d.flavour[0]);
        for (int j = 0; j < i; ++j) CHECK(std::strcmp(d.name, codexDef(static_cast<CodexEntry>(j)).name) != 0);
        CHECK(d.reagents[0].present() && d.products[0].present());
        for (const CodexTerm& t : {d.reagents[0], d.reagents[1], d.products[0], d.products[1]}) {
            CHECK(t.mat < kMaterialCount);
            if (t.present()) CHECK(t.name()[0] != '\0');
        }
        // The game's pixel font has no arrows or accents: plain ASCII only.
        const std::string formula = codexFormula(e);
        CHECK(formula.find(" -> ") != std::string::npos);
        for (const std::string& text : {std::string(d.name), std::string(d.riddle), std::string(d.flavour), formula})
            for (const char c : text) CHECK(c >= ' ' && c <= '~');
    }
    CHECK(codexFormula(CodexEntry::WaterLava) == "Water + Lava -> Steam & Obsidian");
    CHECK(codexFormula(CodexEntry::Condensation) == "Steam -> Water");

    // Every pairwise reaction is filed under an entry whose formula names both its reagents (brine quenching
    // lava as water does) and everything it makes.
    auto names = [](const CodexTerm (&terms)[2], MatId m) { return terms[0].mat == m || terms[1].mat == m; };
    auto asWater = [](MatId m) { return m == mat::Brine ? static_cast<MatId>(mat::Water) : m; };
    const MaterialTables& mt = materials();
    for (int self = 0; self < kMaterialCount; ++self)
        for (int r = 0; r < mt.reactionCount[static_cast<size_t>(self)]; ++r) {
            const Reaction& re = mt.reactions[static_cast<size_t>(self)][static_cast<size_t>(r)];
            const CodexDef& d = codexDef(re.codex);
            CHECK(names(d.reagents, asWater(static_cast<MatId>(self))) && names(d.reagents, asWater(re.other)));
            if (re.selfInto != kKeep) CHECK(names(d.products, re.selfInto));
            if (re.otherInto != kKeep) CHECK(names(d.products, re.otherInto));
        }
}

TEST_CASE("a transmutation is discovered once, where the view can see it, and previews keep the profile") {
    initMaterials();
    // Water quenching lava at the foot of a shaft at x 40, in the first column of chunks.
    ThreadPool pool(2);
    World w(512, 128, 5);
    for (int y = 70; y <= 91; ++y) {
        w.place(39, y, mat::Rock);
        w.place(41, y, mat::Rock);
    }
    w.place(40, 91, mat::Rock);
    w.place(40, 90, mat::Lava);
    w.place(40, 89, mat::Water);
    for (int i = 0; i < 200 && w.at(40, 90).mat != mat::Obsidian; ++i) w.step(pool);
    REQUIRE(w.at(40, 90).mat == mat::Obsidian);
    const uint32_t quench = codexBit(CodexEntry::WaterLava);
    CHECK(codexWitnessed(w, {0.0f, 0.0f, 480.0f, 128.0f}) == quench);
    CHECK(codexWitnessed(w, {200.0f, 0.0f, 270.0f, 128.0f}) == 0);
    // A view whose margin reaches the reaction's chunk counts it; one falling short does not.
    CHECK(codexWitnessed(w, {71.0f, 0.0f, 200.0f, 128.0f}) == quench);
    CHECK(codexWitnessed(w, {72.0f, 0.0f, 200.0f, 128.0f}) == 0);
    // A view hanging past the world's edges reads the chunks it still touches; one wholly outside reads nothing.
    CHECK(codexWitnessed(w, {-300.0f, -40.0f, 330.0f, 200.0f}) == quench);
    CHECK(codexWitnessed(w, {0.0f, 120.0f, 480.0f, 270.0f}) == quench);
    CHECK(codexWitnessed(w, {-300.0f, 0.0f, 250.0f, 128.0f}) == 0);
    CHECK(codexWitnessed(w, {0.0f, -400.0f, 480.0f, 270.0f}) == 0);
    CHECK(codexWitnessed(w, {600.0f, 0.0f, 480.0f, 270.0f}) == 0);

    // In play, what is new enters the profile's record and is announced, once.
    const uint32_t seen = quench | codexBit(CodexEntry::Kindling);
    uint32_t known = codexBit(CodexEntry::Kindling), preview = 0;
    CHECK(discoverCodex(seen, known, preview, false) == quench);
    CHECK(known == seen && preview == 0);
    CHECK(discoverCodex(seen, known, preview, false) == 0);
    // A bot or a capture announces its finds once each, and leaves the profile's record as it was loaded.
    uint32_t profile = codexBit(CodexEntry::Kindling), session = 0;
    CHECK(discoverCodex(seen, profile, session, true) == quench);
    CHECK(profile == codexBit(CodexEntry::Kindling) && session == quench);
    CHECK(discoverCodex(seen, profile, session, true) == 0);
    // Bits past the last entry (a mask from a newer build, say) are never announced.
    CHECK(discoverCodex(~kCodexAll, known, preview, false) == 0);
}

TEST_CASE("elixirs lend haste and fury for a while") {
    SanctumRun s(12);
    RunState& run = s.run;
    Level& level = s.level;
    Player& pl = level.player();
    const float baseDamage = pl.damageMul();
    PlayerInput drink;
    drink.drinkPressed = true;

    run.flask = {mat::Ichor, 100};
    level.tick(drink);
    CHECK(run.flask.empty());
    CHECK(pl.status.fury > 11.0f);
    CHECK(std::fabs(pl.damageMul() - baseDamage * 1.5f) < 1e-4f);

    run.flask = {mat::Quicksilver, 50}; // half a draught: half as long
    pl.status.chilled = 2.0f;
    level.tick(drink);
    CHECK(pl.status.chilled == 0.0f);
    CHECK(pl.status.hasted > 7.0f && pl.status.hasted < 7.6f);

    for (int i = 0; i < 60 * 13; ++i) level.tick(PlayerInput{});
    CHECK(pl.status.fury == 0.0f && pl.status.hasted == 0.0f);
    CHECK(std::fabs(pl.damageMul() - baseDamage) < 1e-4f);
}

TEST_CASE("a mimic poses as a chest until approached, and pays out like one") {
    SanctumRun s(13);
    Level& level = s.level;
    const Vec2 home = level.player().body.pos;
    Enemy& mimic = level.spawnEnemy(EnemyType::Mimic, home + Vec2{60.0f, -2.0f});
    for (int i = 0; i < 30; ++i) level.tick(PlayerInput{});
    CHECK(mimic.disguised());
    CHECK(!mimic.aggro());

    level.debugTeleport(mimic.body.pos - Vec2{20.0f, 0.0f}); // inside its reach, short of "open"
    level.tick(PlayerInput{});
    CHECK(!mimic.disguised());
    CHECK(mimic.aggro());

    const size_t before = level.pickups().size();
    mimic.applyDamage(level, {1.0e6f, DamageType::Physical, {}, Team::Player, false, false, "test"});
    CHECK(level.pickups().size() >= before + 4); // the chest's coins and treasure, plus its own gold
    const std::vector<FeatId> earned = level.takeFeats();
    CHECK(std::find(earned.begin(), earned.end(), FeatId::Mimic) != earned.end());

    // A shot wakes a sleeping one from afar.
    Enemy& other = level.spawnEnemy(EnemyType::Mimic, home + Vec2{-70.0f, -2.0f});
    other.applyDamage(level, {1.0f, DamageType::Physical, {}, Team::Player, false, false, "test"});
    CHECK(!other.disguised());
}

TEST_CASE("a chest holds the same treasure however the stage went before it was opened") {
    SanctumRun s(12);
    Level& level = s.level;
    const Vec2 at = level.player().body.pos + Vec2{40.0f, -10.0f};
    auto open = [&](uint32_t lootSeed) {
        const size_t before = level.pickups().size();
        level.dropLoot(at, 1, lootSeed);
        std::vector<std::string> loot;
        for (size_t i = before; i < level.pickups().size(); ++i) {
            const Pickup& p = level.pickups()[i];
            loot.push_back(std::to_string(static_cast<int>(p.type)) + " " + p.label() + " " + std::to_string(p.amount));
        }
        return loot;
    };
    const std::vector<std::string> first = open(77u);
    CHECK(first.size() >= 4); // three to six coins and a find
    for (int i = 0; i < 50; ++i) level.rng().next(); // the stage's stream has moved on
    CHECK(open(77u) == first);

    // Each of a stage's chests (and mimics) has a seed of its own, the same each time the stage is built.
    auto lootSeeds = [&](uint32_t seed) {
        RunState run = newRun(seed, Difficulty::Adept, false);
        run.levelIndex = 1;
        Level stage(generateLevel(biomeForDepth(1), 1, seed, s.pool), run, s.audio, s.pool);
        std::vector<uint32_t> seeds;
        for (const Pickup& p : stage.pickups())
            if (p.type == PickupType::Chest) seeds.push_back(p.lootSeed);
        for (const auto& e : stage.enemies())
            if (e->type() == EnemyType::Mimic) seeds.push_back(e->lootSeed);
        return seeds;
    };
    std::vector<uint32_t> seeds = lootSeeds(21u);
    CHECK(!seeds.empty());
    CHECK(lootSeeds(21u) == seeds);
    std::sort(seeds.begin(), seeds.end());
    CHECK(std::adjacent_find(seeds.begin(), seeds.end()) == seeds.end());
}

TEST_CASE("a transmutation circle dissolves stray matter but keeps the alchemist's offering") {
    initMaterials();
    ThreadPool pool(2);
    Audio audio;
    RunState run = newRun(21, Difficulty::Adept, false);
    run.levelIndex = 1;
    Level level(generateLevel(biomeForDepth(1), 1, 21u, pool), run, audio, pool);
    level.player().invulnerable = 1.0e6f;
    REQUIRE(!level.transmuters().empty());
    const Transmuter& t = level.transmuters().front();
    const int cx = floorToInt(t.pos.x), floorY = floorToInt(t.pos.y);
    World& w = level.world();
    // Oil (or water) that ran in from a far pool, and ten cells of the wanted matter poured by the alchemist.
    const MatId stray = t.wants == mat::Oil ? mat::Water : mat::Oil;
    for (int x = cx - 9; x <= cx + 9; ++x) w.place(x, floorY - 2, stray);
    for (int x = cx - 4; x <= cx + 5; ++x) {
        Cell c = makeCell(t.wants, static_cast<uint32_t>(x));
        c.flags |= cellflag::kPlayerMade;
        w.set(x, floorY, c);
    }
    for (int i = 0; i < 12; ++i) level.tick(PlayerInput{});
    int strays = 0, offered = 0;
    for (int y = floorY - 6; y <= floorY; ++y)
        for (int x = cx - 9; x <= cx + 9; ++x) {
            strays += w.at(x, y).mat == stray ? 1 : 0;
            offered += w.at(x, y).mat == t.wants && (w.at(x, y).flags & cellflag::kPlayerMade) ? 1 : 0;
        }
    CHECK(strays == 0);
    CHECK(offered >= 8); // a liquid offering may spread, but stays in the basin
    CHECK(level.transmuters().front().progress == offered);
}

TEST_CASE("stage events are announced, telegraphed, then land") {
    initMaterials();
    ThreadPool pool(4);
    Audio audio;
    auto count = [](const World& w, Vec2 at, int r, MatId m) {
        int n = 0;
        for (int y = floorToInt(at.y) - r; y <= floorToInt(at.y) + r; ++y)
            for (int x = floorToInt(at.x) - r; x <= floorToInt(at.x) + r; ++x) n += w.matAt(x, y) == m;
        return n;
    };
    for (int k = 0; k < static_cast<int>(StageEventKind::Count); ++k) {
        const auto kind = static_cast<StageEventKind>(k);
        RunState run = newRun(21, Difficulty::Adept, false);
        run.levelIndex = 1;
        Level level(generateLevel(biomeForDepth(1), 1, 21u, pool), run, audio, pool);
        level.player().invulnerable = 1.0e6f;
        bool started = false;
        for (int hop = 0; hop < 30 && !started; ++hop) {
            level.debugWarpAhead();
            for (int i = 0; i < 5; ++i) level.tick(PlayerInput{});
            started = level.events().trigger(level, kind);
        }
        CHECK(started);
        if (!started) continue;
        CHECK(!level.events().takeNotice().text.empty());
        const Vec2 site = level.events().site();
        const size_t enemies = level.enemies().size();
        // Debris falls to the floor below the ceiling site, up to 80 cells down.
        const int gravel = count(level.world(), site, 90, mat::Gravel) + count(level.world(), site, 90, mat::Snow);
        const int gold = count(level.world(), site, 90, mat::GoldDust);
        for (int i = 0; i < 60 * 8 && level.events().active(); ++i) level.tick(PlayerInput{});
        for (int i = 0; i < 30; ++i) level.tick(PlayerInput{});
        CHECK(!level.events().active());
        const World& w = level.world();
        switch (kind) {
        case StageEventKind::CaveIn: CHECK(count(w, site, 90, mat::Gravel) + count(w, site, 90, mat::Snow) > gravel + 20); break;
        case StageEventKind::GoldFall: CHECK(count(w, site, 90, mat::GoldDust) > gold + 20); break;
        case StageEventKind::Breach: CHECK(count(w, site, 50, mat::Brine) > 100); break;
        case StageEventKind::GasSeep: CHECK(count(w, site, 60, mat::Methane) > 20); break;
        case StageEventKind::Swarm: CHECK(level.enemies().size() >= enemies + 3); break;
        case StageEventKind::Count: break;
        }
    }
}

TEST_CASE("ordeals stack their rules and open one at a time") {
    for (int i = 1; i <= kMaxOrdeal; ++i) CHECK(ordealDef(i).name[0] && ordealDef(i).rule[0]);
    CHECK(ordealLabel(0).empty() && ordealLabel(3) == "Ordeal III" && ordealLabel(kMaxOrdeal) == "Ordeal VIII");
    CHECK(ordealRules(2) == std::string(ordealDef(1).rule) + " " + ordealDef(2).rule);

    const RunState plain = newRun(5, Difficulty::Adept, false);
    const RunState frail = newRun(5, Difficulty::Adept, false, LoadoutId::Apprentice, static_cast<int>(Ordeal::FrailFlesh));
    CHECK(frail.maxHp < plain.maxHp && frail.hp == frail.maxHp);
    CHECK(newRun(5, Difficulty::Adept, false, LoadoutId::Apprentice, 99).ordeal == kMaxOrdeal);

    // Foes are hardened and strike harder; the keepers are tougher still.
    initMaterials();
    ThreadPool pool(2);
    Audio audio;
    RunState run = newRun(9, Difficulty::Adept, false, LoadoutId::Apprentice, kMaxOrdeal);
    Level level(generateSanctum(0, 9u, pool), run, audio, pool);
    const Vec2 at = level.player().body.pos + Vec2{40.0f, -6.0f};
    const Enemy& slime = level.spawnEnemy(EnemyType::Slime, at);
    CHECK(std::fabs(slime.maxHp - enemyDef(EnemyType::Slime).hp * 1.25f) < 0.01f);
    CHECK(std::fabs(slime.damageScale() - 1.25f) < 1e-4f);
    const Enemy& toad = level.spawnEnemy(EnemyType::BlackToad, at);
    CHECK(std::fabs(toad.maxHp - enemyDef(EnemyType::BlackToad).hp * 1.25f * 1.4f) < 0.1f);

    // Completing the Work opens the next ordeal, but only from the highest one open.
    Profile p;
    RunState won = newRun(1, Difficulty::Adept, false);
    won.levelIndex = kFinalStage;
    CHECK(foldRun(p, won, true, "Completed the Great Work").unlockedOrdeal == 1);
    CHECK(foldRun(p, won, true, "Completed the Great Work").unlockedOrdeal == 0 && p.ordealUnlocked == 1);
    won.ordeal = 1;
    CHECK(foldRun(p, won, true, "Completed the Great Work").unlockedOrdeal == 2 && p.ordealUnlocked == 2);
    CHECK(p.history.front().ordeal == 1);

    // The chronicle keeps the ordeal, and reads entries from before ordeals.
    RunHistoryEntry e;
    e.ordeal = 3;
    e.fate = "Slain by a Mimic";
    RunHistoryEntry back;
    CHECK(decodeHistoryEntry(encodeHistoryEntry(e), back) && back.ordeal == 3 && back.fate == e.fate);
    CHECK(decodeHistoryEntry("7|0|1|2|0|0|0|100|5|orbited by bats", back) && back.ordeal == 0 && back.fate == "orbited by bats");
}

TEST_CASE("slaying all four guardians gathers the Quintessence") {
    SanctumRun s(14);
    RunState& run = s.run;
    Level& level = s.level;
    const Vec2 near = level.player().body.pos + Vec2{40.0f, -10.0f};
    for (int stage = 0; stage < kGuardianCount; ++stage) {
        CHECK(!run.quintessence());
        CHECK(guardianStage(guardianFor(stage)) == stage);
        Enemy& g = level.spawnEnemy(guardianFor(stage), near);
        g.applyDamage(level, {1.0e7f, DamageType::Physical, {}, Team::Player, false, false, "test"});
        CHECK(run.essences == (1u << (stage + 1)) - 1u);
    }
    CHECK(run.quintessence());
    CHECK(guardianStage(EnemyType::Slime) == -1);
    const std::vector<FeatId> feats = level.takeFeats();
    CHECK(std::find(feats.begin(), feats.end(), FeatId::RedColossus) != feats.end());
}

TEST_CASE("stage lanterns hang from solid ceilings, not pooled steam") {
    initMaterials();
    ThreadPool pool(4);
    Audio audio;
    // Seed 5's Athanor once hung lanterns from steam; they fell and burned as the player arrived.
    for (int index = 0; index < kStageCount; ++index) {
        RunState run = newRun(5, Difficulty::Adept, false);
        run.levelIndex = index;
        Level level(generateLevel(biomeForDepth(index), index, 5u, pool), run, audio, pool);
        const size_t lanterns = level.lanterns().size();
        CHECK(lanterns > 0);
        for (int i = 0; i < 60; ++i) level.tick(PlayerInput{});
        CHECK(level.lanterns().size() == lanterns);
    }
}

TEST_CASE("a body frozen into solid ice is freed") {
    initMaterials();
    World w(128, 128, 3);
    for (int y = 50; y < 72; ++y)
        for (int x = 50; x < 72; ++x) w.place(x, y, mat::Ice);
    Body b;
    b.halfW = 3.0f;
    b.halfH = 6.0f;
    b.pos = b.prevPos = {61.0f, 61.0f};
    CHECK(boxBlocked(w, b.pos, b.halfW, b.halfH));
    CHECK(unbury(b, w));
    CHECK(!boxBlocked(w, b.pos, b.halfW, b.halfH));
}

namespace {

/// Casts until the wand is ready again (cooldowns elapse instantly in tests).
CastOutcome castNow(Wand& w, Rng& rng) {
    w.cooldown = 0.0f;
    w.mana = w.manaMax;
    return w.tryCast(rng);
}

} // namespace

TEST_CASE("wands recharge past trailing modifiers and unaffordable spells") {
    Rng rng(4);
    // A modifier at the end of the deck casts nothing: the wand recharges instead.
    Wand w = makeStarterWand(0);
    w.slots = {SpellId::SparkBolt, SpellId::SparkBolt, SpellId::DamageUp, SpellId::None};
    w.resetDeck();
    CHECK(castNow(w, rng).status == CastStatus::Cast);
    CHECK(castNow(w, rng).status == CastStatus::Cast);
    CHECK(castNow(w, rng).status == CastStatus::Empty); // only Damage Up left: no phantom cast
    CHECK(castNow(w, rng).status == CastStatus::Cast);  // recharged, back at the top

    // A spell dearer than the whole mana pool is skipped instead of locking the deck.
    Wand poor = makeStarterWand(0);
    poor.manaMax = 100.0f;
    poor.slots = {SpellId::SparkBolt, SpellId::HolyBomb, SpellId::None, SpellId::None};
    poor.resetDeck();
    CHECK(castNow(poor, rng).status == CastStatus::Cast);
    CHECK(castNow(poor, rng).status == CastStatus::NoMana);
    CHECK(castNow(poor, rng).status == CastStatus::Cast); // back to the Spark Bolt
}

TEST_CASE("rare finds are attack spells from deeper down") {
    Rng rng(5);
    for (int tier = 0; tier <= 4; ++tier) {
        bool showpiece = false;
        for (int i = 0; i < 300; ++i) {
            const SpellId id = randomRareSpell(tier, rng);
            const SpellDef& d = spellDef(id);
            CHECK(d.type == SpellType::Projectile && !d.trigger);
            CHECK(d.tier >= std::clamp(tier + 1, 2, 4));
            CHECK(id != SpellId::SporeBomb);
            showpiece = showpiece || d.tier >= 3;
        }
        CHECK(showpiece); // even the first stage can turn up Lightning or a Black Hole
    }
}

TEST_CASE("a hoard wand always carries a rare attack spell") {
    for (uint32_t seed = 1; seed <= 50; ++seed) {
        for (int tier = 0; tier <= 3; ++tier) {
            Rng rng(seed);
            const Wand w = makeHoardWand(tier, rng);
            CHECK(std::any_of(w.slots.begin(), w.slots.end(), [&](SpellId s) {
                const SpellDef& d = spellDef(s);
                return d.type == SpellType::Projectile && !d.trigger && d.tier >= std::clamp(tier + 1, 2, 4);
            }));
        }
    }
}

TEST_CASE("floor and chest finds reach one tier deeper, and spare the kit's staples") {
    Rng rng(9);
    int deeper = 0, spark = 0;
    constexpr int kRolls = 2000;
    for (int i = 0; i < kRolls; ++i) {
        const SpellId s = randomFind(0, rng);
        CHECK(spellDef(s).tier <= 1);
        deeper += spellDef(s).tier == 1 ? 1 : 0;
        spark += s == SpellId::SparkBolt ? 1 : 0;
    }
    CHECK(deeper > kRolls / 3);   // about half the first stage's finds come from the second
    CHECK(spark < kRolls / 25);  // the rod already holds three
}

TEST_CASE("the first sanctum's shop stocks the coming depth") {
    bool deeper = false;
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        SanctumRun s(seed);
        Level& level = s.level;
        int wares = 0;
        for (const Pickup& p : level.pickups()) {
            if (p.price <= 0) continue;
            ++wares;
            CHECK(!p.rare);
            if (p.type == PickupType::Spell) {
                CHECK(spellDef(p.spell).tier <= 1);
                deeper = deeper || spellDef(p.spell).tier == 1;
            } else if (p.type == PickupType::Wand) {
                CHECK(p.price == 250);
            }
        }
        CHECK(wares == 4);
    }
    CHECK(deeper);
}

TEST_CASE("a spell set down to make room leaves E to the find beside it") {
    SanctumRun s(21);
    Level& level = s.level;
    const Vec2 at = level.player().body.pos;
    auto place = [&](float dx, SpellId spell, bool setDown, int price) {
        Pickup p = makeSpellPickup(at + Vec2{dx, 0.0f}, spell);
        p.setDown = setDown;
        p.price = price;
        level.spawnPickup(std::move(p));
    };
    const auto focusedSpell = [&] {
        const Pickup* p = level.focusedPickup();
        return p ? p->spell : SpellId::None;
    };

    // Nearer than the find, but a step aside: E goes to the find.
    level.pickups().clear();
    place(4.0f, SpellId::SparkBolt, true, 0);
    place(12.0f, SpellId::Bomb, false, 0);
    CHECK(focusedSpell() == SpellId::Bomb);
    // Standing right over it, E takes it back up.
    level.pickups().clear();
    place(1.0f, SpellId::SparkBolt, true, 0);
    place(12.0f, SpellId::Bomb, false, 0);
    CHECK(focusedSpell() == SpellId::SparkBolt);
    // Alone in reach, it is E's as usual.
    level.pickups().clear();
    place(6.0f, SpellId::SparkBolt, true, 0);
    CHECK(focusedSpell() == SpellId::SparkBolt);
    // A ware in reach still wins over a plain spell beside it.
    level.pickups().clear();
    place(4.0f, SpellId::SparkBolt, false, 0);
    place(-10.0f, SpellId::Fireball, false, 90);
    CHECK(focusedSpell() == SpellId::Fireball);
}

TEST_CASE("kills the world makes are counted, chained and paid") {
    SanctumRun s(12);
    RunState& run = s.run;
    Level& level = s.level;
    const Vec2 near = level.player().body.pos + Vec2{30.0f, -10.0f};
    auto kill = [&](const DamageInfo& blow) {
        Enemy& e = level.spawnEnemy(EnemyType::Slime, near);
        e.applyDamage(level, blow);
    };
    auto goldOnFloor = [&] {
        return std::count_if(level.pickups().begin(), level.pickups().end(), [](const Pickup& p) { return p.type == PickupType::Gold; });
    };
    const size_t goldBefore = goldOnFloor();
    // A spell's own blow is an ordinary kill.
    kill({1.0e6f, DamageType::Fire, {}, Team::Player, false, false, spellDef(SpellId::Fireball).name});
    CHECK(run.stats.alchemicalKills == 0);
    // Flames, lava and a gunpowder blast are the world's doing; three at once make a chain.
    kill({1.0e6f, DamageType::Fire, {}, Team::Monster, false, true, "flames"});
    kill({1.0e6f, DamageType::Fire, {}, Team::Monster, false, true, "Lava"});
    kill({1.0e6f, DamageType::Explosion, {}, Team::Monster, false, false, "a gunpowder blast"});
    CHECK(run.stats.alchemicalKills == 3);
    const std::vector<FeatId> earned = level.takeFeats();
    CHECK(std::find(earned.begin(), earned.end(), FeatId::ChainReaction) != earned.end());
    const size_t goldAfter = goldOnFloor();
    CHECK(goldAfter >= goldBefore + 3);
    CHECK(std::any_of(level.floatTexts().begin(), level.floatTexts().end(),
                      [](const FloatText& t) { return t.text == "Chain reaction x3"; }));
}

TEST_CASE("an arc spark's own splash is the spell's kill, not the world's") {
    SanctumRun s(12);
    RunState& run = s.run;
    Level& level = s.level;
    World& w = level.world();
    // A dry corridor in the brick under the sanctum floor (nothing in it conducts), ending in a wall, with a bat hanging
    // above the spark's line just short of the wall.
    constexpr int kRow = 225, kWallX = 260;
    for (int y = kRow - 12; y <= kRow + 3; ++y)
        for (int x = 200; x < kWallX; ++x) w.clear(x, y);
    const Enemy& bat = level.spawnEnemy(EnemyType::Bat, {static_cast<float>(kWallX) - 7.0f, static_cast<float>(kRow) - 6.0f});
    const CastShot shot = castOnce({SpellId::ArcBolt});
    REQUIRE(shot.projectiles.size() == 1);
    Projectile spark;
    spark.spell = shot.projectiles.front().spell;
    spark.def = shot.projectiles.front().def;
    spark.pos = spark.prevPos = {210.5f, static_cast<float>(kRow) + 0.5f};
    spark.vel = {spark.def.speed, 0.0f};
    spark.damageMul = 10.0f; // its splash on the wall alone is a killing blow
    const int kills = run.stats.kills, alchemical = run.stats.alchemicalKills;
    for (int i = 0; i < 20 && !spark.dead; ++i) updateProjectile(level, spark, kTickDt);
    CHECK(spark.dead);
    CHECK(!bat.alive());
    CHECK(run.stats.kills == kills + 1);
    CHECK(run.stats.alchemicalKills == alchemical); // no "Electrocuted!": the spell slew it, not the world
}

TEST_CASE("hits on one creature add up into one number until the moment passes") {
    SanctumRun s(12);
    Level& level = s.level;
    constexpr uint32_t kOwner = 0xABCDu; // no actor has this id
    const Vec2 p = level.player().body.pos + Vec2{30.0f, -10.0f};
    auto numbers = [&] {
        std::vector<const FloatText*> out;
        for (const FloatText& t : level.floatTexts())
            if (t.owner == kOwner) out.push_back(&t);
        return out;
    };
    level.addDamageNumber(kOwner, p, 4.0f, false);
    level.addDamageNumber(kOwner, p, 5.0f, true);
    std::vector<const FloatText*> shown = numbers();
    CHECK(shown.size() == 1);
    CHECK(shown.front()->text == "9!");
    CHECK(shown.front()->color == 0xffe040u);
    for (int i = 0; i < 30; ++i) level.tick(PlayerInput{}); // half a second: past the moment
    level.addDamageNumber(kOwner, p, 2.0f, false);
    shown = numbers();
    CHECK(shown.size() == 2);
    CHECK(shown.back()->text == "2");
}

TEST_CASE("a critical blast trebles on foes, never on the caster's side") {
    SanctumRun s(12);
    Level& level = s.level;
    // Deep in the brick under the sanctum floor, far from the player and from one another.
    const Enemy& struck = level.spawnEnemy(EnemyType::Golem, {100.0f, 230.0f});
    const Enemy& plain = level.spawnEnemy(EnemyType::Golem, {300.0f, 230.0f});
    const Enemy& homunculus = level.spawnEnemy(EnemyType::Companion, {200.0f, 230.0f});
    REQUIRE(homunculus.team == Team::Player);
    ExplosionSpec blast;
    blast.radius = 6.0f;
    blast.damage = 10.0f;
    blast.power = 0; // leaves the brick be
    blast.team = Team::Player;
    level.explode(plain.body.pos, blast);
    blast.crit = true;
    level.explode(struck.body.pos, blast);
    level.explode(homunculus.body.pos, blast); // the alchemist's blasts catch their own side too
    const float plainLoss = plain.maxHp - plain.hp, critLoss = struck.maxHp - struck.hp;
    CHECK(plainLoss > 0.0f);
    CHECK(std::fabs(critLoss - 3.0f * plainLoss) < 0.01f);
    CHECK(std::fabs((homunculus.maxHp - homunculus.hp) - 10.0f * 0.35f) < 0.01f); // the usual sting, no harder
    auto critNumber = [&](const Enemy& e) {
        return std::any_of(level.floatTexts().begin(), level.floatTexts().end(),
                           [&](const FloatText& t) { return t.owner == e.id && t.crit; });
    };
    CHECK(critNumber(struck));
    CHECK(!critNumber(plain));
    CHECK(!critNumber(homunculus));
}

TEST_CASE("reaching a stage is a goal met or not, never a count") {
    Milestones m; // a first run that ended on stage I still counts as having reached stage I
    m.deepest = 1;
    const UnlockProgress p = loadoutProgress(LoadoutId::Pyromancer, m);
    CHECK(p.value == 0 && p.target == 1);
    CHECK(loadoutRequirement(LoadoutId::Pyromancer, m).find('/') == std::string::npos);
    m.deepest = 2;
    CHECK(loadoutProgress(LoadoutId::Pyromancer, m).value == 1);
    CHECK(loadoutUnlocked(LoadoutId::Pyromancer, m));
}
