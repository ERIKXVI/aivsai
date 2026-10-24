#pragma once
// Night sky for levels that open onto the surface: gradient, twinkling stars, the moon and
// two ranges of parallax mountains, drawn behind the world above the terrain skyline.

#include <vector>

namespace pm {

class Sky {
public:
    /// `skyline[x]` is the first ground row of world column x; `surfaceY` the mean horizon.
    /// The layer's top-left texel shows world cell (originX, originY).
    void draw(const std::vector<int>& skyline, int surfaceY, int originX, int originY, int texW, int texH, float time,
              bool emissionPass) const;
};

} // namespace pm
