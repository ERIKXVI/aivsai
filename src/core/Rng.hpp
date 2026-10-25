#pragma once
// Deterministic random number generation. `Rng` is a small, fast PCG32 generator used for
// gameplay and generation; `hash*` functions provide stateless randomness that is safe to
// call from any thread (used heavily by the cellular automaton).

#include <cstdint>

#include "core/Math.hpp"

namespace pm {

/// Strong 32-bit integer hash (lowbias32 by Chris Wellons).
constexpr uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

constexpr uint32_t hashCombine(uint32_t a, uint32_t b) {
    return hash32(a ^ (b + 0x9e3779b9U + (a << 6) + (a >> 2)));
}

constexpr uint32_t hash2(int x, int y, uint32_t seed = 0) {
    return hashCombine(hashCombine(seed, static_cast<uint32_t>(x)), static_cast<uint32_t>(y));
}

constexpr uint32_t hash3(int x, int y, int z, uint32_t seed = 0) {
    return hashCombine(hash2(x, y, seed), static_cast<uint32_t>(z));
}

/// Maps a 32-bit hash to [0, 1).
constexpr float hashToUnit(uint32_t h) { return static_cast<float>(h >> 8) * (1.0f / 16777216.0f); }

/// FNV-1a over a string, used to turn text seeds into numbers.
constexpr uint32_t hashString(const char* s) {
    uint32_t h = 2166136261U;
    while (*s) {
        h ^= static_cast<uint8_t>(*s++);
        h *= 16777619U;
    }
    return h;
}

class Rng {
public:
    constexpr Rng() : Rng(0x853c49e6748fea9bULL) {}
    constexpr explicit Rng(uint64_t seed) { reseed(seed); }

    constexpr void reseed(uint64_t seed) {
        state_ = 0;
        inc_ = (seed << 1u) | 1u;
        next();
        state_ += seed ^ 0xda3e39cb94b95bdbULL;
        next();
    }

    constexpr uint32_t next() {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        auto xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        auto rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }

    /// Uniform float in [0, 1).
    constexpr float uniform() { return hashToUnit(next()); }
    constexpr float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    /// Uniform integer in [lo, hi] (inclusive).
    constexpr int rangeInt(int lo, int hi) {
        if (hi <= lo) return lo;
        auto span = static_cast<uint32_t>(hi - lo + 1);
        return lo + static_cast<int>(next() % span);
    }
    constexpr bool chance(float p) { return uniform() < p; }
    constexpr int sign() { return (next() & 1u) ? 1 : -1; }
    float angle() { return uniform() * kTau; }
    Vec2 inCircle(float radius) {
        // Rejection sampling keeps the distribution uniform.
        for (;;) {
            float x = range(-1.0f, 1.0f), y = range(-1.0f, 1.0f);
            if (x * x + y * y <= 1.0f) return {x * radius, y * radius};
        }
    }

    template <typename T, size_t N>
    constexpr const T& pick(const T (&arr)[N]) { return arr[next() % N]; }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 0;
};

/// Tiny xorshift generator for the hot cellular-automaton loop (one per worker / chunk).
struct FastRng {
    uint32_t s;
    constexpr explicit FastRng(uint32_t seed) : s(seed ? seed : 0x9e3779b9U) {}
    constexpr uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    constexpr bool bit() { return (next() & 0x100u) != 0; }
    /// True with probability n/256.
    constexpr bool chance256(uint32_t n) { return (next() & 0xFFu) < n; }
    constexpr int dir() { return bit() ? 1 : -1; }
};

} // namespace pm
