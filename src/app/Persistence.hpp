#pragma once
// Player settings and the persistent profile (statistics across runs), stored as simple
// key=value text files in the per-user data directory.

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace pm {

struct Settings {
    float masterVolume = 0.8f;
    float musicVolume = 0.6f;
    float sfxVolume = 0.85f;
    bool screenShake = true;
    bool fullscreen = true;     ///< borderless fullscreen (the first-launch default)
    bool showFps = false;
    int difficulty = 1; ///< 0 Apprentice, 1 Adept, 2 Archmage
    int loadout = 0;    ///< last chosen starting loadout
    bool hints = true;  ///< contextual hints for new players
    bool matterLens = true; ///< name the matter or creature under the resting cursor
    bool highQuality = true; ///< graphics quality (false trims lighting for weak GPUs)
    int ordeal = 0;          ///< ordeal chosen for new runs (capped by the profile's unlocks)
};

/// One finished run, remembered for the Grimoire's chronicle.
struct RunHistoryEntry {
    uint32_t seed = 0;
    int loadout = 0;
    int difficulty = 1;
    int depth = 0;         ///< level index reached (0-based)
    bool sanctum = false;  ///< ...and whether the run ended in the sanctum after it
    bool victory = false;
    bool daily = false;
    int ordeal = 0;        ///< ordeal level the run was under
    float time = 0.0f;     ///< seconds
    int kills = 0;
    std::string fate;      ///< how it ended: "Slain by a Fire Imp", "Abandoned"...
};

struct Profile {
    static constexpr size_t kHistorySize = 10;

    int runs = 0;
    int victories = 0;
    int deepest = 0;          ///< deepest stage reached (1-based)
    float bestTime = 0.0f;    ///< fastest victory in seconds, 0 = none
    int totalKills = 0;
    int totalGold = 0;
    int lastDailySeed = 0;    ///< seed of the last daily rite attempted
    int bestDailyDepth = 0;
    int chestsOpened = 0;
    int bestKills = 0;        ///< most creatures slain in one run
    int bestGold = 0;         ///< most gold gathered in one run (-1: unknown, from an older profile)
    uint32_t featsEarned = 0; ///< bit i set = FeatId i earned
    int ordealUnlocked = 0;   ///< highest ordeal open to new runs (completing the Work opens the next)
    // Grimoire: what the alchemist has discovered across all runs.
    uint64_t spellsSeen = 0;          ///< bit i set = SpellId i discovered
    uint32_t perksSeen = 0;           ///< bit i set = PerkId i taken
    std::array<int, 24> creatureKills{}; ///< indexed by EnemyType (kMaxCreatureTypes)
    std::vector<RunHistoryEntry> history; ///< most recent first, at most kHistorySize
};

/// Directory for settings and profile (created on demand). Falls back to the working
/// directory if the platform location is unavailable.
const std::filesystem::path& dataDirectory();

/// Simple key=value files (one pair per line), written atomically.
using KeyValues = std::map<std::string, std::string>;
KeyValues readKeyValues(const std::filesystem::path& path);
void writeKeyValues(const std::filesystem::path& path, const KeyValues& kv);
/// Deletes a key=value file and any temporary file an interrupted write left beside it.
void removeKeyValues(const std::filesystem::path& path);
/// Strict base-10 integer: the whole string must parse and lie in [lo, hi].
bool parseInteger(const std::string& s, long long& out, long long lo, long long hi);

Settings loadSettings();
void saveSettings(const Settings& s);
Profile loadProfile();
void saveProfile(const Profile& p);

/// The field mapping behind load/save, kept apart from file I/O so migrations of old files can be
/// tested. Missing keys keep their defaults; profiles from older builds are upgraded on the way in.
Settings settingsFromKeyValues(const KeyValues& kv);
KeyValues settingsToKeyValues(const Settings& s);
Profile profileFromKeyValues(const KeyValues& kv);
KeyValues profileToKeyValues(const Profile& p);

/// A chronicle entry as one line of text, and back; parsing rejects malformed lines.
std::string encodeHistoryEntry(const RunHistoryEntry& e);
bool decodeHistoryEntry(const std::string& text, RunHistoryEntry& out);

} // namespace pm
