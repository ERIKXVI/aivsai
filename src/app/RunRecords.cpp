#include "app/RunRecords.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>

#include "game/Enemy.hpp"
#include "gen/Biome.hpp"

namespace pm {

static_assert(std::tuple_size_v<decltype(Profile::creatureKills)> == kMaxCreatureTypes, "profile and run kill counts must match");
static_assert(kSpellCount <= 63, "spellsSeen round-trips through run.sav via strtoll, so bit 63 must stay unused");
static_assert(kPerkCount <= 32, "Profile::perksSeen is a 32-bit mask (bit i = PerkId i)");

namespace {

namespace fs = std::filesystem;

constexpr int kSaveVersion = 1;

fs::path savePath() { return dataDirectory() / "run.sav"; }

/// Bit i set for each PerkId i the run holds, as in the perksSeen masks.
uint32_t heldPerkMask(const RunState& run) {
    uint32_t mask = 0;
    for (size_t i = 0; i < run.perks.size(); ++i)
        if (run.perks[i] > 0) mask |= 1u << i;
    return mask;
}

// --- Encoding ------------------------------------------------------------------------

std::string num(float v) {
    std::ostringstream ss;
    ss << std::setprecision(9) << v;
    return ss.str();
}

template <typename Range>
std::string list(const Range& values) {
    std::string out;
    for (auto v : values) {
        if (!out.empty()) out += ',';
        out += std::to_string(static_cast<long long>(v));
    }
    return out;
}

std::string encodeWand(const Wand& w) {
    std::ostringstream ss;
    ss << w.name << '|' << w.capacity << '|' << num(w.castDelay) << '|' << num(w.rechargeTime) << '|' << num(w.manaMax) << '|'
       << num(w.manaCharge) << '|' << num(w.spreadDeg) << '|' << num(w.speedMul) << '|' << w.spellsPerCast << '|'
       << (w.shuffle ? 1 : 0) << '|' << w.handleColor << '|' << w.gemColor << '|' << w.length << '|' << list(w.slots);
    return ss.str();
}

// --- Decoding (never throws; any malformed field rejects the save) -------------------

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::string item;
    std::istringstream ss(s);
    while (std::getline(ss, item, sep)) parts.push_back(item);
    if (!s.empty() && s.back() == sep) parts.emplace_back();
    return parts;
}

bool parseFloat(const std::string& s, float& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return end && *end == '\0';
}

template <typename T>
bool parseInt(const std::string& s, T& out, long long lo, long long hi) {
    long long v = 0;
    if (!parseInteger(s, v, lo, hi)) return false;
    out = static_cast<T>(v);
    return true;
}

bool parseSpells(const std::string& s, std::vector<SpellId>& out) {
    out.clear();
    if (s.empty()) return true;
    for (const std::string& part : split(s, ',')) {
        uint8_t v = 0;
        if (!parseInt(part, v, 0, kSpellCount - 1)) return false;
        out.push_back(static_cast<SpellId>(v));
    }
    return true;
}

bool decodeWand(const std::string& s, Wand& w) {
    const std::vector<std::string> f = split(s, '|');
    if (f.size() != 14) return false;
    w.name = f[0];
    bool ok = parseInt(f[1], w.capacity, 1, 64) && parseFloat(f[2], w.castDelay) && parseFloat(f[3], w.rechargeTime) &&
              parseFloat(f[4], w.manaMax) && parseFloat(f[5], w.manaCharge) && parseFloat(f[6], w.spreadDeg) &&
              parseFloat(f[7], w.speedMul) && parseInt(f[8], w.spellsPerCast, 1, 16) && parseInt(f[10], w.handleColor, 0, 0xFFFFFF) &&
              parseInt(f[11], w.gemColor, 0, 0xFFFFFF) && parseInt(f[12], w.length, 1, 32) && parseSpells(f[13], w.slots);
    w.shuffle = f[9] == "1";
    if (!ok || static_cast<int>(w.slots.size()) != w.capacity || w.manaMax <= 0.0f) return false;
    w.mana = w.manaMax;
    w.resetDeck();
    return true;
}

/// The value saved under `key`, or an empty string if the save has none.
std::string valueOf(const KeyValues& kv, const char* key) {
    auto it = kv.find(key);
    return it == kv.end() ? std::string() : it->second;
}

/// Parses the run's single-valued fields, `wandCount` among them; false rejects the save.
bool readScalarFields(const KeyValues& kv, RunState& r, int& wandCount) {
    int version = 0, difficulty = 0, loadout = 0;
    long long spellsSeen = 0;
    bool ok = parseInt(valueOf(kv, "version"), version, kSaveVersion, kSaveVersion) &&
              parseInt(valueOf(kv, "seed"), r.seed, 0, 0xFFFFFFFFll) &&
              parseInt(valueOf(kv, "difficulty"), difficulty, 0, kDifficultyCount - 1) &&
              parseInt(valueOf(kv, "loadout"), loadout, 0, kLoadoutCount - 1) &&
              parseInt(valueOf(kv, "level"), r.levelIndex, 0, kFinalStage) && parseFloat(valueOf(kv, "max_hp"), r.maxHp) &&
              parseFloat(valueOf(kv, "hp"), r.hp) && parseInt(valueOf(kv, "gold"), r.gold, 0, 1000000000) &&
              parseFloat(valueOf(kv, "levitation"), r.levitationMax) &&
              parseInt(valueOf(kv, "wands"), wandCount, 1, RunState::kMaxWands) &&
              parseInt(valueOf(kv, "active_wand"), r.activeWand, 0, RunState::kMaxWands - 1) &&
              parseInt(valueOf(kv, "flask_mat"), r.flask.mat, 0, kMaterialCount - 1) &&
              parseInt(valueOf(kv, "flask_amount"), r.flask.amount, 0, Flask::kCapacity) &&
              parseInteger(valueOf(kv, "spells_seen"), spellsSeen, std::numeric_limits<long long>::min(),
                           std::numeric_limits<long long>::max());
    if (!ok || r.maxHp <= 0.0f) return false;
    r.daily = valueOf(kv, "daily") == "1";
    r.inSanctum = valueOf(kv, "sanctum") == "1";
    r.difficulty = static_cast<Difficulty>(difficulty);
    r.loadout = static_cast<LoadoutId>(loadout);
    // Saves from before ordeals carry none.
    if (!valueOf(kv, "ordeal").empty() && !parseInt(valueOf(kv, "ordeal"), r.ordeal, 0, kMaxOrdeal)) return false;
    r.hp = std::clamp(r.hp, 1.0f, r.maxHp);
    r.spellsSeen = static_cast<uint64_t>(spellsSeen);
    return true;
}

/// Decodes the `wandCount` saved wands and keeps the wand in hand among them; false rejects the save.
bool readWands(const KeyValues& kv, int wandCount, RunState& r) {
    for (int i = 0; i < wandCount; ++i) {
        Wand w;
        if (!decodeWand(valueOf(kv, ("wand" + std::to_string(i)).c_str()), w)) return false;
        r.wands.push_back(std::move(w));
    }
    r.activeWand = std::min(r.activeWand, wandCount - 1);
    return true;
}

/// Parses the satchel's spells, one for each slot (empty ones included); false rejects the save.
bool readSatchel(const KeyValues& kv, RunState& r) {
    std::vector<SpellId> inventory;
    if (!parseSpells(valueOf(kv, "inventory"), inventory) || inventory.size() != r.inventory.size()) return false;
    std::copy(inventory.begin(), inventory.end(), r.inventory.begin());
    return true;
}

/// Parses the perks held and the perks seen this run; false rejects the save.
bool readPerks(const KeyValues& kv, RunState& r) {
    const std::vector<std::string> perks = split(valueOf(kv, "perks"), ',');
    if (perks.size() != r.perks.size()) return false;
    for (size_t i = 0; i < perks.size(); ++i)
        if (!parseInt(perks[i], r.perks[i], 0, 255)) return false;
    // Saves from before perksSeen carry none: the perks still held are all that is known.
    if (valueOf(kv, "perks_seen").empty()) {
        r.perksSeen |= heldPerkMask(r);
    } else if (!parseInt(valueOf(kv, "perks_seen"), r.perksSeen, 0, 0xFFFFFFFFll)) {
        return false;
    }
    return true;
}

/// Parses the run's statistics; false rejects the save.
bool readStats(const KeyValues& kv, RunState& r) {
    const std::vector<std::string> stats = split(valueOf(kv, "stats"), ',');
    // Saves from before alchemical kills were counted carry eight fields.
    return (stats.size() == 8 || stats.size() == 9) && parseFloat(stats[0], r.stats.time) &&
           parseInt(stats[1], r.stats.kills, 0, 1 << 30) && parseInt(stats[2], r.stats.goldCollected, 0, 1 << 30) &&
           parseInt(stats[3], r.stats.spellsCast, 0, 1 << 30) && parseInt(stats[4], r.stats.deepest, 0, 99) &&
           parseInt(stats[5], r.stats.chestsOpened, 0, 1 << 30) && parseFloat(stats[6], r.stats.damageDealt) &&
           parseFloat(stats[7], r.stats.damageTaken) &&
           (stats.size() != 9 || parseInt(stats[8], r.stats.alchemicalKills, 0, 1 << 30));
}

/// Parses the essences of the guardians slain and the kills by creature type; false rejects the save.
bool readKills(const KeyValues& kv, RunState& r) {
    const std::vector<std::string> kills = split(valueOf(kv, "kills_by_type"), ',');
    // Saves from before essences carry none.
    if (!valueOf(kv, "essences").empty() && !parseInt(valueOf(kv, "essences"), r.essences, 0, RunState::kAllEssences))
        return false;
    if (kills.size() > r.killsByType.size()) return false; // older saves know fewer creatures
    for (size_t i = 0; i < kills.size(); ++i)
        if (!parseInt(kills[i], r.killsByType[i], 0, 1 << 30)) return false;
    return true;
}

// --- Folding a run into the profile --------------------------------------------------

/// Adds the run to the profile's lifetime statistics and bests.
void addStatistics(Profile& p, const RunState& run, bool victory) {
    p.runs++;
    p.deepest = std::max(p.deepest, run.levelIndex + 1);
    p.totalKills += run.stats.kills;
    p.totalGold += run.stats.goldCollected;
    if (victory) {
        p.victories++;
        if (p.bestTime <= 0.0f || run.stats.time < p.bestTime) p.bestTime = run.stats.time;
    }
    if (run.daily) p.bestDailyDepth = std::max(p.bestDailyDepth, run.levelIndex + 1);
    p.chestsOpened += run.stats.chestsOpened;
    p.bestKills = std::max(p.bestKills, run.stats.kills);
    p.bestGold = std::max(p.bestGold, run.stats.goldCollected);
}

/// Grimoire discoveries: records the spells and perks the run met and the creatures it slew.
void addToGrimoire(Profile& p, const RunState& run) {
    p.spellsSeen |= run.spellsSeen;
    for (const Wand& w : run.wands)
        for (SpellId s : w.slots)
            if (s != SpellId::None) p.spellsSeen |= 1ull << static_cast<unsigned>(s);
    p.perksSeen |= run.perksSeen; // includes perks spent since (Second Life)
    p.perksSeen |= heldPerkMask(run);
    for (size_t i = 0; i < p.creatureKills.size(); ++i) p.creatureKills[i] += run.killsByType[i];
}

/// Puts the run at the head of the chronicle, which keeps the latest Profile::kHistorySize runs.
void addToChronicle(Profile& p, const RunState& run, bool victory, const std::string& fate) {
    RunHistoryEntry entry;
    entry.seed = run.seed;
    entry.loadout = static_cast<int>(run.loadout);
    entry.difficulty = static_cast<int>(run.difficulty);
    entry.depth = std::clamp(run.levelIndex, 0, kFinalStage);
    entry.sanctum = run.inSanctum;
    entry.victory = victory;
    entry.daily = run.daily;
    entry.ordeal = run.ordeal;
    entry.time = run.stats.time;
    entry.kills = run.stats.kills;
    entry.fate = fate;
    p.history.insert(p.history.begin(), std::move(entry));
    if (p.history.size() > Profile::kHistorySize) p.history.resize(Profile::kHistorySize);
}

/// Appends a count such as "1 spell" or "3 spells" to a comma-separated list; nothing when `n` is not positive.
void appendCount(std::string& text, int n, const char* one, const char* many) {
    if (n <= 0) return;
    if (!text.empty()) text += ", ";
    text += std::to_string(n) + " " + (n == 1 ? one : many);
}

/// What the run found that no run before it had, e.g. "2 spells, 1 perk"; empty if nothing.
std::string describeDiscoveries(const Profile& before, const Profile& p) {
    const int spells = std::popcount(p.spellsSeen & ~before.spellsSeen);
    const int perks = std::popcount(p.perksSeen & ~before.perksSeen);
    int creatures = 0;
    for (size_t i = 0; i < p.creatureKills.size(); ++i)
        creatures += before.creatureKills[i] == 0 && p.creatureKills[i] > 0 ? 1 : 0;
    std::string discoveries;
    appendCount(discoveries, spells, "spell", "spells");
    appendCount(discoveries, creatures, "creature", "creatures");
    appendCount(discoveries, perks, "perk", "perks");
    return discoveries;
}

/// Notes the starting paths the run unlocked and the locked path it left nearest to opening.
void notePathProgress(const Profile& before, const Profile& p, RunRecord& record) {
    const Milestones was = milestonesOf(before);
    const Milestones now = milestonesOf(p);
    float best = -1.0f;
    for (int i = 0; i < kLoadoutCount; ++i) {
        const auto id = static_cast<LoadoutId>(i);
        if (!loadoutUnlocked(id, was) && loadoutUnlocked(id, now)) record.unlockedPaths.push_back(id);
        // The locked path nearest to opening is the carrot for the next run.
        const UnlockProgress prior = loadoutProgress(id, was), after = loadoutProgress(id, now);
        if (after.target <= 0 || after.value >= after.target) continue;
        const float fraction = static_cast<float>(after.value) / static_cast<float>(after.target);
        if (fraction <= best) continue;
        best = fraction;
        record.nearestPath = PathProgress{loadoutDef(id).name, after.text, prior.value, after.value, after.target};
    }
}

/// Opens the next ordeal when the run earned it; returns the ordeal opened, or 0 if none.
int openNextOrdeal(Profile& p, const RunState& run, bool victory) {
    // Completing the Work under the highest ordeal open (or any ordeal, the first time) opens the next.
    if (victory && run.ordeal >= p.ordealUnlocked && p.ordealUnlocked < kMaxOrdeal) {
        p.ordealUnlocked = run.ordeal + 1;
        return p.ordealUnlocked;
    }
    return 0;
}

/// The records the run broke, as the end screen lists them.
std::vector<std::string> personalBests(const Profile& before, const RunState& run, bool victory) {
    std::vector<std::string> records;
    // Personal bests; a first run sets every one of them, which is no news.
    if (victory && before.victories == 0) records.emplace_back("Your first Great Work!");
    if (before.runs > 0) {
        if (run.levelIndex + 1 > before.deepest) records.emplace_back("Deepest descent yet");
        if (run.stats.kills > before.bestKills && run.stats.kills > 0) records.emplace_back("Most creatures slain in a run");
        if (before.bestGold >= 0 && run.stats.goldCollected > before.bestGold && run.stats.goldCollected > 0)
            records.emplace_back("Most gold gathered in a run");
    }
    return records;
}

} // namespace

