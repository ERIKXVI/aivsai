// Game-rule tests: the spell table, wand deck casting, starting loadouts, run saves and the
// death screen's cause and advice.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "app/Persistence.hpp"
#include "app/RunRecords.hpp"
#include "audio/Audio.hpp"
#include "core/ThreadPool.hpp"
#include "game/Actor.hpp"
#include "game/Body.hpp"
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

} // namespace

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
}

TEST_CASE("found attack spells go straight onto the active wand") {
    initMaterials(); // the filter reads what a spell's spilled matter does
    RunState run = newRun(5u, Difficulty::Adept, false, LoadoutId::Apprentice);
    // The digging wand only digs; its bomb waits in the satchel.
    for (SpellId s : run.wands[1].slots) CHECK(s != SpellId::Bomb);
    CHECK(std::find(run.inventory.begin(), run.inventory.end(), SpellId::Bomb) != run.inventory.end());

    run.activeWand = 0;
    CHECK(run.wands[0].slots[3] == SpellId::None);
    CHECK(run.trySlotIntoActiveWand(SpellId::FireBolt));
    CHECK(run.wands[0].slots[3] == SpellId::FireBolt);
    CHECK((run.spellsSeen >> static_cast<unsigned>(SpellId::FireBolt)) & 1ull);
    // The rod is full now, so the next spell is left for the satchel.
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
    // The Hermeticist opens to a guardian's slayer, before any victory.
    Milestones slayer;
    slayer.guardiansSlain = 1;
    CHECK(loadoutUnlocked(LoadoutId::Hermeticist, slayer));
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
    CHECK(r.perks == run.perks);
    CHECK(r.stats.kills == 57 && r.stats.time == run.stats.time && r.stats.alchemicalKills == 6);
    CHECK(r.killsByType == run.killsByType && r.spellsSeen == run.spellsSeen);

    // A save from before alchemical kills were counted (eight stats) still loads.
    {
        KeyValues kv = readKeyValues(path);
        std::string& stats = kv["stats"];
        stats.erase(stats.rfind(','));
        writeKeyValues(path, kv);
    }
    const std::optional<RunState> older = readRunSave(path);
    REQUIRE(older.has_value());
    CHECK(older->stats.kills == 57 && older->stats.alchemicalKills == 0);

    // A damaged file is rejected rather than half-loaded.
    {
        std::ofstream out(path, std::ios::trunc);
        out << "version=1\nseed=12\nwands=2\nwand0=broken\n";
    }
    CHECK(!readRunSave(path).has_value());
    std::filesystem::remove(path);
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

TEST_CASE("sanctum lanterns stay hung") {
    initMaterials();
    ThreadPool pool(2);
    Audio audio;
    RunState run = newRun(11, Difficulty::Adept, false);
    Level level(generateSanctum(1, 11u, pool), run, audio, pool);
    const size_t lanterns = level.lanterns().size();
    CHECK(lanterns > 0);
    for (int i = 0; i < 60; ++i) level.tick(PlayerInput{});
    CHECK(level.lanterns().size() == lanterns);
}

TEST_CASE("a spell through a lantern knocks it down to shatter") {
    initMaterials();
    ThreadPool pool(2);
    Audio audio;
    RunState run = newRun(11, Difficulty::Adept, false);
    Level level(generateSanctum(1, 11u, pool), run, audio, pool);
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

TEST_CASE("guardians rage, the toad sheds its brood and the slayer earns a perk") {
    initMaterials();
    ThreadPool pool(2);
    Audio audio;
    RunState run = newRun(11, Difficulty::Adept, false);
    Level level(generateSanctum(0, 11u, pool), run, audio, pool);
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
    int after = 0;
    for (uint8_t n : run.perks) after += n;
    CHECK(after == perks + 1);
}

TEST_CASE("feats are described and kills earn them") {
    for (int i = 0; i < kFeatCount; ++i) {
        const FeatDef& d = featDef(static_cast<FeatId>(i));
        CHECK(d.name && d.name[0] && d.description && d.description[0]);
        for (int j = 0; j < i; ++j) CHECK(std::strcmp(d.name, featDef(static_cast<FeatId>(j)).name) != 0);
    }
    initMaterials();
    ThreadPool pool(2);
    Audio audio;
    RunState run = newRun(11, Difficulty::Adept, false);
    Level level(generateSanctum(0, 11u, pool), run, audio, pool);
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

TEST_CASE("elixirs lend haste and fury for a while") {
    initMaterials();
    ThreadPool pool(2);
    Audio audio;
    RunState run = newRun(12, Difficulty::Adept, false);
    Level level(generateSanctum(0, 12u, pool), run, audio, pool);
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
    initMaterials();
    ThreadPool pool(2);
    Audio audio;
    RunState run = newRun(13, Difficulty::Adept, false);
    Level level(generateSanctum(0, 13u, pool), run, audio, pool);
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
    initMaterials();
    ThreadPool pool(2);
    Audio audio;
    RunState run = newRun(14, Difficulty::Adept, false);
    Level level(generateSanctum(0, 14u, pool), run, audio, pool);
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
            const SpellDef& d = spellDef(randomRareSpell(tier, rng));
            CHECK(d.type == SpellType::Projectile && !d.trigger);
            CHECK(d.tier >= std::clamp(tier + 1, 2, 4));
            showpiece = showpiece || d.tier >= 3;
        }
        CHECK(showpiece); // even the first stage can turn up Lightning or a Black Hole
    }
}

TEST_CASE("the first sanctum's shop stocks the coming depth") {
    initMaterials();
    ThreadPool pool(2);
    Audio audio;
    bool deeper = false;
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        RunState run = newRun(seed, Difficulty::Adept, false);
        Level level(generateSanctum(0, seed, pool), run, audio, pool);
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

TEST_CASE("kills the world makes are counted, chained and paid") {
    initMaterials();
    ThreadPool pool(2);
    Audio audio;
    RunState run = newRun(12, Difficulty::Adept, false);
    Level level(generateSanctum(0, 12u, pool), run, audio, pool);
    const Vec2 near = level.player().body.pos + Vec2{30.0f, -10.0f};
    auto kill = [&](const DamageInfo& blow) {
        Enemy& e = level.spawnEnemy(EnemyType::Slime, near);
        e.applyDamage(level, blow);
    };
    const size_t goldBefore = std::count_if(level.pickups().begin(), level.pickups().end(),
                                            [](const Pickup& p) { return p.type == PickupType::Gold; });
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
    const size_t goldAfter = std::count_if(level.pickups().begin(), level.pickups().end(),
                                           [](const Pickup& p) { return p.type == PickupType::Gold; });
    CHECK(goldAfter >= goldBefore + 3);
    CHECK(std::any_of(level.floatTexts().begin(), level.floatTexts().end(),
                      [](const FloatText& t) { return t.text == "Chain reaction x3"; }));
}
