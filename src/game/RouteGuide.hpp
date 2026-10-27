#pragma once
// Where the compass should point: a little way ahead along the stage's guaranteed route,
// rather than straight at the portal through hundreds of cells of rock.

#include <vector>

#include "core/Math.hpp"

namespace pm {

class RouteGuide {
public:
    /// Rebuilds the route for a new stage. The generator's main path starts at the cave mouth on
    /// the surface stage and ends where the exit stood before its platform settled it a few cells
    /// lower, so the spawn point and the final exit position are added at either end.
    void reset(Vec2 start, const std::vector<Vec2>& path, Vec2 exit);
    /// Records how far along the route the alchemist has come (never goes back). Once per tick.
    void update(Vec2 pos);
    /// The point the compass aims at: a stretch further along the route, or, for an alchemist who
    /// has strayed from it, the nearest route point that is not behind them.
    Vec2 target(Vec2 pos) const;

    float progress() const { return progress_; }
    /// The route's whole length, from spawn to portal (0 before the first reset).
    float length() const { return arc_.empty() ? 0.0f : arc_.back(); }

    static constexpr float kOnRoute = 48.0f;   ///< cells from the route's skeleton that still count as on it
    static constexpr float kLookahead = 64.0f; ///< how far ahead along the route the arrow aims
    static constexpr float kRejoinLead = 16.0f; ///< aim just past the rejoin point when off the route

private:
    struct Hit {
        float dist;
        float arc;
    };
    /// Nearest point on the route at or beyond arc length `fromArc`.
    Hit nearest(Vec2 pos, float fromArc) const;
    Vec2 pointAt(float arc) const;

    std::vector<Vec2> pts_;
    std::vector<float> arc_; ///< cumulative length at each point
    float progress_ = 0.0f;
};

} // namespace pm
