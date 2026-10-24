#include "core/Profiler.hpp"

#include <algorithm>

namespace pm {

Profiler& Profiler::instance() {
    static Profiler p;
    return p;
}

void Profiler::add(ProfileSection s, double ms) {
    auto i = static_cast<size_t>(s);
    avg_[i] = avg_[i] * 0.95 + ms * 0.05;
    peak_[i] = std::max(peak_[i] * 0.995, ms);
}

const char* Profiler::name(ProfileSection s) {
    static constexpr const char* kNames[] = {"tick", "  cells", "  entities", "  particles", "world layer", "upload", "render"};
    return kNames[static_cast<size_t>(s)];
}

} // namespace pm
