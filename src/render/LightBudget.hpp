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
    static constexpr int kFadeSlots = 4;     ///< slots held back for lights fading out

    /// Replaces `lights` (this frame's candidates) with the lights to draw, at most `budget`.
    /// Keyed lights fade toward 1 while selected and toward 0 once dropped; a dropped light
    /// keeps a slot until it has faded out.
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
        uint32_t frame = 0; ///< last frame this key was a candidate
    };
    std::unordered_map<uint32_t, Fade> fades_;
    std::vector<PointLight> chosen_;
    uint32_t frame_ = 0;
};

} // namespace pm
