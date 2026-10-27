#include "game/RouteGuide.hpp"

#include <algorithm>
#include <limits>

namespace pm {

void RouteGuide::reset(Vec2 start, const std::vector<Vec2>& path, Vec2 exit) {
    pts_.clear();
    arc_.clear();
    progress_ = 0.0f;
    auto add = [&](Vec2 p) {
        if (!pts_.empty() && (p - pts_.back()).lengthSq() < 1.0f) return;
        arc_.push_back(pts_.empty() ? 0.0f : arc_.back() + (p - pts_.back()).length());
        pts_.push_back(p);
    };
    add(start);
    for (Vec2 p : path) add(p);
    add(exit);
}

RouteGuide::Hit RouteGuide::nearest(Vec2 pos, float fromArc) const {
    Hit best{std::numeric_limits<float>::max(), fromArc};
    for (size_t i = 0; i + 1 < pts_.size(); ++i) {
        const float segLen = arc_[i + 1] - arc_[i];
        if (arc_[i + 1] < fromArc || segLen <= 0.0f) continue;
        const Vec2 a = pts_[i], ab = pts_[i + 1] - a;
        // Project onto the segment, but never onto the part behind `fromArc`.
        const float tMin = std::max(0.0f, (fromArc - arc_[i]) / segLen);
        const float t = std::clamp(dot(pos - a, ab) / (segLen * segLen), tMin, 1.0f);
        const float d = (a + ab * t - pos).length();
        if (d < best.dist) best = {d, arc_[i] + segLen * t};
    }
    return best;
}

Vec2 RouteGuide::pointAt(float arc) const {
    if (arc >= arc_.back()) return pts_.back();
    const size_t i = static_cast<size_t>(std::upper_bound(arc_.begin(), arc_.end(), arc) - arc_.begin());
    if (i == 0) return pts_.front();
    const float t = (arc - arc_[i - 1]) / (arc_[i] - arc_[i - 1]);
    return lerp(pts_[i - 1], pts_[i], t);
}

void RouteGuide::update(Vec2 pos) {
    if (pts_.size() < 2) return;
    const Hit h = nearest(pos, 0.0f);
    if (h.dist < kOnRoute) progress_ = std::max(progress_, h.arc);
}

Vec2 RouteGuide::target(Vec2 pos) const {
    if (pts_.size() < 2) return pts_.empty() ? pos : pts_.back();
    const Hit h = nearest(pos, progress_);
    return pointAt(h.arc + (h.dist < kOnRoute ? kLookahead : kRejoinLead));
}

} // namespace pm
