#pragma once
// The Alchemical Codex: every transmutation the world's matter can perform, collected as the alchemist first
// witnesses it on screen (in a run or the Alchemy Sandbox) and listed in the Grimoire. The simulation notes which
// entries fire in each chunk (sim/Codex.hpp); this holds what the codex says about each one.

#include <cstdint>
#include <string>

#include "core/Math.hpp"
#include "sim/Codex.hpp"
#include "sim/Material.hpp"

namespace pm {

class World;

/// A reagent or product: the matter whose colour its swatch shows, and the name it goes by in the formula (the
/// matter's own name unless `label` gives another).
struct CodexTerm {
    MatId mat = mat::Empty; ///< mat::Empty: no term
    const char* label = nullptr;

    bool present() const { return mat != mat::Empty; }
    const char* name() const { return label ? label : matDef(mat).name; }
};

struct CodexDef {
    const char* name;      ///< the alchemical operation, e.g. "Quenching"
    CodexTerm reagents[2]; ///< what goes in; the second may be absent
    CodexTerm products[2]; ///< what comes out; the second may be absent
    const char* riddle;    ///< the hint shown while it is undiscovered
    const char* flavour;   ///< a sentence shown once it is known
};

const CodexDef& codexDef(CodexEntry e);
/// The formula on one line: "Water + Lava -> Steam & Obsidian".
std::string codexFormula(CodexEntry e);

/// How far beyond the edge of the view (in cells) a transmutation still counts as witnessed.
constexpr int kCodexViewMargin = 8;
/// The entries that fired during `world`'s last step in the chunks touching `view` (a rectangle of cells) widened by
/// kCodexViewMargin: what the alchemist could see happen. Read it on the main thread, after the step.
uint32_t codexWitnessed(const World& world, Rectf view);
/// Records the entries in `witnessed`: in `known` (the profile's record) or, for a session that must leave the
/// profile as loaded (bots, captures), in `preview`. Returns the entries new to both, to announce.
uint32_t discoverCodex(uint32_t witnessed, uint32_t& known, uint32_t& preview, bool previewOnly);

} // namespace pm
