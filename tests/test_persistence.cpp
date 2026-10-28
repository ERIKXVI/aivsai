// Persistence: key=value files, the strict integer parser the save formats share, and the
// settings and profile field mappings, including the upgrade of profiles from older builds.

#include <filesystem>
#include <fstream>
#include <string>

#include "app/Persistence.hpp"
#include "game/Ordeals.hpp"
#include "tests/TestFramework.hpp"

using namespace pm;

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

    // Removing the file takes the leftover temporary file with it, so nothing comes back.
    writeKeyValues(file, {{"runs", "8"}});
    fs::copy_file(file, tmp, fs::copy_options::overwrite_existing, ec);
    removeKeyValues(file);
    CHECK(!fs::exists(file) && !fs::exists(tmp));
    CHECK(readKeyValues(file).empty());
    fs::remove_all(dir, ec);
}

TEST_CASE("key=value files fall back to the previous write after a power cut") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "pm_test_kv_bak";
    std::error_code ec;
    fs::remove_all(dir, ec); // nothing left over from an aborted run
    fs::create_directories(dir, ec);
    const fs::path file = dir / "run.sav";
    fs::path bak = file;
    bak += ".bak";
    writeKeyValues(file, {{"runs", "1"}});
    writeKeyValues(file, {{"runs", "2"}});
    CHECK(readKeyValues(file)["runs"] == "2");
    CHECK(fs::exists(bak));

    // The new file's data never reached the disk: zero-filled or empty, the backup is read.
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        const std::string zeros(64, '\0');
        out.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    }
    CHECK(readKeyValues(file)["runs"] == "1");
    { std::ofstream out(file, std::ios::trunc); }
    CHECK(readKeyValues(file)["runs"] == "1");
    // A file the player deleted is not brought back.
    fs::remove(file, ec);
    CHECK(readKeyValues(file).empty());

    // Removing the file takes the backup with it, so an abandoned run cannot come back.
    writeKeyValues(file, {{"runs", "3"}});
    writeKeyValues(file, {{"runs", "4"}});
    CHECK(fs::exists(file) && fs::exists(bak));
    removeKeyValues(file);
    CHECK(!fs::exists(file) && !fs::exists(bak));
    CHECK(readKeyValues(file).empty());
    fs::remove_all(dir, ec);
}

TEST_CASE("integers parse strictly") {
    long long v = 0;
    CHECK(parseInteger("42", v, 0, 100) && v == 42);
    CHECK(parseInteger("-7", v, -10, 10) && v == -7);
    CHECK(!parseInteger("", v, 0, 100));
    CHECK(!parseInteger("12abc", v, 0, 100));
    CHECK(!parseInteger("4 ", v, 0, 100));
    CHECK(!parseInteger("101", v, 0, 100));
    CHECK(!parseInteger("-1", v, 0, 100));
}

TEST_CASE("settings survive a save and load") {
    Settings s;
    s.masterVolume = 0.25f;
    s.musicVolume = 0.5f;
    s.sfxVolume = 0.125f;
    s.screenShake = false;
    s.fullscreen = false;
    s.showFps = true;
    s.difficulty = 2;
    s.loadout = 3;
    s.hints = false;
    s.matterLens = false;
    s.highQuality = false;
    s.ordeal = 4;
    const Settings back = settingsFromKeyValues(settingsToKeyValues(s));
    CHECK(back.masterVolume == s.masterVolume && back.musicVolume == s.musicVolume && back.sfxVolume == s.sfxVolume);
    CHECK(!back.screenShake && !back.fullscreen && back.showFps);
    CHECK(back.difficulty == 2 && back.loadout == 3 && back.ordeal == 4);
    CHECK(!back.hints && !back.matterLens && !back.highQuality);

    // Missing or unreadable keys keep their defaults; an ordeal past the last is capped.
    const Settings defaults;
    const Settings sparse = settingsFromKeyValues({{"ordeal", "99"}, {"music_volume", "loud"}});
    CHECK(sparse.ordeal == kMaxOrdeal);
    CHECK(sparse.musicVolume == defaults.musicVolume && sparse.masterVolume == defaults.masterVolume);
    CHECK(sparse.difficulty == defaults.difficulty && sparse.fullscreen == defaults.fullscreen && sparse.hints == defaults.hints);
}

