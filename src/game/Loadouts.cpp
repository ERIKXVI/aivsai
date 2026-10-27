#include "game/Loadouts.hpp"

#include <algorithm>
#include <iterator>
#include <cstdio>

#include "core/Rng.hpp"
#include "game/Run.hpp"

namespace pm {

namespace {

constexpr LoadoutDef kDefs[] = {
    {"Apprentice", "A spark rod, a digging wand and a flask of water. Humble, dependable.", 0, false},
    {"Pyromancer", "An ember staff, a flask of oil and skin that shrugs off flame.", 0, false},
    {"Geomancer", "Picks, dynamite, a flask of acid and a stout frame. The mountain is a door.", 1, false},
    {"Stormcaller", "Leaping arcs, a bottled storm and a flask of water to carry it.", 0, false},
    {"Hermeticist", "A wand of power rolled anew each run, homing sigils and a gift.", 0, true},
};
static_assert(std::size(kDefs) == kLoadoutCount, "a LoadoutDef for every LoadoutId");

struct Requirement {
    const char* text;
    int Milestones::*field;
    int target;
    bool counted = true; ///< progress shown as n/target; false: a yes/no goal (reaching a stage)
};

constexpr Requirement kRequirements[] = {
    {"", nullptr, 0},
    // Paced so a first session opens one or two paths: an unlock at the end of a run is the
    // best reason to start another.
    {"Reach the second stage, Albedo", &Milestones::deepest, 2, false},
    {"Open chests", &Milestones::chestsOpened, 8},
    {"Slay creatures", &Milestones::totalKills, 100},
    {"Slay a stage guardian", &Milestones::guardiansSlain, 1},
};
static_assert(std::size(kRequirements) == kLoadoutCount, "a Requirement for every LoadoutId");

Wand makeWand(const char* name, int capacity, float delay, float recharge, float mana, float charge, float spread,
              std::initializer_list<SpellId> spells, uint32_t handle, uint32_t gem, int length) {
    Wand w;
    w.name = name;
    w.capacity = capacity;
    w.castDelay = delay;
    w.rechargeTime = recharge;
    w.manaMax = w.mana = mana;
    w.manaCharge = charge;
    w.spreadDeg = spread;
    w.slots.assign(spells);
    w.slots.resize(static_cast<size_t>(capacity), SpellId::None);
    w.handleColor = handle;
    w.gemColor = gem;
    w.length = length;
    w.resetDeck();
    return w;
}

void giveFlask(RunState& run, MatId mat, int amount) {
    run.flask.mat = mat;
    run.flask.amount = amount;
}

} // namespace

const LoadoutDef& loadoutDef(LoadoutId id) { return kDefs[static_cast<size_t>(id)]; }

bool loadoutUnlocked(LoadoutId id, const Milestones& m) {
    const Requirement& r = kRequirements[static_cast<size_t>(id)];
    return r.field == nullptr || m.*r.field >= r.target;
}

UnlockProgress loadoutProgress(LoadoutId id, const Milestones& m) {
    const Requirement& r = kRequirements[static_cast<size_t>(id)];
    if (r.field == nullptr) return {};
    // A depth counts the stage a run starts on, so "1/2" after a first-stage death would read as
    // progress that was never made: such goals are simply met or not.
    if (!r.counted) return {r.text, m.*r.field >= r.target ? 1 : 0, 1};
    return {r.text, m.*r.field, r.target};
}

std::string loadoutRequirement(LoadoutId id, const Milestones& m) {
    const Requirement& r = kRequirements[static_cast<size_t>(id)];
    if (r.field == nullptr) return {};
    char buf[96];
    if (r.counted && r.target > 1) std::snprintf(buf, sizeof(buf), "%s (%d/%d)", r.text, std::min(m.*r.field, r.target), r.target);
    else std::snprintf(buf, sizeof(buf), "%s", r.text);
    return buf;
}

void equipLoadout(RunState& run, LoadoutId id) {
    run.loadout = id;
    switch (id) {
    case LoadoutId::Apprentice: break;
    case LoadoutId::Pyromancer:
        run.wands[0] = makeWand("Ember Staff", 4, 0.12f, 0.40f, 280.0f, 115.0f, 2.0f,
                                {SpellId::FireBolt, SpellId::SparkBolt, SpellId::FireBolt}, 0x5a2a1a, 0xff7a30, 10);
        run.inventory.fill(SpellId::None);
        run.addSpell(SpellId::Fireball);
        run.addSpell(SpellId::Incendiary);
        giveFlask(run, mat::Oil, 500);
        run.applyPerk(PerkId::Fireproof);
        break;
    case LoadoutId::Geomancer:
        run.wands[1] = makeWand("Quarry Wand", 4, 0.09f, 0.50f, 240.0f, 95.0f, 0.0f,
                                {SpellId::DiggingBolt, SpellId::Dynamite}, 0x5a4a3a, 0xc08040, 9);
        run.inventory.fill(SpellId::None);
        run.addSpell(SpellId::Chainsaw);
        run.addSpell(SpellId::AcidGlob);
        run.addSpell(SpellId::Bomb);
        giveFlask(run, mat::Acid, 400);
        run.maxHp += 30.0f;
        run.hp = run.maxHp;
        break;
    case LoadoutId::Stormcaller:
        run.wands[0] = makeWand("Tempest Rod", 4, 0.13f, 0.38f, 300.0f, 130.0f, 2.5f,
                                {SpellId::ArcBolt, SpellId::SparkBolt, SpellId::ArcBolt}, 0x3a3a5a, 0x9ad0ff, 10);
        run.inventory.fill(SpellId::None);
        run.addSpell(SpellId::Lightning);
        giveFlask(run, mat::Water, 700);
        run.applyPerk(PerkId::Insulated);
        break;
    case LoadoutId::Hermeticist: {
        Rng rng(run.seed ^ 0x4e524d53u);
        run.wands[0] = makeRandomWand(2, rng);
        run.inventory.fill(SpellId::None);
        run.addSpell(SpellId::Homing);
        run.addSpell(randomSpell(2, rng));
        static constexpr PerkId kGifts[] = {PerkId::Vitality, PerkId::ArcaneFlow, PerkId::CriticalMind, PerkId::Swiftness,
                                            PerkId::LuckyDay, PerkId::Expansion};
        run.applyPerk(rng.pick(kGifts));
        run.gold += 120;
        break;
    }
    case LoadoutId::Count: break;
    }
    // The bomb waits in the satchel of every kit that keeps the Delver's Wand: on the digging
    // wand it went off in the alchemist's face. (The Geomancer packs its own.)
    if (id != LoadoutId::Geomancer) run.addSpell(SpellId::Bomb);
    for (const Wand& w : run.wands) run.noteWand(w);
}

} // namespace pm
