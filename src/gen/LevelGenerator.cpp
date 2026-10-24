#include "gen/LevelGenerator.hpp"

#include <algorithm>
#include <cmath>

#include "core/Noise.hpp"
#include "core/Rng.hpp"
#include "core/ThreadPool.hpp"
#include "gen/Navigation.hpp"
#include "gen/Vaults.hpp"
#include "sim/World.hpp"

namespace pm {

namespace {

constexpr int kBorder = 4;
constexpr int kSkyRows = 200; ///< open sky added above the first stage

/// Picks from a small weighted table; entries with zero weight are skipped.
template <size_t N>
MatId pickWeighted(const std::array<WeightedMat, N>& table, Rng& rng) {
    float total = 0.0f;
    for (const auto& e : table) total += e.weight;
    float r = rng.uniform() * total;
    for (const auto& e : table) {
        if (e.weight <= 0.0f) continue;
        if (r < e.weight) return e.mat;
        r -= e.weight;
    }
    return table[0].mat;
}

class Generator {
public:
    Generator(BiomeId biome, int levelIndex, uint32_t seed, const GenOptions& opt)
        : w_(opt.width), h_(opt.height), opt_(opt), biome_(biome), b_(biomeDef(biome)), level_(levelIndex),
          seed_(seed), rng_(seed), surface_(levelIndex == 0) {
        // The first stage begins outdoors on the mountainside, so it gets extra sky above.
        if (surface_) h_ += kSkyRows;
    }

    LevelLayout run(ThreadPool& pool) {
        open_.assign(static_cast<size_t>(w_) * h_, 0);
        planPath();
        carveNoiseCaves(pool);
        carvePath();
        carveChambers();
        carveBranches();
        smooth();
        sealBorders();
        carvePockets();

        LevelLayout out;
        out.world = std::make_unique<World>(w_, h_, seed_);
        World& world = *out.world;
        paintMaterials(world, pool);
        placeVegetation(world);
        if (surface_) placeTrees(world);
        placeStructures(world);
        placePools(world);
        placePowders(world);
        placeExitPlatform(world);
        placeArenaPlatforms(world);
        presimulate(world, pool);
        // Guarantee the route: progressively wider clearing until a player-sized body fits.
        for (int radius = 7; radius <= 13; radius += 3) {
            clearPath(world, radius);
            if (pathExists(world, start_, exit_)) break;
        }

        out.biome = biome_;
        out.levelIndex = level_;
        out.seed = seed_;
        out.playerStart = start_;
        out.exitPos = exit_;
        out.mainPath = path_;
        if (surface_) {
            out.skyline = skyline_;
            out.surfaceY = surfaceY_;
        }
        collectSpawns(world, out);
        placeTransmuter(world, out);
        placeVaults(world, out);
        return out;
    }

private:
    size_t idx(int x, int y) const { return static_cast<size_t>(y) * w_ + x; }
    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }
    bool isOpen(int x, int y) const { return inBounds(x, y) && open_[idx(x, y)] != 0; }

