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
    CHECK(plain.projectiles.size() == 1 && boosted.projectiles.size() == 1);
    CHECK(boosted.projectiles[0].def.damage > plain.projectiles[0].def.damage);
    CHECK(boosted.mana > plain.mana);

    // A multicast draws several cards into one cast.
    CastShot multi = castOnce({SpellId::DoubleCast, SpellId::SparkBolt, SpellId::MagicMissile, SpellId::Bomb});
    CHECK(multi.projectiles.size() == 2);

    // A trigger carries the next card as its impact payload.
    CastShot trig = castOnce({SpellId::SparkTrigger, SpellId::Bomb});
    CHECK(trig.projectiles.size() == 1);
    CHECK(trig.projectiles[0].payload != nullptr);
    if (trig.projectiles[0].payload) CHECK(trig.projectiles[0].payload->projectiles.size() == 1);

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
    for (int d = 0; d < 3; ++d) {
        for (int l = 0; l < kLoadoutCount; ++l) {
            RunState run = newRun(1234u + static_cast<uint32_t>(l), static_cast<Difficulty>(d), false, static_cast<LoadoutId>(l));
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
    Milestones veteran{5, 1, 40, 1000};
    for (int l = 0; l < kLoadoutCount; ++l) CHECK(loadoutUnlocked(static_cast<LoadoutId>(l), veteran));
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

    const auto path = std::filesystem::temp_directory_path() / "pm_test_run.sav";
    writeRunSave(run, path);
    const std::optional<RunState> loaded = readRunSave(path);
    CHECK(loaded.has_value());
    if (loaded) {
        const RunState& r = *loaded;
        CHECK(r.seed == run.seed && r.difficulty == run.difficulty && r.loadout == run.loadout);
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
        CHECK(r.stats.kills == 57 && r.stats.time == run.stats.time);
        CHECK(r.killsByType == run.killsByType && r.spellsSeen == run.spellsSeen);
    }

    // A damaged file is rejected rather than half-loaded.
    {
        std::ofstream out(path, std::ios::trunc);
        out << "version=1\nseed=12\nwands=2\nwand0=broken\n";
    }
    CHECK(!readRunSave(path).has_value());
    std::filesystem::remove(path);
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
    CHECK(std::strstr(deathAdvice(victim), "obsidian") != nullptr);

    victim.lastHitCause = "Bomb";
    victim.lastHitSelf = true;
    CHECK(deathCause(victim) == "Undone by your own Bomb");
    CHECK(std::strstr(deathAdvice(victim), "Your own spells") != nullptr);
}

TEST_CASE("the end screen shows the nearest locked path and personal bests") {
    Profile profile;
    profile.runs = 3;
    profile.deepest = 1;
    profile.chestsOpened = 9;
    profile.bestKills = 10;
    RunState run = newRun(9, Difficulty::Adept, false);
    run.levelIndex = 1;
    run.stats.kills = 14;
    run.stats.chestsOpened = 2;
    const RunRecord record = foldRun(profile, run, false, "Slain by a Fire Imp");
    // Chests 11/12 beats reaching the third stage (2/3) and slaying 14/250.
    CHECK(record.nearestPath.has_value());
    CHECK(record.nearestPath->name == "Geomancer");
    CHECK(record.nearestPath->was == 9 && record.nearestPath->now == 11 && record.nearestPath->target == 12);
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
    for (int t = 0; t < 4; ++t) CHECK(enemyDef(guardianFor(t)).guardian);
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
    CHECK(!find()->enraged());
    hurtTo(0.45f);
    CHECK(find()->enraged());
    hurtTo(0.3f);
    CHECK(level.enemies().size() == before + 4); // and the second
    int perks = 0;
    for (uint8_t n : run.perks) perks += n;
    Enemy* t = find();
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

TEST_CASE("stage lanterns hang from solid ceilings, not pooled steam") {
    initMaterials();
    ThreadPool pool(4);
    Audio audio;
    // Seed 5's Athanor once hung lanterns from steam; they fell and burned as the player arrived.
    for (int index = 0; index < 5; ++index) {
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

TEST_CASE("key=value files survive an interrupted replace") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "pm_test_kv";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path file = dir / "profile.dat";
    writeKeyValues(file, {{"runs", "7"}, {"fate", "a=b"}});
    KeyValues kv = readKeyValues(file);
    CHECK(kv["runs"] == "7");
    CHECK(kv["fate"] == "a=b"); // values may contain '='

    // Only the temporary file is left (old file removed, rename failed): it is still read.
    fs::path tmp = file;
    tmp += ".tmp";
    fs::rename(file, tmp, ec);
    CHECK(!fs::exists(file));
    CHECK(readKeyValues(file)["runs"] == "7");
    fs::remove_all(dir, ec);
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