RunRecord recordRun(Profile& profile, const RunState& run, bool victory, const std::string& fate) {
    RunRecord record = foldRun(profile, run, victory, fate);
    saveProfile(profile);
    return record;
}

Milestones milestonesOf(const Profile& p) {
    int guardians = 0;
    for (int i = 0; i < kGuardianCount; ++i) guardians += p.creatureKills[static_cast<size_t>(guardianFor(i))];
    // The Hermeticist once opened to a victory: a victor keeps the path they earned.
    if (p.victories > 0) guardians = std::max(guardians, 1);
    return {p.deepest, p.victories, p.chestsOpened, p.totalKills, guardians};
}

RunRecord foldRun(Profile& p, const RunState& run, bool victory, const std::string& fate) {
    const Profile before = p;
    addStatistics(p, run, victory);
    addToGrimoire(p, run);
    addToChronicle(p, run, victory, fate);

    RunRecord record;
    record.discoveries = describeDiscoveries(before, p);
    notePathProgress(before, p, record);
    record.unlockedOrdeal = openNextOrdeal(p, run, victory);
    record.records = personalBests(before, run, victory);
    return record;
}

std::string depthLabel(const RunHistoryEntry& e) {
    if (e.victory) return "Magnum Opus";
    if (e.sanctum) return std::string("Sanctum after ") + stageNumeral(e.depth);
    return std::string(stageNumeral(e.depth)) + " " + biomeDef(biomeForDepth(e.depth)).name;
}

