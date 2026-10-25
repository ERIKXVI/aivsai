#pragma once
// Run bookkeeping: the suspended-run save behind "Continue the Work" (written at the start
// of every stage) and folding a finished run into the persistent profile.

#include <optional>
#include <string>
#include <vector>

#include "app/Persistence.hpp"
#include "game/Run.hpp"

namespace pm {

/// What a run added to the profile, for the end screen.
struct RunRecord {
    std::string discoveries;                ///< e.g. "2 spells, 1 perk"; empty if nothing new
    std::vector<std::string> unlockedPaths; ///< starting paths unlocked by this run
};

/// Folds a finished or abandoned run into the profile (statistics, Grimoire, chronicle) and
/// saves the profile. `fate` says how the run ended, for the chronicle.
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
