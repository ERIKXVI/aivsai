#include "game/Codex.hpp"

#include <iterator>

#include "sim/World.hpp"

namespace pm {

namespace {

// One entry per CodexEntry, in its order. Each formula names what the rule in sim/ChunkUpdater.cpp (or the
// reaction in sim/Material.cpp) really takes and makes; a label stands in for matter of several kinds.
constexpr CodexDef kCodex[] = {
    {"Quenching", {{mat::Water}, {mat::Lava}}, {{mat::Steam}, {mat::Obsidian}}, "Something cools lava into black stone.",
     "Brine quenches lava just as well."},
    {"Meltwater", {{mat::Lava}, {mat::Ice}}, {{mat::Water}}, "A frozen wall weeps where lava touches it.",
     "The lava keeps its heat; the ice gives way."},
    {"Snowmelt", {{mat::Lava}, {mat::Snow}}, {{mat::Water}}, "Molten rock falls on a white drift.",
     "Snow gives way to lava even faster than ice."},
    {"Vitrification", {{mat::Lava}, {mat::Sand}}, {{mat::Glass}}, "Lava bakes a humble powder clear.",
     "Slow work: the lava must rest on the sand a while."},
    {"Searing", {{mat::Lava}, {mat::Blood}}, {{mat::Smoke}}, "Spilled blood meets molten stone.",
     "The lava takes its due and leaves only smoke."},
    {"Fuming", {{mat::Lava}, {mat::Toxic}}, {{mat::ToxicGas}}, "Lava boils a foul sludge.",
     "Worse than before: now you have to breathe it."},
    {"Rendering", {{mat::Lava}, {mat::Slime}}, {{mat::Smoke}}, "Lava meets something that clings.",
     "The sticky stuff hisses away into smoke."},
    {"Solution", {{mat::Water}, {mat::Salt}}, {{mat::Brine}}, "A white powder vanishes into water.",
     "Brine still douses flames and quenches lava."},
    {"Leaching", {{mat::Water}, {mat::SaltRock}}, {{mat::Brine}}, "Water rests against the pale walls of Albedo.",
     "Slowly the walls salt the pool, yet they never wear away."},
    {"Salting", {{mat::Salt}, {mat::Ice}}, {{mat::Water}}, "What melts ice without any fire?",
     "The salt is not used up: a little goes a long way."},
    {"Purification", {{mat::Vitae}, {mat::Toxic}}, {{mat::Water}}, "The draught of life meets a poisoned pool.",
     "Both are spent, and only clean water remains."},
    {"Neutralisation", {{mat::Vitae}, {mat::Acid}}, {{mat::Water}}, "The draught of life calms what burns the skin.",
     "Life and corrosion cancel out into plain water."},
    {"Germination", {{mat::Water}, {mat::Spore}}, {{mat::Fungus}}, "Glowing dust, watered, takes root.",
     "Glowcaps light the deep caverns."},
    {"Amalgamation", {{mat::Quicksilver}, {mat::GoldDust}}, {{mat::Midas}}, "Heavy silver drinks in gold dust.",
     "What it makes turns stone into gold ore."},
    {"Kindling", {{mat::Fire, "Flame"}, {mat::Wood, "Fuel"}}, {{mat::Fire}}, "Heat finds something that will burn.",
     "Lava sets fuel alight as surely as flame."},
    {"Dousing", {{mat::Water}, {mat::Fire, "Flames"}}, {{mat::Steam}}, "Something wet meets something burning.",
     "Brine, blood, Vitae and ichor douse flames too."},
    {"Calcination", {{mat::Wood}, {mat::Fire}}, {{mat::Ash}}, "What remains when wood has burned away?",
     "Coal, moss, flesh, spores and glowcaps leave ash too."},
    {"Detonation", {{mat::Gunpowder}, {mat::Fire}}, {{mat::Fire, "Blast"}}, "Black powder meets a spark.",
     "Powder heaped together goes up in one great blast."},
    {"Corrosion", {{mat::Acid}, {mat::Rock, "Stone"}}, {{mat::ToxicGas}}, "Something green eats through stone.",
     "Glass, metal, gold, runestone and bedrock resist it."},
    {"Parting", {{mat::Acid}, {mat::GoldOre}}, {{mat::GoldDust}}, "Acid finds what the ore is hiding.",
     "Gold resists acid: only the rock around it dissolves."},
    {"Thaw", {{mat::Fire, "Flame"}, {mat::Ice}}, {{mat::Water}}, "Fire is set against the frozen halls.",
     "Snow melts at a touch of flame as well."},
    {"Projection", {{mat::Midas}, {mat::Rock, "Stone"}}, {{mat::GoldOre}}, "Liquid gold touches common stone.",
     "The old masters' dream: base matter made gold."},
    {"Condensation", {{mat::Steam}}, {{mat::Water}}, "What rises as vapour may fall as rain.",
     "Not all of it: some steam simply fades away."},
    {"Withering", {{mat::Vine}}, {{mat::Litter}}, "Cut a vine from the rock it hangs from.",
     "A glowvine crumbles into spores instead."},
};
static_assert(std::size(kCodex) == kCodexCount, "a CodexDef for every CodexEntry");

/// The present terms' names joined by `joint`.
std::string joinTerms(const CodexTerm (&terms)[2], const char* joint) {
    std::string out;
    for (const CodexTerm& t : terms) {
        if (!t.present()) continue;
        if (!out.empty()) out += joint;
        out += t.name();
    }
    return out;
}

} // namespace

const CodexDef& codexDef(CodexEntry e) { return kCodex[static_cast<size_t>(e)]; }

std::string codexFormula(CodexEntry e) {
    const CodexDef& d = codexDef(e);
    return joinTerms(d.reagents, " + ") + " -> " + joinTerms(d.products, " & ");
}

uint32_t codexWitnessed(const World& world, Rectf view) {
    const int x0 = floorToInt(view.x) - kCodexViewMargin, y0 = floorToInt(view.y) - kCodexViewMargin;
    const int x1 = floorToInt(view.x + view.w) + kCodexViewMargin, y1 = floorToInt(view.y + view.h) + kCodexViewMargin;
    return world.codexSeen(x0, y0, x1, y1) & kCodexAll;
}

uint32_t discoverCodex(uint32_t witnessed, uint32_t& known, uint32_t& preview, bool previewOnly) {
    const uint32_t fresh = witnessed & kCodexAll & ~(known | preview);
    (previewOnly ? preview : known) |= fresh;
    return fresh;
}

} // namespace pm