    void carveCircle(float cx, float cy, float r, float roughness = 0.0f) {
        int x0 = std::max(1, static_cast<int>(cx - r - 3)), x1 = std::min(w_ - 2, static_cast<int>(cx + r + 3));
        int y0 = std::max(1, static_cast<int>(cy - r - 3)), y1 = std::min(h_ - 2, static_cast<int>(cy + r + 3));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                float dx = static_cast<float>(x) - cx, dy = static_cast<float>(y) - cy;
                float rr = r;
                if (roughness > 0.0f) rr += noise::perlin(x * 0.15f, y * 0.15f, seed_ ^ 0x77u) * roughness;
                if (dx * dx + dy * dy <= rr * rr) open_[idx(x, y)] = 1;
            }
        }
    }

    void carveSegment(Vec2 a, Vec2 b, float baseRadius, float radiusVar, float wobble, uint32_t salt) {
        Vec2 d = b - a;
        float len = d.length();
        if (len < 1.0f) return;
        Vec2 dir = d / len;
        Vec2 perp{-dir.y, dir.x};
        for (float t = 0.0f; t <= len; t += 2.0f) {
            float s = t * 0.012f;
            float off = noise::perlin(s, static_cast<float>(salt) * 0.37f, seed_ ^ salt) * wobble;
            // Taper the wobble at segment ends so consecutive segments stay connected.
            float taper = std::min(1.0f, std::min(t, len - t) / 40.0f);
            Vec2 p = a + dir * t + perp * (off * taper);
            float r = baseRadius + noise::perlin(s * 2.0f, 9.1f, seed_ ^ (salt * 7u)) * radiusVar;
            carveCircle(p.x, p.y, std::max(6.0f, r), 1.5f);
        }
    }

    void planSkyline() {
        surfaceY_ = kSkyRows - 40;
        skyline_.assign(static_cast<size_t>(w_), surfaceY_);
        for (int x = 0; x < w_; ++x) {
            float hills = noise::fbm(static_cast<float>(x) * 0.006f, 3.7f, seed_ ^ 0x51u, 3) * 34.0f;
            float bumps = noise::perlin(static_cast<float>(x) * 0.05f, 1.1f, seed_ ^ 0x52u) * 4.0f;
            skyline_[static_cast<size_t>(x)] = surfaceY_ + static_cast<int>(hills + bumps);
        }
    }

    void flattenSkyline(int cx, int halfWidth, int height) {
        for (int x = std::max(0, cx - halfWidth - 20); x <= std::min(w_ - 1, cx + halfWidth + 20); ++x) {
            int d = std::abs(x - cx);
            float t = d <= halfWidth ? 1.0f : 1.0f - static_cast<float>(d - halfWidth) / 20.0f;
            int& h = skyline_[static_cast<size_t>(x)];
            h = static_cast<int>(lerp(static_cast<float>(h), static_cast<float>(height), t));
        }
    }

    void planPath() {
        start_ = {static_cast<float>(w_) * rng_.range(0.3f, 0.7f), 56.0f};
        if (surface_) {
            planSkyline();
            // The alchemist arrives on the surface and walks to a cave mouth nearby.
            const int entranceX = static_cast<int>(static_cast<float>(w_) * rng_.range(0.4f, 0.6f));
            const int dir = rng_.chance(0.5f) ? 1 : -1;
            const int startX = entranceX - dir * 115;
            const int ground = skyline_[static_cast<size_t>(startX)];
            flattenSkyline(startX, 22, ground);
            start_ = {static_cast<float>(startX), static_cast<float>(ground - 1)};
            path_.push_back({static_cast<float>(entranceX), static_cast<float>(skyline_[static_cast<size_t>(entranceX)])});
        }
        exit_ = {static_cast<float>(w_) * rng_.range(0.2f, 0.8f), static_cast<float>(h_ - 64)};
        const int legs = 6 + std::min(level_, 2);
        if (path_.empty()) path_.push_back(start_);
        bool left = rng_.chance(0.5f);
        for (int i = 1; i < legs; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(legs);
            float y = lerp(path_.front().y, exit_.y, t) + rng_.range(-25.0f, 25.0f);
            float x = left ? rng_.range(0.12f, 0.42f) : rng_.range(0.58f, 0.88f);
            left = !left;
            path_.push_back({x * static_cast<float>(w_), y});
        }
        path_.push_back(exit_);
    }

    void carveNoiseCaves(ThreadPool& pool) {
        // Coarse field (1/4 resolution) for the big shapes, bilinear-upsampled, plus a fine octave.
        const int cs = 4;
        const int gw = w_ / cs + 2, gh = h_ / cs + 2;
        std::vector<float> coarse(static_cast<size_t>(gw) * gh);
        const float threshold = lerp(0.32f, 0.08f, b_.openness);
        const uint32_t s = seed_;
        pool.parallelFor(gh, [&](int gy) {
            for (int gx = 0; gx < gw; ++gx) {
                float x = static_cast<float>(gx * cs), y = static_cast<float>(gy * cs);
                // Horizontally stretched caves read better in a side view.
                float n = noise::warped(x * 0.0085f, y * 0.012f, s, 4, 1.4f);
                float ridges = noise::ridged(x * 0.006f, y * 0.006f, s ^ 0xBEEFu, 3);
                coarse[static_cast<size_t>(gy) * gw + gx] = n + (ridges - 0.55f) * 0.35f;
            }
        });
        pool.parallelFor(h_, [&](int y) {
            for (int x = 0; x < w_; ++x) {
                float fx = static_cast<float>(x) / cs, fy = static_cast<float>(y) / cs;
                int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
                float tx = fx - ix, ty = fy - iy;
                auto at = [&](int gx, int gy) { return coarse[static_cast<size_t>(gy) * gw + gx]; };
                float v = lerp(lerp(at(ix, iy), at(ix + 1, iy), tx), lerp(at(ix, iy + 1), at(ix + 1, iy + 1), tx), ty);
                v += noise::perlin(x * 0.06f, y * 0.06f, s ^ 0x1234u) * 0.06f;
                if (v > threshold || aboveGround(x, y)) open_[idx(x, y)] = 1;
            }
        });
    }

    void carvePath() {
        for (size_t i = 0; i + 1 < path_.size(); ++i) {
            carveSegment(path_[i], path_[i + 1], 14.0f, 3.0f, 34.0f, static_cast<uint32_t>(i * 31 + 5));
        }
    }

    void carveChambers() {
        int count = rng_.rangeInt(5, 8);
        for (int i = 0; i < count; ++i) {
            const Vec2& anchor = path_[static_cast<size_t>(rng_.rangeInt(1, static_cast<int>(path_.size()) - 2))];
            float cx = clamp(anchor.x + rng_.range(-140.0f, 140.0f), 70.0f, static_cast<float>(w_ - 70));
            float cy = clamp(anchor.y + rng_.range(-60.0f, 60.0f), 90.0f, static_cast<float>(h_ - 90));
            float rx = rng_.range(30.0f, 70.0f), ry = rng_.range(18.0f, 36.0f);
            for (int y = static_cast<int>(cy - ry - 8); y <= static_cast<int>(cy + ry + 8); ++y) {
                for (int x = static_cast<int>(cx - rx - 8); x <= static_cast<int>(cx + rx + 8); ++x) {
                    if (!inBounds(x, y)) continue;
                    float dx = (x - cx) / rx, dy = (y - cy) / ry;
                    float edge = 1.0f + noise::perlin(x * 0.07f, y * 0.07f, seed_ ^ 0x5151u) * 0.25f;
                    if (dx * dx + dy * dy < edge) open_[idx(x, y)] = 1;
                }
            }
            chambers_.push_back({cx, cy});
            // Connect to the path so every chamber is reachable.
            carveSegment({cx, cy}, anchor, 8.0f, 2.0f, 12.0f, static_cast<uint32_t>(900 + i));
        }
    }

    void carveBranches() {
        int count = rng_.rangeInt(4, 7);
        for (int i = 0; i < count; ++i) {
            Vec2 p = path_[static_cast<size_t>(rng_.rangeInt(0, static_cast<int>(path_.size()) - 1))];
            float angle = rng_.chance(0.5f) ? rng_.range(-0.5f, 0.5f) : rng_.range(kPi - 0.5f, kPi + 0.5f);
            int steps = rng_.rangeInt(40, 110);
            float r = rng_.range(5.0f, 8.0f);
            for (int s = 0; s < steps; ++s) {
                angle += rng_.range(-0.25f, 0.25f);
                p += fromAngle(angle, 2.5f);
                p.x = clamp(p.x, 20.0f, static_cast<float>(w_ - 20));
                p.y = clamp(p.y, 30.0f, static_cast<float>(h_ - 30));
                carveCircle(p.x, p.y, r, 1.0f);
            }
            pockets_.push_back(p);
        }
    }

    void smooth() {
        std::vector<uint8_t> next = open_;
        for (int y = 1; y < h_ - 1; ++y) {
            for (int x = 1; x < w_ - 1; ++x) {
                int n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) n += open_[idx(x + dx, y + dy)];
                next[idx(x, y)] = n >= 5 ? 1 : 0;
            }
        }
        open_.swap(next);
    }

    bool aboveGround(int x, int y) const { return surface_ && y < skyline_[static_cast<size_t>(x)]; }

    void sealBorders() {
        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                if (aboveGround(x, y)) {
                    open_[idx(x, y)] = 1;
                    continue;
                }
                if (x < kBorder || y < kBorder || x >= w_ - kBorder || y >= h_ - kBorder) open_[idx(x, y)] = 0;
            }
        }
    }

    void carvePockets() {
        if (!surface_) carveCircle(start_.x, start_.y, 20.0f, 2.0f);
        carveCircle(exit_.x, exit_.y - 10.0f, 30.0f, 2.0f);
        if (biome_ == BiomeId::Athanor) carveArena();
        // Reach the start pocket from above the first path segment's top.
        sealBorders();
    }

    /// The final stage ends in a vast furnace hall where the Rebis waits above the portal.
    void carveArena() {
        const float cx = exit_.x, cy = exit_.y - 70.0f;
        const float rx = std::min(200.0f, static_cast<float>(w_) * 0.5f - 30.0f), ry = 95.0f;
        for (int y = static_cast<int>(cy - ry - 6); y <= static_cast<int>(cy + ry + 6); ++y)
            for (int x = static_cast<int>(cx - rx - 6); x <= static_cast<int>(cx + rx + 6); ++x) {
                if (!inBounds(x, y)) continue;
                float dx = (static_cast<float>(x) - cx) / rx, dy = (static_cast<float>(y) - cy) / ry;
                float edge = 1.0f + noise::perlin(x * 0.05f, y * 0.05f, seed_ ^ 0xA7u) * 0.08f;
                if (dx * dx + dy * dy < edge) open_[idx(x, y)] = 1;
            }
        arena_ = {cx, cy, rx, ry};
    }

    void placeArenaPlatforms(World& world) {
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

    void paintMaterials(World& world, ThreadPool& pool) {
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
                        float n = noise::fbm(x * v.scale, y * v.scale, s + 101u * static_cast<uint32_t>(k + 1), 2);
                        if (n * 0.5f + 0.5f > v.threshold) {
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

    bool isFloor(const World& w, int x, int y) const {
        // An open cell directly above a solid one.
        return w.inBounds(x, y + 1) && w.at(x, y).mat == mat::Empty && isSolidForCollision(w.at(x, y + 1).mat);
    }

    void placeVegetation(World& world) {
        if (b_.vegetation <= 0.0f) return;
        for (int y = kBorder + 1; y < h_ - kBorder - 1; ++y) {
            for (int x = kBorder + 1; x < w_ - kBorder - 1; ++x) {
                if (!open_[idx(x, y)]) continue;
                MatId below = world.at(x, y + 1).mat;
                MatId above = world.at(x, y - 1).mat;
                float n = noise::perlin(x * 0.04f, y * 0.04f, seed_ ^ 0xC0FEu);
                // Moss carpets on floors.
                if (isSolidForCollision(below) && phaseOf(below) == Phase::Solid && n > 0.35f - b_.vegetation * 0.4f) {
                    int thickness = 1 + static_cast<int>(hash2(x, y, seed_) % 3u);
                    for (int k = 1; k <= thickness; ++k) {
                        if (world.inBounds(x, y + k) && phaseOf(world.at(x, y + k).mat) == Phase::Solid &&
                            isSolidForCollision(world.at(x, y + k).mat) && world.at(x, y + k).mat != mat::Bedrock)
                            world.at(x, y + k) = makeCell(mat::Moss, hash2(x, y + k, seed_));
                    }
                }
                // Hanging strands from ceilings.
                if (isSolidForCollision(above) && phaseOf(above) == Phase::Solid && above != mat::Bedrock &&
                    (hash2(x, y, seed_ ^ 7u) % 1000u) < static_cast<uint32_t>(b_.vegetation * 70.0f)) {
                    int len = 3 + static_cast<int>(hash2(x, y, seed_ ^ 9u) % 14u);
                    MatId strand = (biome_ == BiomeId::Citrinitas || hash2(x, y, 3u) % 4u == 0) ? mat::GlowVine : mat::Vine;
                    for (int k = 0; k < len && world.inBounds(x, y + k) && world.at(x, y + k).mat == mat::Empty; ++k)
                        world.at(x, y + k) = makeCell(strand, hash2(x, y + k, seed_));
                }
            }
        }
        // Glowcap mushrooms on floors.
        int shrooms = static_cast<int>(40 * b_.vegetation);
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

    void placeTrees(World& world) {
        const float entranceX = path_.front().x;
        for (int x = 20; x < w_ - 20; x += rng_.rangeInt(26, 60)) {
            if (std::fabs(static_cast<float>(x) - entranceX) < 30.0f || std::fabs(static_cast<float>(x) - start_.x) < 14.0f) continue;
            const int ground = skyline_[static_cast<size_t>(x)];
            if (!isFloor(world, x, ground - 1)) continue;
            const int height = rng_.rangeInt(16, 34);
            const int top = ground - height;
            // Trunk, slightly leaning, with a couple of branches.
            float lean = rng_.range(-0.15f, 0.15f);
            for (int y = ground - 1; y >= top; --y) {
                int tx = x + static_cast<int>(lean * static_cast<float>(ground - y));
                world.placeIfEmpty(tx, y, mat::Wood);
                if (y > top + height / 2) world.placeIfEmpty(tx + 1, y, mat::Wood);
            }
            for (int b = 0; b < 2; ++b) {
                int by = top + rng_.rangeInt(3, height / 2);
                int dir = rng_.sign();
                int bx = x + static_cast<int>(lean * static_cast<float>(ground - by));
                for (int k = 1; k < rng_.rangeInt(4, 8); ++k) world.placeIfEmpty(bx + dir * k, by - k / 2, mat::Wood);
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
                            if (world.inBounds(cx + dx, cy + dy) && world.at(cx + dx, cy + dy).mat == mat::Empty) world.at(cx + dx, cy + dy) = c;
                        }
            }
        }
        // Grass tufts along the surface.
        for (int x = 0; x < w_; ++x) {
            const int ground = skyline_[static_cast<size_t>(x)];
            if (!isFloor(world, x, ground - 1) || hash2(x, 0, seed_) % 3u == 0u) continue;
            int blades = 1 + static_cast<int>(hash2(x, 1, seed_) % 3u);
            for (int k = 1; k <= blades; ++k) world.placeIfEmpty(x, ground - k, mat::Vine);
        }
    }

    /// A runestone basin that rewards the alchemist for pouring in a particular material.
    void placeTransmuter(World& world, LevelLayout& out) {
        std::vector<Vec2> candidates = chambers_;
        for (int attempt = 0; attempt < 40 && !candidates.empty(); ++attempt) {
            const Vec2 c = candidates[static_cast<size_t>(rng_.rangeInt(0, static_cast<int>(candidates.size()) - 1))];
            int x = static_cast<int>(c.x) + rng_.rangeInt(-20, 20), y = static_cast<int>(c.y);
            // Drop to the chamber floor.
            while (y < h_ - 20 && world.inBounds(x, y + 1) && !isSolidForCollision(world.at(x, y + 1).mat)) ++y;
            if (y >= h_ - 20 || (Vec2{static_cast<float>(x), static_cast<float>(y)} - start_).length() < 120.0f) continue;
            // Clear headroom and build the basin: a runestone tub with a raised rim.
            for (int dy = -14; dy <= 0; ++dy)
                for (int dx = -11; dx <= 11; ++dx)
                    if (world.inBounds(x + dx, y + dy) && world.at(x + dx, y + dy).mat != mat::Bedrock) world.clear(x + dx, y + dy);
            for (int dx = -10; dx <= 10; ++dx) {
                for (int dy = 1; dy <= 4; ++dy) world.place(x + dx, y + dy, mat::Rune);
            }
            for (int dy = -4; dy <= 0; ++dy) {
                world.place(x - 10, y + dy, mat::Rune);
                world.place(x + 10, y + dy, mat::Rune);
            }
            out.spawns.push_back({SpawnKind::Transmuter, {static_cast<float>(x), static_cast<float>(y)}, 0.5f});
            return;
        }
    }

    /// True if every cell of the rectangle is solid ground the vault may replace.
    bool solidRect(const World& world, int x0, int y0, int w, int h) const {
        for (int y = y0; y < y0 + h; y += 2)
            for (int x = x0; x < x0 + w; x += 2) {
                if (!world.inBounds(x, y) || isOpen(x, y)) return false;
                const MatId m = world.at(x, y).mat;
                if (m == mat::Bedrock || m == mat::Rune || phaseOf(m) != Phase::Solid) return false;
            }
        return true;
    }

    void placeVaults(World& world, LevelLayout& out) {
        NavGrid nav;
        nav.build(world);
        if (!nav.computeDistances(start_)) return;
        const int wanted = level_ >= 4 ? 1 : 2;
        const int top = (surface_ ? kSkyRows : 0) + 70;
        uint32_t used = 0;
        std::vector<Rectf> placed;
        for (int attempt = 0; attempt < 500 && static_cast<int>(placed.size()) < wanted; ++attempt) {
            VaultPlan plan;
            plan.kind = pickVaultKind(biome_, rng_, used);
            vaultSize(plan.kind, plan.w, plan.h);
            if (w_ - 2 * kBorder - 40 < plan.w || h_ - top - 60 < plan.h) return;
            plan.x0 = rng_.rangeInt(kBorder + 20, w_ - kBorder - 20 - plan.w);
            plan.y0 = rng_.rangeInt(top, h_ - kBorder - 40 - plan.h);
            const Vec2 c{static_cast<float>(plan.x0) + plan.w * 0.5f, static_cast<float>(plan.y0) + plan.h * 0.5f};
            const float extent = static_cast<float>(std::max(plan.w, plan.h)) * 0.5f;
            if ((c - start_).length() < 170.0f || (c - exit_).length() < 120.0f || distanceToPath(c) < extent + 30.0f) continue;
            if (arena_.w > 0.0f && (c - Vec2{arena_.x, arena_.y}).length() < arena_.w + extent + 30.0f) continue;
            bool overlaps = false;
            for (const Rectf& r : placed)
                overlaps |= std::fabs(r.x - c.x) < (r.w + plan.w) * 0.5f + 30.0f && std::fabs(r.y - c.y) < (r.h + plan.h) * 0.5f + 30.0f;
            if (overlaps || !solidRect(world, plan.x0 - 4, plan.y0 - 4, plan.w + 8, plan.h + 8)) continue;

            // The doorway must tunnel through to caves the player can actually reach.
            const int door = vaultDoorRow(plan);
            int tunnelEnd = -1;
            const int firstSide = rng_.sign();
            for (int s = 0; s < 2 && tunnelEnd < 0; ++s) {
                plan.doorSide = s == 0 ? firstSide : -firstSide;
                const int startX = plan.doorSide > 0 ? plan.x0 + plan.w : plan.x0 - 1;
                for (int k = 0; k < 110; ++k) {
                    const int x = startX + plan.doorSide * k, y = door - 4;
                    if (!world.inBounds(x, y) || x < kBorder + 6 || x >= w_ - kBorder - 6) break;
                    if (world.at(x, y).mat == mat::Empty && nav.distanceAt({static_cast<float>(x), static_cast<float>(y)}) >= 0) {
                        tunnelEnd = x;
                        break;
                    }
                }
            }
            if (tunnelEnd < 0) continue;

            buildVault(world, plan, rng_, out.spawns);
            // A paved passage with timber lintels leads in from the caves.
            const int startX = plan.doorSide > 0 ? plan.x0 + plan.w : plan.x0 - 1;
            const int x0 = std::min(startX, tunnelEnd), x1 = std::max(startX, tunnelEnd);
            for (int x = x0; x <= x1; ++x) {
                for (int y = door - 12; y <= door; ++y)
                    if (world.at(x, y).mat != mat::Bedrock && phaseOf(world.at(x, y).mat) == Phase::Solid) world.clear(x, y);
                if (phaseOf(world.at(x, door + 1).mat) == Phase::Solid && world.at(x, door + 1).mat != mat::Bedrock)
                    world.place(x, door + 1, mat::Brick);
                if (std::abs(x - startX) % 14 < 2 && phaseOf(world.at(x, door - 13).mat) == Phase::Solid)
                    world.place(x, door - 13, mat::Wood);
            }
            placed.push_back({c.x, c.y, static_cast<float>(plan.w), static_cast<float>(plan.h)});
            out.vaults.push_back({static_cast<float>(plan.x0), static_cast<float>(plan.y0), static_cast<float>(plan.w), static_cast<float>(plan.h)});
            used |= 1u << static_cast<unsigned>(plan.kind);
        }
    }

    void placeMushroom(World& world, int x, int y) {
        int stem = rng_.rangeInt(2, 5);
        for (int k = 0; k < stem; ++k) world.placeIfEmpty(x, y - k, mat::Bone);
        int capY = y - stem;
        int capR = rng_.rangeInt(2, 4);
        for (int dx = -capR; dx <= capR; ++dx) {
            world.placeIfEmpty(x + dx, capY, mat::Fungus);
            if (std::abs(dx) < capR) world.placeIfEmpty(x + dx, capY - 1, mat::Fungus);
        }
    }

    void placeStructures(World& world) {
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

    float distanceToPath(Vec2 p) const {
        float best = 1e9f;
        for (size_t i = 0; i + 1 < path_.size(); ++i) {
            Vec2 a = path_[i], b = path_[i + 1];
            Vec2 ab = b - a;
            float t = clamp(dot(p - a, ab) / std::max(1.0f, ab.lengthSq()), 0.0f, 1.0f);
            best = std::min(best, (a + ab * t - p).length());
        }
        return best;
    }

    void placePools(World& world) {
        const bool molten = biome_ == BiomeId::Rubedo || biome_ == BiomeId::Athanor;
        int attempts = molten ? 70 : 46;
        for (int i = 0; i < attempts; ++i) {
            int x = rng_.rangeInt(20, w_ - 20), y = rng_.rangeInt(120, h_ - 30);
            if (!isOpen(x, y)) continue;
            // Find the floor below; pools only form in basins with walls on both sides.
            int fy = y;
            while (fy < h_ - 10 && isOpen(x, fy + 1) && fy - y < 60) ++fy;
            if (isOpen(x, fy + 1)) continue;
            MatId liquid = pickWeighted(b_.pools, rng_);
            if (liquid == mat::Empty) continue;
            bool dangerous = liquid == mat::Lava || liquid == mat::Acid;
            if (dangerous && distanceToPath({static_cast<float>(x), static_cast<float>(fy)}) < 40.0f) liquid = mat::Water;
            if (biome_ == BiomeId::Rubedo && liquid == mat::Water) liquid = mat::Oil;
            int r = rng_.rangeInt(6, 15) + (molten && liquid == mat::Lava ? 5 : 0);
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

    void placePowders(World& world) {
        int attempts = 28;
        for (int i = 0; i < attempts; ++i) {
            int x = rng_.rangeInt(20, w_ - 20), y = rng_.rangeInt(80, h_ - 30);
            if (!isOpen(x, y)) continue;
            if (distanceToPath({static_cast<float>(x), static_cast<float>(y)}) < 30.0f) continue;
            MatId powder = pickWeighted(b_.powders, rng_);
            if (powder == mat::Empty) continue;
            int r = rng_.rangeInt(4, 10);
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx)
                    if (dx * dx + dy * dy <= r * r) world.placeIfEmpty(x + dx, y + dy, powder);
        }
    }

    void placeExitPlatform(World& world) {
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

    void presimulate(World& world, ThreadPool& pool) {
        world.wakeAll();
        for (int i = 0; i < opt_.presimTicks; ++i) world.step(pool);
        world.events().clear();
    }

    void clearPath(World& world, int radius) {
        // Loose material that settled into the main corridor is removed so the route stays
        // open; harmless liquids are left in place. Wider passes also re-carve rock.
        for (size_t i = 0; i + 1 < path_.size(); ++i) {
            Vec2 a = path_[i], b = path_[i + 1];
            float len = (b - a).length();
            for (float t = 0; t <= len; t += 3.0f) {
                Vec2 p = lerp(a, b, t / len);
                for (int dy = -radius; dy <= radius; ++dy)
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (dx * dx + dy * dy > radius * radius) continue;
                        int x = static_cast<int>(p.x) + dx, y = static_cast<int>(p.y) + dy;
                        if (!world.inBounds(x, y) || x < kBorder + 3 || y < kBorder + 3 || x >= w_ - kBorder - 3 ||
                            y >= h_ - kBorder - 3)
                            continue;
                        if (!isOpen(x, y) && radius < 13) continue;
                        MatId m = world.at(x, y).mat;
                        Phase ph = phaseOf(m);
                        if (ph == Phase::Powder || m == mat::Lava || m == mat::Acid || ph == Phase::Solid)
                            world.clear(x, y);
                    }
            }
        }
    }

    bool spaceAbove(const World& w, int x, int y, int height, int halfWidth) const {
        for (int dy = 0; dy < height; ++dy)
            for (int dx = -halfWidth; dx <= halfWidth; ++dx) {
                if (!w.inBounds(x + dx, y - dy)) return false;
                if (phaseOf(w.at(x + dx, y - dy).mat) != Phase::Empty) return false;
            }
        return true;
    }

    void collectSpawns(const World& world, LevelLayout& out) {
        auto difficultyAt = [&](float y) { return clamp((y - start_.y) / (exit_.y - start_.y), 0.0f, 1.0f); };
        auto farFromEnds = [&](Vec2 p, float minStart) {
            return (p - start_).length() > minStart && (p - exit_).length() > 50.0f;
        };

        int groundTarget = 16 + level_ * 4;
        int airTarget = 7 + level_ * 2;
        int tries = 0;
        int ground = 0, air = 0;
        while ((ground < groundTarget || air < airTarget) && tries++ < 6000) {
            int x = rng_.rangeInt(12, w_ - 12), y = rng_.rangeInt(40, h_ - 20);
            Vec2 p{static_cast<float>(x), static_cast<float>(y)};
            if (!farFromEnds(p, 110.0f)) continue;
            // Nothing lurks under the open sky; the first steps outside are peaceful.
            if (surface_ && y < skyline_[static_cast<size_t>(x)] + 40) continue;
            if (ground < groundTarget && isFloor(world, x, y) && spaceAbove(world, x, y, 14, 4)) {
                out.spawns.push_back({SpawnKind::GroundEnemy, p, difficultyAt(p.y)});
                ++ground;
            } else if (air < airTarget && spaceAbove(world, x, y, 24, 10)) {
                out.spawns.push_back({SpawnKind::AirEnemy, p, difficultyAt(p.y)});
                ++air;
            }
        }

        // Treasure: chests and potions favour chambers; free spells are rarer.
        auto placeOnFloor = [&](SpawnKind kind, Vec2 around, float spread) {
            for (int t = 0; t < 80; ++t) {
                int x = static_cast<int>(around.x + rng_.range(-spread, spread));
                int y = static_cast<int>(around.y + rng_.range(-spread * 0.5f, spread * 0.5f));
                for (int k = 0; k < 50 && world.inBounds(x, y); ++k, ++y) {
                    if (isFloor(world, x, y)) {
                        if (spaceAbove(world, x, y, 10, 3)) {
                            out.spawns.push_back({kind, {static_cast<float>(x), static_cast<float>(y)}, difficultyAt(y)});
                            return true;
                        }
                        break;
                    }
                    if (phaseOf(world.at(x, y).mat) != Phase::Empty) break;
                }
            }
            return false;
        };
        for (const Vec2& c : chambers_) {
            placeOnFloor(rng_.chance(0.6f) ? SpawnKind::Chest : SpawnKind::Potion, c, 40.0f);
        }
        for (const Vec2& p : pockets_) placeOnFloor(rng_.chance(0.5f) ? SpawnKind::Spell : SpawnKind::Potion, p, 12.0f);
        placeOnFloor(SpawnKind::Chest, path_[path_.size() / 2], 60.0f);

        // Lanterns along the main path help orientation in the dark.
        for (size_t i = 0; i + 1 < path_.size(); ++i) {
            Vec2 a = path_[i], b = path_[i + 1];
            float len = (b - a).length();
            for (float t = 40.0f; t < len; t += 110.0f) {
                Vec2 p = lerp(a, b, t / len);
                int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
                // Hang from the ceiling above the corridor centre.
                int cy = y;
                while (cy > 5 && world.inBounds(x, cy) && phaseOf(world.at(x, cy).mat) == Phase::Empty && y - cy < 30) --cy;
                if (y - cy < 30 && y - cy > 6)
                    out.spawns.push_back({SpawnKind::Lantern, {static_cast<float>(x), static_cast<float>(cy + 1)}, 0.0f});
            }
        }
    }

    int w_, h_;
    GenOptions opt_;
    BiomeId biome_;
    const BiomeDef& b_;
    int level_;
    uint32_t seed_;
    Rng rng_;
    std::vector<uint8_t> open_;
    std::vector<Vec2> path_;
    std::vector<Vec2> chambers_;
    std::vector<Vec2> pockets_;
    Vec2 start_, exit_;
    bool surface_ = false;
    int surfaceY_ = 0;
    Rectf arena_{0.0f, 0.0f, 0.0f, 0.0f}; ///< boss arena centre (x, y) and radii (w, h)
    std::vector<int> skyline_;
};

} // namespace

LevelLayout generateLevel(BiomeId biome, int levelIndex, uint32_t seed, ThreadPool& pool, const GenOptions& options) {
    Generator gen(biome, levelIndex, seed, options);
    LevelLayout layout = gen.run(pool);
    return layout;
}

bool pathExists(const World& world, Vec2 from, Vec2 to, std::vector<uint8_t>* reachedBlocks) {
    NavGrid nav;
    nav.build(world);
    if (!nav.computeDistances(to)) return false;
    if (reachedBlocks) {
        reachedBlocks->assign(nav.distances().size(), 0);
        for (size_t i = 0; i < nav.distances().size(); ++i)
            (*reachedBlocks)[i] = nav.distances()[i] != NavGrid::kUnreachable ? 1 : 0;
    }
    return nav.distanceAt(from) != NavGrid::kUnreachable;
}

LevelLayout generateSanctum(int levelIndex, uint32_t seed, ThreadPool& pool) {
    constexpr int kW = 448, kH = 256;
    LevelLayout out;
    out.world = std::make_unique<World>(kW, kH, seed);
    out.biome = BiomeId::Sanctum;
    out.levelIndex = levelIndex;
    out.seed = seed;
    World& w = *out.world;
    Rng rng(seed);

    const int floorY = 190, ceilY = 60, left = 30, right = kW - 30;
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            bool inside = x > left && x < right && y > ceilY && y < floorY;
            // Vaulted ceiling.
            float arch = 30.0f * std::sin(kPi * static_cast<float>(x - left) / static_cast<float>(right - left));
            if (y < ceilY + 1 + static_cast<int>(std::max(0.0f, 30.0f - arch))) inside = false;
            if (inside) continue;
            bool edge = x < 3 || y < 3 || x >= kW - 3 || y >= kH - 3;
            MatId m = edge ? mat::Bedrock : mat::Brick;
            Cell c = makeCell(m, hash2(x, y, seed));
            // Brick courses: darker mortar lines.
            bool mortar = (y % 6 == 0) || ((x + ((y / 6) % 2) * 6) % 12 == 0);
            c.var = static_cast<uint8_t>(mortar ? 20 : 120 + (hash2(x, y, 5u) & 63u));
            w.at(x, y) = c;
        }
    }
    // Rune trim along the floor.
    for (int x = left; x <= right; ++x) w.place(x, floorY, mat::Rune);
    // Healing font: a basin of Vitae.
    const int fontX = kW / 2;
    for (int x = fontX - 16; x <= fontX + 16; ++x) {
        for (int y = floorY - 7; y < floorY; ++y) {
            bool wall = x == fontX - 16 || x == fontX + 16 || y == floorY - 1;
            if (wall) w.place(x, y, mat::Rune);
            else if (y > floorY - 6) w.place(x, y, mat::Vitae);
        }
    }
    w.wakeAll();
    for (int i = 0; i < 30; ++i) w.step(pool);
    w.events().clear();

    out.playerStart = {static_cast<float>(left + 24), static_cast<float>(floorY - 1)};
    out.exitPos = {static_cast<float>(right - 24), static_cast<float>(floorY - 1)};
    // Shop pedestals (Spell), a perk altar (Altar) and a free potion.
    for (int i = 0; i < 4; ++i)
        out.spawns.push_back({SpawnKind::Spell, {static_cast<float>(left + 60 + i * 26), static_cast<float>(floorY - 1)}, 0.0f});
    for (int i = 0; i < 3; ++i)
        out.spawns.push_back({SpawnKind::Altar, {static_cast<float>(fontX + 50 + i * 28), static_cast<float>(floorY - 1)}, 0.0f});
    out.spawns.push_back({SpawnKind::Lantern, {static_cast<float>(left + 90), static_cast<float>(ceilY + 22)}, 0.0f});
    out.spawns.push_back({SpawnKind::Lantern, {static_cast<float>(right - 90), static_cast<float>(ceilY + 22)}, 0.0f});
    out.spawns.push_back({SpawnKind::Lantern, {static_cast<float>(fontX), static_cast<float>(ceilY + 2)}, 0.0f});
    (void)rng;
    return out;
}

} // namespace pm
