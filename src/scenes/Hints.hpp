#pragma once
// Contextual hints: short prompts that teach a mechanic the first time it becomes
// relevant (liquid nearby, a new spell, low health...). Each hint is shown once per
// profile; the seen-set lives in the profile so returning players are left alone.

#include <cstdint>

namespace pm {

class Level;
struct RunState;

// Values are bit positions in the saved profile: append new hints, never reorder.
enum class Hint : uint8_t { Absorb, Satchel, Heal, Sanctum, Transmuter, Compass, Gold, Burning, Count };

class HintTracker {
public:
    /// `seen` is the profile's bitmask of hints already shown; it is updated in place.
    explicit HintTracker(uint32_t& seen) : seen_(seen) {}

    /// Call once per tick while playing; picks a hint when its trigger fires.
    void update(const Level& level, const RunState& run, float dt);
    /// Draws the active hint (screen space, above the hotbar).
    void draw(float screenW, float screenH, bool gamepad) const;
    /// Forget the current hint (level change).
    void reset();

private:
    bool seen(Hint h) const { return (seen_ >> static_cast<unsigned>(h)) & 1u; }
    void show(Hint h);

    uint32_t& seen_;
    Hint active_ = Hint::Count;
    float timer_ = 0.0f;
    float scanTimer_ = 0.0f;
    int baselineSpells_ = -1; ///< satchel size when the level began
};

} // namespace pm
