#pragma once
// Starting loadouts: alternative opening kits unlocked by long-term milestones, giving
// each run a different first few minutes.

#include <cstdint>
#include <string>

namespace pm {

struct RunState;

enum class LoadoutId : uint8_t { Apprentice, Pyromancer, Geomancer, Stormcaller, Hermeticist, Count };

constexpr int kLoadoutCount = static_cast<int>(LoadoutId::Count);

/// Lifetime progress that unlocks loadouts (filled from the player's profile).
struct Milestones {
    int deepest = 0;
    int victories = 0;
    int chestsOpened = 0;
    int totalKills = 0;
};

struct LoadoutDef {
    const char* name;
    const char* description;
    int signatureWand; ///< index of the wand that characterises the kit (shown as a preview)
    bool randomKit;    ///< the signature wand is rolled per run, so it cannot be previewed
};

const LoadoutDef& loadoutDef(LoadoutId id);
bool loadoutUnlocked(LoadoutId id, const Milestones& m);
/// Unlock requirement with current progress, e.g. "Open 12 chests (5/12)".
std::string loadoutRequirement(LoadoutId id, const Milestones& m);
/// Replaces the default kit of a fresh run with the loadout's wands, spells, flask and perks.
void equipLoadout(RunState& run, LoadoutId id);

} // namespace pm
