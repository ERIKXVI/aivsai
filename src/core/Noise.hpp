#pragma once
// Coherent noise for procedural generation: 2D gradient (Perlin-style) noise, fractal
// Brownian motion, ridged noise and domain warping. All functions are pure and seedable.

#include <cstdint>

namespace pm::noise {

/// Gradient noise in roughly [-1, 1].
float perlin(float x, float y, uint32_t seed);

/// Fractal sum of `octaves` layers of perlin noise, normalized to roughly [-1, 1].
float fbm(float x, float y, uint32_t seed, int octaves, float lacunarity = 2.0f, float gain = 0.5f);

/// Ridged multifractal noise in [0, 1]; high values form thin connected ridges.
float ridged(float x, float y, uint32_t seed, int octaves);

/// fbm sampled at a position displaced by another fbm field (organic, swirly shapes).
float warped(float x, float y, uint32_t seed, int octaves, float warpStrength);

/// Cellular (Worley) noise: distance to the nearest feature point, roughly [0, 1].
float worley(float x, float y, uint32_t seed);

} // namespace pm::noise
