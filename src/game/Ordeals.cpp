#include "game/Ordeals.hpp"

#include <array>

#include "core/Math.hpp"

namespace pm {

namespace {

constexpr std::array<OrdealDef, kMaxOrdeal> kOrdeals = {{
    {"Hardened Foes", "Creatures have a quarter more health."},
    {"Gathering Elites", "Elites are twice as common."},
    {"Lean Sanctums", "Sanctums restore only half the health you have lost."},
    {"Dear Wares", "Shop prices rise by half."},
    {"Cruel Blows", "Creatures strike a quarter harder."},
    {"Restless Mountain", "The mountain stirs twice as often."},
    {"Frail Flesh", "You set out with a quarter less health."},
    {"Wrathful Keepers", "Guardians and the Rebis have 40% more health."},
}};

constexpr const char* kNumerals[] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII"};
static_assert(std::size(kNumerals) == kMaxOrdeal + 1, "a numeral for every ordeal");

} // namespace

const OrdealDef& ordealDef(int level) { return kOrdeals[static_cast<size_t>(clamp(level, 1, kMaxOrdeal) - 1)]; }

std::string ordealLabel(int level) {
    if (level <= 0) return {};
    return std::string("Ordeal ") + kNumerals[clamp(level, 1, kMaxOrdeal)];
}

std::string ordealRules(int level) {
    std::string out;
    for (int i = 1; i <= std::min(level, kMaxOrdeal); ++i) {
        if (!out.empty()) out += ' ';
        out += ordealDef(i).rule;
    }
    return out;
}

} // namespace pm
