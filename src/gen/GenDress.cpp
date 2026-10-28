// Level generation, step two: painting materials and furnishing the carved caves.

#include "gen/GeneratorImpl.hpp"

#include <algorithm>
#include <cmath>

#include "core/Noise.hpp"
#include "core/ThreadPool.hpp"
#include "sim/World.hpp"

namespace pm::gen_detail {

void Generator::placeArenaPlatforms(World& world) {
    if (arena_.w <= 0.0f) return;
    // Floating obsidian ledges give footing while dodging the Rebis.
    const int count = 6;
    for (int i = 0; i < count; ++i) {
        float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
        int px = static_cast<int>(arena_.x - arena_.w * 0.75f + t * arena_.w * 1.5f);
        int py = static_cast<int>(arena_.y + ((i % 2) ? 20.0f : -15.0f));
        int half = rng_.rangeInt(9, 14);
        for (int x = px - half; x <= px + half; ++x)
            for (int y = py; y < py + 3; ++y)
                if (world.inBounds(x, y) && world.at(x, y).mat == mat::Empty)
                    world.at(x, y) = makeCell(y == py ? mat::Rune : mat::Obsidian, hash2(x, y, seed_));
    }
}

void Generator::paintMaterials(World& world, ThreadPool& pool) {
    // Dilated air mask: solids within 2 cells of air get the biome's surface skin.
    std::vector<uint8_t> nearAir(open_.size(), 0);
    pool.parallelFor(h_, [&](int y) {
        for (int x = 0; x < w_; ++x) {
            bool near = false;
            for (int dy = -2; dy <= 2 && !near; ++dy)
                for (int dx = -2; dx <= 2 && !near; ++dx) near = isOpen(x + dx, y + dy);
            nearAir[idx(x, y)] = near ? 1 : 0;
        }
    });

    const uint32_t s = seed_;
    pool.parallelFor(h_, [&](int y) {
        for (int x = 0; x < w_; ++x) {
            Cell& c = world.at(x, y);
            if (open_[idx(x, y)]) {
                c = Cell{};
                continue;
            }
            const int edge = std::min(std::min(x, w_ - 1 - x), std::min(y, h_ - 1 - y));
            MatId m;
            if (edge < 3) {
                m = mat::Bedrock;
            } else if (edge < 10 + static_cast<int>(noise::perlin(x * 0.1f, y * 0.1f, s) * 4.0f)) {
                m = b_.hardRock;
            } else {
                float strata = noise::fbm(x * 0.005f, y * 0.03f, s ^ 0x9u, 3);
                m = strata > 0.22f ? b_.rockAlt : b_.rock;
                for (int k = 0; k < static_cast<int>(b_.veins.size()); ++k) {
                    const VeinDef& v = b_.veins[k];
                    if (v.mat == mat::Empty) continue;
                    const float threshold = v.threshold - (omen_ == Omen::Gilded && v.mat == mat::GoldOre ? 0.06f : 0.0f);
                    float n = noise::fbm(x * v.scale, y * v.scale, s + 101u * static_cast<uint32_t>(k + 1), 2);
                    if (n * 0.5f + 0.5f > threshold) {
                        m = v.mat;
                        break;
                    }
                }
                if (nearAir[idx(x, y)] && (m == b_.rock || m == b_.rockAlt) &&
                    noise::perlin(x * 0.05f, y * 0.05f, s ^ 0x77u) > -0.15f) {
                    m = b_.surface;
                }
            }
            // Coherent shading plus darker fracture lines give rock a hand-painted texture.
            float tex = noise::fbm(x * 0.09f, y * 0.09f, s ^ 0x3131u, 3);
            float cracks = noise::ridged(x * 0.045f, y * 0.06f, s ^ 0x9191u, 2);
            int var = 128 + static_cast<int>(tex * 125.0f) - static_cast<int>(smoothstep(0.8f, 0.97f, cracks) * 95.0f) +
                      static_cast<int>(hash2(x, y, s) & 31u) - 16;
            if (phaseOf(m) == Phase::Powder) var = static_cast<int>(hash2(x, y, s) & 255u);
            c = makeCell(m, hash2(x, y, s ^ 0xABCDu));
            c.var = static_cast<uint8_t>(clamp(var, 0, 255));
        }
    });
}

bool Generator::isFloor(const World& w, int x, int y) const {
    // An open cell directly above a solid one.
    return w.inBounds(x, y + 1) && w.at(x, y).mat == mat::Empty && isSolidForCollision(w.at(x, y + 1).mat);
}

void Generator::placeVegetation(World& world) {
    if (vegetation() <= 0.0f) return;
    for (int y = kBorder + 1; y < h_ - kBorder - 1; ++y) {
        for (int x = kBorder + 1; x < w_ - kBorder - 1; ++x) {
            if (!open_[idx(x, y)]) continue;
            MatId below = world.at(x, y + 1).mat;
            MatId above = world.at(x, y - 1).mat;
            float n = noise::perlin(x * 0.04f, y * 0.04f, seed_ ^ 0xC0FEu);
            // Moss carpets on floors.
            if (isSolidForCollision(below) && phaseOf(below) == Phase::Solid && n > 0.35f - vegetation() * 0.4f) {
                int thickness = 1 + static_cast<int>(hash2(x, y, seed_) % 3u);
                for (int k = 1; k <= thickness; ++k) {
                    if (world.inBounds(x, y + k) && phaseOf(world.at(x, y + k).mat) == Phase::Solid &&
                        isSolidForCollision(world.at(x, y + k).mat) && world.at(x, y + k).mat != mat::Bedrock)
                        world.at(x, y + k) = makeCell(mat::Moss, hash2(x, y + k, seed_));
                }
            }
            // Hanging strands from ceilings.
            if (isSolidForCollision(above) && phaseOf(above) == Phase::Solid && above != mat::Bedrock &&
                (hash2(x, y, seed_ ^ 7u) % 1000u) < static_cast<uint32_t>(vegetation() * 70.0f)) {
                int len = 3 + static_cast<int>(hash2(x, y, seed_ ^ 9u) % 14u);
                MatId strand = (biome_ == BiomeId::Citrinitas || hash2(x, y, 3u) % 4u == 0) ? mat::GlowVine : mat::Vine;
                for (int k = 0; k < len && world.inBounds(x, y + k) && world.at(x, y + k).mat == mat::Empty; ++k)
                    world.at(x, y + k) = makeCell(strand, hash2(x, y + k, seed_));
            }
        }
    }
    // Glowcap mushrooms on floors.
    int shrooms = static_cast<int>(40 * vegetation());
    for (int i = 0; i < shrooms; ++i) {
        int x = rng_.rangeInt(10, w_ - 10), y = rng_.rangeInt(10, h_ - 10);
        for (int k = 0; k < 40 && y < h_ - 10; ++k, ++y) {
            if (isFloor(world, x, y)) {
                placeMushroom(world, x, y);
                break;
            }
        }
    }
}

void Generator::placeTrees(World& world, LevelLayout& out) {
    const float entranceX = path_.front().x;
    for (int x = 20; x < w_ - 20; x += rng_.rangeInt(26, 60)) {
        if (std::fabs(static_cast<float>(x) - entranceX) < 30.0f || std::fabs(static_cast<float>(x) - start_.x) < 14.0f) continue;
        const int ground = skyline_[static_cast<size_t>(x)];
        if (!isFloor(world, x, ground - 1)) continue;
        TreeShape tree;
        tree.rootX = x;
        tree.rootY = ground - 1;
        auto wood = [&](int wx, int wy) {
            if (world.placeIfEmpty(wx, wy, mat::Wood)) tree.cells.push_back(static_cast<uint32_t>(idx(wx, wy)));
        };
        const int height = rng_.rangeInt(16, 34);
        const int top = ground - height;
        // Trunk, slightly leaning, with a couple of branches.
        float lean = rng_.range(-0.15f, 0.15f);
        for (int y = ground - 1; y >= top; --y) {
            int tx = x + static_cast<int>(lean * static_cast<float>(ground - y));
            wood(tx, y);
            if (y > top + height / 2) wood(tx + 1, y);
        }
        for (int b = 0; b < 2; ++b) {
            int by = top + rng_.rangeInt(3, height / 2);
            int dir = rng_.sign();
            int bx = x + static_cast<int>(lean * static_cast<float>(ground - by));
            for (int k = 1; k < rng_.rangeInt(4, 8); ++k) wood(bx + dir * k, by - k / 2);
        }
        // Canopy of overlapping leaf blobs.
        const int topX = x + static_cast<int>(lean * static_cast<float>(height));
        for (int blob = 0; blob < 4; ++blob) {
            int cx = topX + rng_.rangeInt(-7, 7), cy = top + rng_.rangeInt(-4, 5);
            int r = rng_.rangeInt(5, 9);
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx)
                    if (dx * dx + dy * dy * 2 <= r * r && hash2(cx + dx, cy + dy, seed_) % 7u != 0u) {
                        Cell c = makeCell(mat::Leaves, hash2(cx + dx, cy + dy, seed_ ^ 3u));
                        if (world.inBounds(cx + dx, cy + dy) && world.at(cx + dx, cy + dy).mat == mat::Empty) {
                            world.at(cx + dx, cy + dy) = c;
                            tree.cells.push_back(static_cast<uint32_t>(idx(cx + dx, cy + dy)));
                        }
                    }
        }
        std::sort(tree.cells.begin(), tree.cells.end());
        if (tree.cells.empty() || world.at(tree.rootX, tree.rootY).mat != mat::Wood) continue;
        // Leaf blobs that never touched the tree would float; a tree is one connected piece.
        for (const uint32_t cell : pruneTree(world, tree))
            world.clear(static_cast<int>(cell % static_cast<uint32_t>(w_)), static_cast<int>(cell / static_cast<uint32_t>(w_)));
        out.trees.push_back(std::move(tree));
    }
    // Grass tufts along the surface (leaf matter: they stand rather than hang like vines).
    for (int x = 0; x < w_; ++x) {
        const int ground = skyline_[static_cast<size_t>(x)];
        if (!isFloor(world, x, ground - 1) || hash2(x, 0, seed_) % 3u == 0u) continue;
        int blades = 1 + static_cast<int>(hash2(x, 1, seed_) % 3u);
        for (int k = 1; k <= blades; ++k) world.placeIfEmpty(x, ground - k, mat::Leaves);
    }
}