TEST_CASE("the profile survives a save and load") {
    Profile p;
    p.runs = 12;
    p.victories = 2;
    p.deepest = 5;
    p.bestTime = 1500.5f;
    p.totalKills = 640;
    p.totalGold = 9000;
    p.lastDailySeed = 123456;
    p.bestDailyDepth = 3;
    p.chestsOpened = 30;
    p.bestKills = 88;
    p.bestGold = 1200;
    p.featsEarned = 0x5A5u;
    p.ordealUnlocked = 3;
    p.spellsSeen = 0x7FFF00FF00FFull;
    p.perksSeen = 0x3Fu;
    p.codexSeen = 0xA5C3E1u;
    p.creatureKills[0] = 40;
    p.creatureKills[p.creatureKills.size() - 1] = 7;
    for (uint32_t i = 0; i < Profile::kHistorySize + 2; ++i) {
        RunHistoryEntry e;
        e.seed = 100 + i;
        e.kills = static_cast<int>(i);
        e.fate = "Slain by a Fire Imp";
        p.history.push_back(e);
    }
    const Profile back = profileFromKeyValues(profileToKeyValues(p));
    CHECK(back.runs == 12 && back.victories == 2 && back.deepest == 5 && back.bestTime == p.bestTime);
    CHECK(back.totalKills == 640 && back.totalGold == 9000 && back.lastDailySeed == 123456 && back.bestDailyDepth == 3);
    CHECK(back.chestsOpened == 30 && back.bestKills == 88 && back.bestGold == 1200);
    CHECK(back.featsEarned == p.featsEarned && back.ordealUnlocked == 3);
    CHECK(back.spellsSeen == p.spellsSeen && back.perksSeen == p.perksSeen && back.creatureKills == p.creatureKills);
    CHECK(back.codexSeen == p.codexSeen);
    // Only the most recent entries of the chronicle are kept.
    CHECK(back.history.size() == Profile::kHistorySize);
    CHECK(back.history.front().seed == 100 && back.history.back().seed == 100 + Profile::kHistorySize - 1);
    CHECK(back.history.front().fate == "Slain by a Fire Imp");
}

TEST_CASE("profiles from older builds are upgraded on load") {
    // From before ordeals and personal bests: one completed Work and a two-run chronicle.
    KeyValues old{{"runs", "6"},
                  {"victories", "1"},
                  {"deepest", "5"},
                  {"history.0", "7|0|1|4|0|1|0|900|31|Completed the Great Work"},
                  {"history.1", "8|0|1|2|1|0|0|300|12|Slain by a Fire Imp"}};
    const Profile p = profileFromKeyValues(old);
    CHECK(p.ordealUnlocked == 1); // the completed Work has already earned the first ordeal
    CHECK(p.bestKills == 31);     // recovered from what the chronicle remembers
    CHECK(p.bestGold == -1);      // unknown, so the next run claims no false record
    CHECK(p.codexSeen == 0);      // from before the Alchemical Codex: nothing witnessed yet
    CHECK(p.history.size() == 2 && p.history[0].victory && p.history[0].ordeal == 0 && p.history[1].sanctum);

    // Without a victory no ordeal is open; a profile with no runs has nothing to recover.
    CHECK(profileFromKeyValues({{"runs", "3"}}).ordealUnlocked == 0);
    const Profile fresh = profileFromKeyValues({});
    CHECK(fresh.runs == 0 && fresh.bestKills == 0 && fresh.bestGold == 0 && fresh.history.empty());

    // A current profile keeps what it stored, even where the chronicle says otherwise.
    old["best_kills"] = "5";
    old["best_gold"] = "40";
    old["ordeal_unlocked"] = "0";
    const Profile current = profileFromKeyValues(old);
    CHECK(current.bestKills == 5 && current.bestGold == 40 && current.ordealUnlocked == 0);

    // The chronicle stops at the first damaged entry.
    old["history.1"] = "garbage";
    old["history.2"] = "9|0|1|1|0|0|0|100|3|Perished";
    CHECK(profileFromKeyValues(old).history.size() == 1);
}
