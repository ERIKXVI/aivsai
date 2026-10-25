#include "render/LightBudget.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace pm {

namespace {

constexpr size_t kMaxTracked = 256; ///< hard cap on remembered fades, should time ever stand still

enum : uint8_t { kSkipped, kSelected, kFading };

} // namespace

float lightScore(const PointLight& light, float viewDiag) {
    const float diag = std::max(viewDiag, 1.0f);
    return light.priority + std::max({light.r, light.g, light.b}) * std::min(light.radius, diag) / diag;
}

void selectLights(std::vector<PointLight>& lights, int budget, float viewDiag) {
    const size_t keep = std::min(lights.size(), static_cast<size_t>(std::max(0, budget)));
    std::vector<std::pair<float, size_t>> ranked;
    ranked.reserve(lights.size());
    for (size_t i = 0; i < lights.size(); ++i) ranked.emplace_back(lightScore(lights[i], viewDiag), i);
    std::partial_sort(ranked.begin(), ranked.begin() + static_cast<long>(keep), ranked.end(),
                      [](const auto& a, const auto& b) { return a.first > b.first || (a.first == b.first && a.second < b.second); });
    std::vector<PointLight> out;
    out.reserve(keep);
    for (size_t i = 0; i < keep; ++i) out.push_back(lights[ranked[i].second]);
    lights.swap(out);
}

float LightSelector::fade(uint32_t key) const {
    auto it = fades_.find(key);
    return it == fades_.end() ? 0.0f : it->second.value;
}

void LightSelector::select(std::vector<PointLight>& lights, int budget, float viewDiag, const Rectf& area, float dt) {
    ++frame_;
    budget = std::max(0, budget);
    const float step = kFadeRate * std::max(0.0f, dt);

    // A keyed light drawn last frame that is no longer registered (it left the view, shattered,
    // or its lava cluster was outranked) comes back as it was last seen, to fade out in place,
    // unless it no longer reaches the view: then it would only take a visible light's slot.
    for (const PointLight& l : lights) {
        if (l.key == 0) continue;
        auto it = fades_.find(l.key);
        if (it != fades_.end()) it->second.seen = frame_;
    }
    const size_t registered = lights.size();
    for (const auto& [key, f] : fades_)
        if (f.seen != frame_ && f.drawn + 1 == frame_ && f.value > step && lightReaches(f.light, area)) lights.push_back(f.light);

    ranked_.clear();
    for (size_t i = 0; i < lights.size(); ++i) ranked_.push_back({lightScore(lights[i], viewDiag), static_cast<uint32_t>(i)});
    std::sort(ranked_.begin(), ranked_.end(),
              [](const Ranked& a, const Ranked& b) { return a.score > b.score || (a.score == b.score && a.index < b.index); });
    const int n = static_cast<int>(ranked_.size());
    auto lightAt = [&](int rank) -> const PointLight& { return lights[ranked_[static_cast<size_t>(rank)].index]; };
    auto unregistered = [&](int rank) { return ranked_[static_cast<size_t>(rank)].index >= registered; };

    // The best `budget` registered lights are always drawn and fade toward full brightness.
    state_.assign(static_cast<size_t>(n), kSkipped);
    int used = 0, lowest = -1;
    for (int i = 0; i < n && used < budget; ++i) {
        if (unregistered(i)) continue;
        state_[static_cast<size_t>(i)] = kSelected;
        lowest = i;
        ++used;
    }

    // Lights fading out keep drawing: in a free slot, else in place of one of the lowest
    // kFadeSlots selected lights, provided it is unkeyed or has not faded in yet. A keyed light
    // that holds its slot is never displaced, so it cannot flicker.
    int cursor = lowest, slotsLeft = kFadeSlots;
    auto takeSlot = [&]() {
        if (used < budget) {
            ++used;
            return true;
        }
        for (; cursor >= 0 && slotsLeft > 0; --cursor) {
            if (state_[static_cast<size_t>(cursor)] != kSelected) continue;
            --slotsLeft;
            const PointLight& l = lightAt(cursor);
            if (l.key != 0 && fade(l.key) > 0.0f) continue;
            state_[static_cast<size_t>(cursor--)] = kSkipped;
            return true;
        }
        return false;
    };
    for (int i = 0; i < n; ++i) {
        const PointLight& l = lightAt(i);
        if (state_[static_cast<size_t>(i)] != kSkipped || l.key == 0 || fade(l.key) <= step) continue;
        if (!takeSlot()) break;
        state_[static_cast<size_t>(i)] = kFading;
    }

    chosen_.clear();
    for (int i = 0; i < n; ++i) {
        const uint8_t state = state_[static_cast<size_t>(i)];
        if (state == kSkipped) continue;
        PointLight l = lightAt(i);
        if (l.key != 0) {
            Fade& f = fades_[l.key];
            if (f.frame != frame_) { // a duplicate key shares the first one's fade
                f.value = approach(f.value, state == kSelected ? 1.0f : 0.0f, step);
                f.frame = frame_;
                if (!unregistered(i)) f.light = l;
            }
            if (f.value <= 0.0f) continue;
            f.drawn = frame_;
            l.r *= f.value;
            l.g *= f.value;
            l.b *= f.value;
        }
        chosen_.push_back(l);
    }

    // Lights that lost their slot or left the view without one fade out unseen; spent fades are
    // dropped.
    const bool overfull = fades_.size() > kMaxTracked;
    for (auto it = fades_.begin(); it != fades_.end();) {
        Fade& f = it->second;
        if (f.frame != frame_) f.value = overfull ? 0.0f : approach(f.value, 0.0f, step);
        it = f.value <= 0.0f ? fades_.erase(it) : std::next(it);
    }
    lights.swap(chosen_);
}

} // namespace pm