void Generator::placeMushroom(World& world, int x, int y) {
    int stem = rng_.rangeInt(2, 5);
    for (int k = 0; k < stem; ++k) world.placeIfEmpty(x, y - k, mat::Bone);
    int capY = y - stem;
    int capR = rng_.rangeInt(2, 4);
    for (int dx = -capR; dx <= capR; ++dx) {
        world.placeIfEmpty(x + dx, capY, mat::Fungus);
        if (std::abs(dx) < capR) world.placeIfEmpty(x + dx, capY - 1, mat::Fungus);
    }
}

void Generator::placeStructures(World& world) {
    // Wooden bridges across open spans near the path.
    int bridges = rng_.rangeInt(4, 8);
    for (int i = 0; i < bridges; ++i) {
        const Vec2& p = path_[static_cast<size_t>(rng_.rangeInt(1, static_cast<int>(path_.size()) - 2))];
        int y = static_cast<int>(p.y) + rng_.rangeInt(-20, 20);
        int x = static_cast<int>(p.x);
        if (!isOpen(x, y)) continue;
        int l = x, r = x;
        while (l > 0 && isOpen(l, y) && x - l < 70) --l;
        while (r < w_ - 1 && isOpen(r, y) && r - x < 70) ++r;
        if (isOpen(l, y) || isOpen(r, y) || r - l < 16) continue;
        for (int bx = l; bx <= r; ++bx) {
            world.placeIfEmpty(bx, y, mat::Wood);
            if ((bx - l) % 9 == 0) world.placeIfEmpty(bx, y + 1, mat::Wood);
        }
    }

    // Gunpowder kegs in the sulphur mines, crystal clusters in the cold and the forge.
    if (biome_ == BiomeId::Citrinitas || biome_ == BiomeId::Nigredo) {
        int kegs = biome_ == BiomeId::Citrinitas ? 10 : 4;
        for (int i = 0; i < kegs; ++i) {
            int x = rng_.rangeInt(20, w_ - 20), y = rng_.rangeInt(100, h_ - 40);
            for (int k = 0; k < 60 && y < h_ - 20; ++k, ++y) {
                if (isFloor(world, x, y)) {
                    for (int by = y - 6; by <= y; ++by)
                        for (int bx = x - 3; bx <= x + 3; ++bx) {
                            bool shell = by == y - 6 || by == y || bx == x - 3 || bx == x + 3;
                            world.placeIfEmpty(bx, by, shell ? mat::Wood : mat::Gunpowder);
                        }
                    break;
                }
            }
        }
    }
    if (biome_ == BiomeId::Albedo || biome_ == BiomeId::Rubedo || biome_ == BiomeId::Athanor) {
        int clusters = 18;
        for (int i = 0; i < clusters; ++i) {
            int x = rng_.rangeInt(20, w_ - 20), y = rng_.rangeInt(40, h_ - 40);
            if (!isOpen(x, y)) continue;
            // Walk to a wall and grow a few crystal spikes from it.
            int dir = rng_.sign();
            while (isOpen(x, y) && x > 10 && x < w_ - 10) x += dir;
            for (int s = 0; s < 4; ++s) {
                float a = (dir > 0 ? kPi : 0.0f) + rng_.range(-0.9f, 0.9f);
                int len = rng_.rangeInt(4, 12);
                for (int k = 0; k < len; ++k) {
                    int cx = x + static_cast<int>(std::cos(a) * k), cy = y + static_cast<int>(std::sin(a) * k);
                    int thick = std::max(0, (len - k) / 4);
                    for (int t = -thick; t <= thick; ++t) world.placeIfEmpty(cx, cy + t, mat::Crystal);
                }
            }
        }
    }
}

