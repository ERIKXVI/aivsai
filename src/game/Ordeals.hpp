#pragma once
// Ordeals: stacking trials for alchemists who have completed the Great Work. Each ordeal keeps
// the rules of every ordeal beneath it and adds one of its own; completing the Work under the
// highest ordeal unlocked opens the next, up to the eighth.

#include <cstdint>
#include <string>

namespace pm {

// Values are ordeal levels: a run under level n is bound by every rule from 1 to n.
enum class Ordeal : uint8_t {
    None,
    HardenedFoes,     ///< creatures have a quarter more health
    GatheringElites,  ///< elites are twice as common
    LeanSanctums,     ///< sanctums restore only half the health lost
    DearWares,        ///< shop prices rise by half
    CruelBlows,       ///< creatures strike a quarter harder
    RestlessMountain, ///< stage events come twice as often
    FrailFlesh,       ///< the alchemist sets out with a quarter less health
    WrathfulKeepers,  ///< guardians and the Rebis have 40% more health
    Count
};

constexpr int kMaxOrdeal = static_cast<int>(Ordeal::Count) - 1;

struct OrdealDef {
    const char* name; ///< "Lean Sanctums"
    const char* rule; ///< "Sanctums restore only half the health you have lost."
};

/// Ordeal level `level` (1..kMaxOrdeal).
const OrdealDef& ordealDef(int level);
/// "Ordeal III", or empty for none.
std::string ordealLabel(int level);
/// Every rule a run under `level` is bound by, one sentence after another.
std::string ordealRules(int level);

} // namespace pm
