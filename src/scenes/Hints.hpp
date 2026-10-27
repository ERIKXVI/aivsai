#pragma once
// Contextual hints: short prompts that teach a mechanic the first time it becomes
// relevant (liquid nearby, a new spell, low health...). Each hint is shown once per launch
// of the game, and they can be switched off in the settings.

#include <cstdint>

#include "game/Spells.hpp"

namespace pm {

class Level;
struct RunState;

/// A satchel spell worth guiding the first wand edit with: an attack spell safe in the wand in
/// hand, or a modifier or multicast to put in front of one (never the kit's bomb).
bool guideCandidate(SpellId s);

/// The wand editor's guided first edit: which satchel spell to lift and where it goes.
struct WandGuide {
    int from = -1;        ///< satchel slot of the spell; -1 when none is worth guiding with
    int wand = -1;        ///< destination wand
    int slot = -1;        ///< an empty slot for an attack spell; for a booster, the attack spell it goes in front of
    bool booster = false; ///< the spell is a modifier or multicast
    bool active() const { return from >= 0 && slot >= 0; }
};
/// The first guideCandidate in the satchel, onto the wand whose first cast hits hardest (direct damage;
/// ties keep wand order) that has room. A booster needs an attack spell with an empty slot after it,
/// and never goes on a shuffle wand.
WandGuide pickWandGuide(const RunState& run);

// Values are bit positions in the seen-mask. WandGuide is not a banner: it marks the wand
// editor's guided first drop as done, and its text captions that guide.
enum class Hint : uint8_t { Absorb, Satchel, Heal, Sanctum, Transmuter, Compass, Gold, Burning, EditWands, WandGuide, Lantern, Lair, Elixir, Count };
static_assert(static_cast<unsigned>(Hint::Count) <= 32, "hints are kept in a 32-bit seen-mask");

class HintTracker {
public:
    /// `seen` is the bitmask of hints already shown; it is updated in place.
    explicit HintTracker(uint32_t& seen) : seen_(seen) {}

    /// Call once per tick while playing; picks a hint when its trigger fires.
    void update(const Level& level, const RunState& run, float dt);
    /// Draws the active hint (screen space, above the hotbar).
    void draw(float screenW, float screenH, bool gamepad) const;
    /// Forget the current hint (level change).
    void reset();

    bool seen(Hint h) const { return (seen_ >> static_cast<unsigned>(h)) & 1u; }
    /// Records a lesson as learnt without showing it (e.g. the player found the editor alone).
    void markSeen(Hint h) {
        seen_ |= 1u << static_cast<unsigned>(h);
        if (active_ == h) active_ = Hint::Count;
    }
    static const char* text(Hint h, bool gamepad);

private:
    void show(Hint h);

    uint32_t& seen_;
    Hint active_ = Hint::Count;
    bool leanSanctum_ = false; ///< the sanctum hint is for a font that holds only water (Lean Sanctums)
    float timer_ = 0.0f;
    float scanTimer_ = 0.0f;
};

} // namespace pm