float Generator::distanceToPath(Vec2 p) const {
    float best = 1e9f;
    for (size_t i = 0; i + 1 < path_.size(); ++i) {
        Vec2 a = path_[i], b = path_[i + 1];
        Vec2 ab = b - a;
        float t = clamp(dot(p - a, ab) / std::max(1.0f, ab.lengthSq()), 0.0f, 1.0f);
        best = std::min(best, (a + ab * t - p).length());
    }
    return best;
}

void Generator::placePools(World& world) {
    const bool molten = biome_ == BiomeId::Rubedo || biome_ == BiomeId::Athanor;
    const bool flooded = omen_ == Omen::Flooded;
    const int baseAttempts = molten ? 70 : 46;
    const int attempts = baseAttempts * (flooded ? 3 : 1);
    for (int i = 0; i < attempts; ++i) {
        int x = rng_.rangeInt(20, w_ - 20), y = rng_.rangeInt(120, h_ - 30);
        if (!isOpen(x, y)) continue;
        // Find the floor below; pools only form in basins with walls on both sides.
        int fy = y;
        while (fy < h_ - 10 && isOpen(x, fy + 1) && fy - y < 60) ++fy;
        if (isOpen(x, fy + 1)) continue;
        // The flood's extra pools keep off the main route so it never becomes a long swim.
        if (i >= baseAttempts && distanceToPath({static_cast<float>(x), static_cast<float>(fy)}) < 30.0f) continue;
        MatId liquid = pickWeighted(b_.pools, rng_);
        if (liquid == mat::Empty) continue;
        bool dangerous = liquid == mat::Lava || liquid == mat::Acid;
        // A flood is water, even in the forge; a volatile stage keeps oil about.
        if (flooded && dangerous) liquid = mat::Water;
        if (omen_ == Omen::Volatile && rng_.chance(0.35f)) liquid = mat::Oil;
        dangerous = liquid == mat::Lava || liquid == mat::Acid;
        if (dangerous && distanceToPath({static_cast<float>(x), static_cast<float>(fy)}) < 40.0f) liquid = mat::Water;
        if (biome_ == BiomeId::Rubedo && liquid == mat::Water && !flooded) liquid = mat::Oil;
        int r = rng_.rangeInt(6, 15) + (molten && liquid == mat::Lava ? 5 : 0) + (flooded ? 6 : 0);
        // Never in the hollow the alchemist starts in (checked after the draws, so other pools keep their rolls).
        const Vec2 centre{static_cast<float>(x), static_cast<float>(fy - r)};
        if ((centre - start_).length() < static_cast<float>(r) + 40.0f) continue;
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (dx * dx + dy * dy <= r * r) world.placeIfEmpty(x + dx, fy - r + dy, liquid);
    }
    // A rare vein of Liquid Midas: the alchemist's jackpot.
    if (rng_.chance(0.35f + 0.1f * static_cast<float>(level_))) {
        for (int tries = 0; tries < 50; ++tries) {
            int x = rng_.rangeInt(30, w_ - 30), y = rng_.rangeInt(200, h_ - 60);
            if (isOpen(x, y) && distanceToPath({static_cast<float>(x), static_cast<float>(y)}) > 60.0f) {
                for (int dy = -3; dy <= 3; ++dy)
                    for (int dx = -3; dx <= 3; ++dx) world.placeIfEmpty(x + dx, y + dy, mat::Midas);
                break;
            }
        }
    }
}

