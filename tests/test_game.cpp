// Game-rule tests: the spell table, wand deck casting, starting loadouts and run saves.

#include <cstring>
#include <filesystem>
#include <fstream>

#include "app/RunRecords.hpp"
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
            CHECK(run.flask.amount >= 0 && run.flask.amount <= Flask::kCapacity);
        }
    }
    // Hermeticist wands are rolled from the seed: same seed, same wand.
    RunState a = newRun(77u, Difficulty::Adept, false, LoadoutId::Hermeticist);
    RunState b = newRun(77u, Difficulty::Adept, false, LoadoutId::Hermeticist);
    CHECK(a.wands[0].slots == b.wands[0].slots);
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
