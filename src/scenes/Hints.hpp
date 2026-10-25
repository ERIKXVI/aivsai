#pragma once
// Contextual hints: short prompts that teach a mechanic the first time it becomes
// relevant (liquid nearby, a new spell, low health...). Each hint is shown once per launch
// of the game, and they can be switched off in the settings.

#include <cstdint>

namespace pm {

class Level;
struct RunState;

// Values are bit positions in the seen-mask. WandGuide is not a banner: it marks the wand
// editor's guided first drop as done, and its text captions that guide.
enum class Hint : uint8_t { Absorb, Satchel, Heal, Sanctum, Transmuter, Compass, Gold, Burning, EditWands, WandGuide, Lantern, Lair, Elixir, Count };

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
    float timer_ = 0.0f;
    float scanTimer_ = 0.0f;
};

} // namespace pm