void writeRunSave(const RunState& r, const fs::path& path) {
    KeyValues kv;
    kv["version"] = std::to_string(kSaveVersion);
    kv["seed"] = std::to_string(r.seed);
    kv["daily"] = r.daily ? "1" : "0";
    kv["difficulty"] = std::to_string(static_cast<int>(r.difficulty));
    kv["loadout"] = std::to_string(static_cast<int>(r.loadout));
    kv["level"] = std::to_string(r.levelIndex);
    kv["ordeal"] = std::to_string(r.ordeal);
    kv["sanctum"] = r.inSanctum ? "1" : "0";
    kv["max_hp"] = num(r.maxHp);
    kv["hp"] = num(r.hp);
    kv["gold"] = std::to_string(r.gold);
    kv["levitation"] = num(r.levitationMax);
    kv["active_wand"] = std::to_string(r.activeWand);
    kv["wands"] = std::to_string(r.wands.size());
    for (size_t i = 0; i < r.wands.size(); ++i) kv["wand" + std::to_string(i)] = encodeWand(r.wands[i]);
    kv["inventory"] = list(r.inventory);
    kv["flask_mat"] = std::to_string(r.flask.mat);
    kv["flask_amount"] = std::to_string(r.flask.amount);
    kv["perks"] = list(r.perks);
    kv["stats"] = num(r.stats.time) + "," + std::to_string(r.stats.kills) + "," + std::to_string(r.stats.goldCollected) + "," +
                  std::to_string(r.stats.spellsCast) + "," + std::to_string(r.stats.deepest) + "," +
                  std::to_string(r.stats.chestsOpened) + "," + num(r.stats.damageDealt) + "," + num(r.stats.damageTaken) + "," +
                  std::to_string(r.stats.alchemicalKills);
    kv["spells_seen"] = std::to_string(r.spellsSeen);
    kv["perks_seen"] = std::to_string(r.perksSeen);
    kv["kills_by_type"] = list(r.killsByType);
    kv["essences"] = std::to_string(r.essences);
    writeKeyValues(path, kv);
}

std::optional<RunState> readRunSave(const fs::path& path) {
    // readKeyValues falls back to the temporary file when a replace was interrupted.
    const KeyValues kv = readKeyValues(path);
    if (kv.empty()) return std::nullopt; // no save
    RunState r;
    int wandCount = 0;
    const bool ok = readScalarFields(kv, r, wandCount) && readWands(kv, wandCount, r) && readSatchel(kv, r) &&
                    readPerks(kv, r) && readStats(kv, r) && readKills(kv, r);
    if (!ok) return std::nullopt;
    return r;
}

void saveSuspendedRun(const RunState& run) { writeRunSave(run, savePath()); }

std::optional<RunState> loadSuspendedRun() { return readRunSave(savePath()); }

void discardSuspendedRun() { removeKeyValues(savePath()); }

} // namespace pm