void Generator::placePowders(World& world) {
    const bool volatileOmen = omen_ == Omen::Volatile;
    const int attempts = volatileOmen ? 120 : 28;
    for (int i = 0; i < attempts; ++i) {
        int x = rng_.rangeInt(20, w_ - 20), y = rng_.rangeInt(80, h_ - 30);
        if (!isOpen(x, y)) continue;
        if (distanceToPath({static_cast<float>(x), static_cast<float>(y)}) < 30.0f) continue;
        MatId powder = pickWeighted(b_.powders, rng_);
        if (volatileOmen && rng_.chance(0.6f)) powder = mat::Gunpowder;
        if (powder == mat::Empty) continue;
        int r = rng_.rangeInt(4, 10) + (volatileOmen ? 2 : 0);
        // Not above (or around) the start: on a surface stage it lies off the route, and loose powder
        // would pour down onto the alchemist (checked after the draws, so other heaps keep their rolls).
        if (std::abs(static_cast<float>(x) - start_.x) < static_cast<float>(r) + 40.0f && static_cast<float>(y) < start_.y + 20.0f)
            continue;
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (dx * dx + dy * dy <= r * r) world.placeIfEmpty(x + dx, y + dy, powder);
    }
}

void Generator::placeExitPlatform(World& world) {
    int ey = static_cast<int>(exit_.y) + 12;
    for (int x = static_cast<int>(exit_.x) - 20; x <= static_cast<int>(exit_.x) + 20; ++x) {
        for (int y = ey; y < ey + 5; ++y) world.place(x, y, mat::Rune);
        for (int y = ey - 40; y < ey; ++y)
            if (world.inBounds(x, y) && phaseOf(world.at(x, y).mat) != Phase::Empty) world.clear(x, y);
    }
    exit_.y = static_cast<float>(ey - 1);
    if (surface_) {
        // Solid ground under the arrival point, whatever the caves below did.
        const int gx = static_cast<int>(start_.x), gy = static_cast<int>(start_.y) + 1;
        for (int x = gx - 16; x <= gx + 16; ++x)
            for (int y = gy; y < gy + 8; ++y)
                if (world.inBounds(x, y) && phaseOf(world.at(x, y).mat) == Phase::Empty)
                    world.at(x, y) = makeCell(y == gy ? mat::Moss : mat::Soil, hash2(x, y, seed_));
        return;
    }
    int sy = static_cast<int>(start_.y) + 12;
    for (int x = static_cast<int>(start_.x) - 12; x <= static_cast<int>(start_.x) + 12; ++x) {
        for (int y = sy; y < sy + 4; ++y) world.place(x, y, b_.hardRock);
    }
    start_.y = static_cast<float>(sy - 1);
}

