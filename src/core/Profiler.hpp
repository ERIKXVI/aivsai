#pragma once
// Minimal frame profiler: named sections accumulate smoothed millisecond timings that can be
// shown in the debug overlay (F3) or printed by headless test runs.

#include <array>
#include <chrono>

namespace pm {

enum class ProfileSection { Tick, Simulation, Entities, Particles, WorldBuild, Upload, Render, Count };

class Profiler {
public:
    static Profiler& instance();

    void add(ProfileSection s, double ms);
    /// Exponentially smoothed milliseconds for a section.
    double average(ProfileSection s) const { return avg_[static_cast<size_t>(s)]; }
    double peak(ProfileSection s) const { return peak_[static_cast<size_t>(s)]; }
    static const char* name(ProfileSection s);

private:
    std::array<double, static_cast<size_t>(ProfileSection::Count)> avg_{};
    std::array<double, static_cast<size_t>(ProfileSection::Count)> peak_{};
};

/// RAII timer adding its lifetime to a profiler section.
class ScopedTimer {
public:
    explicit ScopedTimer(ProfileSection s) : section_(s), start_(std::chrono::steady_clock::now()) {}
    ~ScopedTimer() {
        auto end = std::chrono::steady_clock::now();
        Profiler::instance().add(section_, std::chrono::duration<double, std::milli>(end - start_).count());
    }
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    ProfileSection section_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace pm
