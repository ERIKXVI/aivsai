#include "render/LightBudget.hpp"

#include <algorithm>
#include <iterator>

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
    std::partial_sort(lights.begin(), lights.begin() + static_cast<long>(keep), lights.end(),
                      [viewDiag](const PointLight& a, const PointLight& b) { return lightScore(a, viewDiag) > lightScore(b, viewDiag); });
    lights.resize(keep);
}

float LightSelector::fade(uint32_t key) const {
    auto it = fades_.find(key);
    return it == fades_.end() ? 0.0f : it->second.value;
}

void LightSelector::select(std::vector<PointLight>& lights, int budget, float viewDiag, float dt) {
    ++frame_;
    budget = std::max(0, budget);
    const float step = kFadeRate * std::max(0.0f, dt);
    selectLights(lights, static_cast<int>(lights.size()), viewDiag); // rank every candidate

    // The best lights take all but a few slots outright; lights still fading out get the
    // rest before anything ranked lower, so a dropped lantern dims instead of vanishing.
    const int n = static_cast<int>(lights.size());
    const int fresh = std::min(n, std::max(0, budget - kFadeSlots));
    std::vector<uint8_t> state(static_cast<size_t>(n), kSkipped);
    int used = 0;
    for (; used < fresh; ++used) state[static_cast<size_t>(used)] = kSelected;
    for (int i = fresh; i < n && used < budget; ++i) {
        const PointLight& l = lights[static_cast<size_t>(i)];
        if (l.key != 0 && fade(l.key) > step) {
            state[static_cast<size_t>(i)] = kFading;
            ++used;
        }
    }
    for (int i = fresh; i < n && used < budget; ++i) {
        if (state[static_cast<size_t>(i)] != kSkipped) continue;
        state[static_cast<size_t>(i)] = kSelected;
        ++used;
    }

    chosen_.clear();
    for (int i = 0; i < n; ++i) {
        if (state[static_cast<size_t>(i)] == kSkipped) continue;
        PointLight l = lights[static_cast<size_t>(i)];
        if (l.key != 0) {
            Fade& f = fades_[l.key];
            if (f.frame != frame_) { // a duplicate key shares the first one's fade
                f.value = approach(f.value, state[static_cast<size_t>(i)] == kSelected ? 1.0f : 0.0f, step);
                f.frame = frame_;
            }
            if (f.value <= 0.0f) continue;
            l.r *= f.value;
            l.g *= f.value;
            l.b *= f.value;
        }
        chosen_.push_back(l);
    }

    // Lights that lost their slot or left the view fade out unseen; spent fades are dropped.
    const bool overfull = fades_.size() > kMaxTracked;
    for (auto it = fades_.begin(); it != fades_.end();) {
        Fade& f = it->second;
        if (f.frame != frame_) f.value = overfull ? 0.0f : approach(f.value, 0.0f, step);
        it = f.value <= 0.0f ? fades_.erase(it) : std::next(it);
    }
    lights.swap(chosen_);
}

} // namespace pm
