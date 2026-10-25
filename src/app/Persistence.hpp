#pragma once
// Player settings and the persistent profile (statistics across runs), stored as simple
// key=value text files in the per-user data directory.

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

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
    bool highQuality = true; ///< graphics quality (false trims lighting for weak GPUs)
};

struct Profile {
    int runs = 0;
    int victories = 0;
    int deepest = 0;          ///< deepest stage reached (1-based)
    float bestTime = 0.0f;    ///< fastest victory in seconds, 0 = none
    int totalKills = 0;
    int totalGold = 0;
    int lastDailySeed = 0;    ///< seed of the last daily rite attempted
    int bestDailyDepth = 0;
    int chestsOpened = 0;
    // Grimoire: what the alchemist has discovered across all runs.
    uint64_t spellsSeen = 0;          ///< bit i set = SpellId i discovered
    uint32_t perksSeen = 0;           ///< bit i set = PerkId i taken
    std::array<int, 16> creatureKills{}; ///< indexed by EnemyType
};

/// Directory for settings and profile (created on demand). Falls back to the working
/// directory if the platform location is unavailable.
const std::filesystem::path& dataDirectory();

/// Simple key=value files (one pair per line), written atomically.
using KeyValues = std::map<std::string, std::string>;
KeyValues readKeyValues(const std::filesystem::path& path);
void writeKeyValues(const std::filesystem::path& path, const KeyValues& kv);

Settings loadSettings();
void saveSettings(const Settings& s);
Profile loadProfile();
void saveProfile(const Profile& p);

} // namespace pm
