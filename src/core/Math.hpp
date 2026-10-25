#pragma once
// Small math helpers shared by every module. Kept header-only and constexpr-friendly.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pm {

constexpr float kPi = 3.14159265358979f;
constexpr float kTau = 6.28318530717958f;

struct Vec2 {
    float x = 0.0f, y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    constexpr Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    constexpr Vec2& operator*=(float s) { x *= s; y *= s; return *this; }

    float length() const { return std::sqrt(x * x + y * y); }
    constexpr float lengthSq() const { return x * x + y * y; }
    Vec2 normalized() const {
        float l = length();
        return l > 1e-6f ? Vec2{x / l, y / l} : Vec2{0.0f, 0.0f};
    }
};

constexpr float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline Vec2 fromAngle(float a, float len = 1.0f) { return {std::cos(a) * len, std::sin(a) * len}; }
inline float angleOf(Vec2 v) { return std::atan2(v.y, v.x); }
inline Vec2 rotate(Vec2 v, float a) {
    float c = std::cos(a), s = std::sin(a);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

struct Vec2i {
    int x = 0, y = 0;
    constexpr bool operator==(const Vec2i&) const = default;
};

struct Recti {
    int x = 0, y = 0, w = 0, h = 0;
    constexpr bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

struct Rectf {
    float x = 0, y = 0, w = 0, h = 0;
    constexpr bool overlaps(const Rectf& o) const {
        return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h;
    }
    constexpr bool contains(Vec2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
    constexpr Vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
};

template <typename T>
constexpr T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

constexpr float lerp(float a, float b, float t) { return a + (b - a) * t; }
constexpr Vec2 lerp(Vec2 a, Vec2 b, float t) { return {lerp(a.x, b.x, t), lerp(a.y, b.y, t)}; }

constexpr float saturate(float v) { return clamp(v, 0.0f, 1.0f); }

constexpr float smoothstep(float e0, float e1, float x) {
    float t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

/// Frame-rate independent exponential approach towards `target`: the remaining gap shrinks by a
/// factor of exp(-sharpness) per second (exp(-sharpness * dt) per call). Higher is snappier.
inline float damp(float current, float target, float sharpness, float dt) {
    return lerp(target, current, std::exp(-sharpness * dt));
}
inline Vec2 damp(Vec2 current, Vec2 target, float sharpness, float dt) {
    float k = std::exp(-sharpness * dt);
    return lerp(target, current, k);
}

/// Moves `current` towards `target` by at most `maxDelta`.
constexpr float approach(float current, float target, float maxDelta) {
    if (current < target) return current + maxDelta > target ? target : current + maxDelta;
    return current - maxDelta < target ? target : current - maxDelta;
}

inline int floorToInt(float v) { return static_cast<int>(std::floor(v)); }
inline int roundToInt(float v) { return static_cast<int>(std::lround(v)); }

constexpr int floorDiv(int a, int b) { return (a >= 0) ? a / b : ((a - b + 1) / b); }
constexpr int positiveMod(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }

/// Wraps an angle into [-pi, pi].
inline float wrapAngle(float a) {
    while (a > kPi) a -= kTau;
    while (a < -kPi) a += kTau;
    return a;
}

} // namespace pm
