#pragma once
// Shared fixtures for headless world tests.

#include "sim/World.hpp"

namespace pm::test {

/// Cells of material `m` anywhere in the world.
inline int countMat(const World& w, MatId m) {
    int n = 0;
    for (int y = 0; y < w.height(); ++y)
        for (int x = 0; x < w.width(); ++x)
            if (w.at(x, y).mat == m) ++n;
    return n;
}

/// Seals the world in a one-cell bedrock box.
inline void buildBox(World& w) {
    for (int x = 0; x < w.width(); ++x) {
        w.place(x, w.height() - 1, mat::Bedrock);
        w.place(x, 0, mat::Bedrock);
    }
    for (int y = 0; y < w.height(); ++y) {
        w.place(0, y, mat::Bedrock);
        w.place(w.width() - 1, y, mat::Bedrock);
    }
}

} // namespace pm::test
