#pragma once
// Run bookkeeping: the suspended-run save behind "Continue the Work" (written at the start
// of every stage) and folding a finished run into the persistent profile.

#include <optional>
#include <string>
#include <vector>

#include "app/Persistence.hpp"
#include "game/Run.hpp"

namespace pm {

/// A starting path still locked after a run, and how far the run carried it.
struct PathProgress {
    std::string name;        ///< "Geomancer"
    std::string requirement; ///< "Open chests"
    int was = 0, now = 0, target = 0;
};

/// What a run added to the profile, for the end screen.
struct RunRecord {
    std::string discoveries;                ///< e.g. "2 spells, 1 perk"; empty if nothing new
    std::vector<std::string> unlockedPaths; ///< starting paths unlocked by this run
    std::optional<PathProgress> nearestPath; ///< the locked path closest to opening
    std::vector<std::string> records;       ///< personal bests this run set
    int unlockedOrdeal = 0;                 ///< the ordeal this run opened, if any
};

/// The profile's lifetime progress, as the loadout unlock rules read it.
Milestones milestonesOf(const Profile& p);

/// Folds a finished or abandoned run into the profile (statistics, Grimoire, chronicle).
/// `fate` says how the run ended, for the chronicle. Pure: nothing is written to disk.
RunRecord foldRun(Profile& profile, const RunState& run, bool victory, const std::string& fate);
/// foldRun, then saves the profile.
RunRecord recordRun(Profile& profile, const RunState& run, bool victory, const std::string& fate);

/// Where a chronicled run ended: "IV Rubedo", "Sanctum after II" or "Magnum Opus".
std::string depthLabel(const RunHistoryEntry& entry);

/// Serialises a run to `path`; `readRunSave` returns nothing for a missing or malformed file.
void writeRunSave(const RunState& run, const std::filesystem::path& path);
std::optional<RunState> readRunSave(const std::filesystem::path& path);

/// The suspended run lives in the data directory.
void saveSuspendedRun(const RunState& run);
std::optional<RunState> loadSuspendedRun();
void discardSuspendedRun();

} // namespace pm