void Generator::presimulate(World& world, ThreadPool& pool) {
    world.wakeAll();
    for (int i = 0; i < opt_.presimTicks; ++i) world.step(pool);
    world.events().clear();
}

void Generator::clearStart(World& world) {
    const int sx = static_cast<int>(start_.x), sy = static_cast<int>(start_.y);
    for (int y = sy - 18; y <= sy; ++y)
        for (int x = sx - 8; x <= sx + 8; ++x) {
            if (!world.inBounds(x, y)) continue;
            const Phase ph = phaseOf(world.at(x, y).mat);
            if (ph == Phase::Powder || ph == Phase::Liquid) world.clear(x, y);
        }
}

void Generator::clearPath(World& world, int radius) {
    // Loose material that settled into the main corridor is removed so the route stays
    // open; harmless liquids are left in place. The final (widest) pass also re-carves rock.
    for (size_t i = 0; i + 1 < path_.size(); ++i) {
        Vec2 a = path_[i], b = path_[i + 1];
        float len = (b - a).length();
        if (len < 1.0f) continue; // coincident points: t / len would be NaN
        for (float t = 0; t <= len; t += 3.0f) {
            Vec2 p = lerp(a, b, t / len);
            for (int dy = -radius; dy <= radius; ++dy)
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx * dx + dy * dy > radius * radius) continue;
                    int x = static_cast<int>(p.x) + dx, y = static_cast<int>(p.y) + dy;
                    if (!world.inBounds(x, y) || x < kBorder + 3 || y < kBorder + 3 || x >= w_ - kBorder - 3 ||
                        y >= h_ - kBorder - 3)
                        continue;
                    if (!isOpen(x, y) && radius < kClearRadiusMax) continue;
                    MatId m = world.at(x, y).mat;
                    Phase ph = phaseOf(m);
                    if (ph == Phase::Powder || m == mat::Lava || m == mat::Acid || ph == Phase::Solid)
                        world.clear(x, y);
                }
        }
    }
}

} // namespace pm::gen_detail
