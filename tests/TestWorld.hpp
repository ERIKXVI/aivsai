#pragma once
// Shared fixtures for headless world tests.

#include <cstring>
#include <type_traits>

#include "sim/World.hpp"

namespace pm::test {

static_assert(std::has_unique_object_representations_v<Cell>, "sameCells() compares Cell bytes; Cell must have no padding");

/// True when both worlds have the same size and byte-identical cells.
inline bool sameCells(const World& a, const World& b) {
    return a.width() == b.width() && a.height() == b.height() &&
           std::memcmp(&a.at(0, 0), &b.at(0, 0),
                       sizeof(Cell) * static_cast<size_t>(a.width()) * static_cast<size_t>(a.height())) == 0;
}

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
