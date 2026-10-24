#pragma once
// Player settings and the persistent profile (statistics across runs), stored as simple
// key=value text files in the per-user data directory.

#include <string>

namespace pm {

struct Settings {
    float masterVolume = 0.8f;
    float musicVolume = 0.6f;
    float sfxVolume = 0.85f;
    bool screenShake = true;
    bool fullscreen = false;
    bool showFps = false;
    int difficulty = 1; ///< 0 Apprentice, 1 Adept, 2 Archmage
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
};

/// Directory for settings and profile (created on demand). Falls back to the working
/// directory if the platform location is unavailable.
std::string dataDirectory();

Settings loadSettings();
void saveSettings(const Settings& s);
Profile loadProfile();
void saveProfile(const Profile& p);

} // namespace pm
