#pragma once
// Which point lights get one of the light shader's slots this frame. Every light source
// registers a candidate; the budget keeps the highest-scoring ones (priority first, then
// brightness times reach) and fades persistent lights in and out so that a busy fight
// never makes a lantern or a lava pool pop. Pure CPU code, so it can be unit tested.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "core/Math.hpp"

namespace pm {

struct PointLight {
    Vec2 pos;             ///< world position in cells
    float r = 1, g = 1, b = 1;
    float radius = 64.0f; ///< in cells
    float bodySkip = 0.0f; ///< cells near the light that should not occlude it
    float priority = 0.0f; ///< added to the budget score; the player and the portal use 100
    uint32_t key = 0;      ///< stable identity for fading in and out (0 = not persistent)
};

/// Light categories occupy the top bits of a key, so ids from different sources never collide.
enum class LightKind : uint32_t { Lantern = 1, Emissive = 2, Portal = 3, Transmuter = 4 };

constexpr uint32_t lightKey(LightKind kind, uint32_t id) {
    return (static_cast<uint32_t>(kind) << 28) | (id & 0x0FFFFFFFu);
}

/// Budget score: priority plus brightness weighted by how much of the view the light reaches.
float lightScore(const PointLight& light, float viewDiag);

/// Keeps the `budget` highest-scoring lights, ordered by descending score.
void selectLights(std::vector<PointLight>& lights, int budget, float viewDiag);

/// Applies the budget every frame and remembers a fade value per keyed light.
class LightSelector {
public:
    static constexpr float kFadeRate = 7.0f; ///< fade units per second
    static constexpr int kFadeSlots = 4;     ///< lowest budget slots a fading light may take over

    /// Replaces `lights` (this frame's candidates) with the lights to draw, at most `budget`.
    /// The `budget` best candidates are always drawn, and keyed ones fade toward 1. A keyed
    /// light that was ranked out, or that stopped being registered at all, fades toward 0 and
    /// keeps drawing meanwhile: in a free slot, or in place of one of the lowest kFadeSlots
    /// lights that has nothing to lose (unkeyed or not faded in yet).
    void select(std::vector<PointLight>& lights, int budget, float viewDiag, float dt);
    /// Forgets every fade (a new level is loading).
    void reset() { fades_.clear(); }

    /// Current fade of a key, 0 when it is not tracked.
    float fade(uint32_t key) const;
    bool tracks(uint32_t key) const { return fades_.count(key) != 0; }
    size_t trackedCount() const { return fades_.size(); }

private:
    struct Fade {
        float value = 0.0f;
        uint32_t frame = 0;     ///< last frame the fade moved with its light
        uint32_t seen = 0;      ///< last frame this key was a candidate
        uint32_t drawn = 0;     ///< last frame this key was drawn
        PointLight light;       ///< the light as last registered, drawn while it fades out unregistered
    };
    struct Ranked {
        float score;
        uint32_t index; ///< into this frame's lights
    };
    std::unordered_map<uint32_t, Fade> fades_;
    // Per-frame scratch, kept to avoid allocating every frame.
    std::vector<Ranked> ranked_;
    std::vector<uint8_t> state_;
    std::vector<PointLight> chosen_;
    uint32_t frame_ = 0;
};

} // namespace pm
