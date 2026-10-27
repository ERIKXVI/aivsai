#include "gen/Vaults.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/Rng.hpp"
#include "sim/World.hpp"

namespace pm {

namespace {

/// Interior bounds and helpers shared by every vault builder.
struct Room {
    World& world;
    const VaultPlan& plan;
    Rng& rng;
    std::vector<SpawnPoint>& spawns;
    int ix0, iy0, ix1, iy1; ///< interior, inclusive; iy1 is the row standing on the floor

    Room(World& w, const VaultPlan& p, Rng& r, std::vector<SpawnPoint>& s)
        : world(w), plan(p), rng(r), spawns(s), ix0(p.x0 + kVaultWall), iy0(p.y0 + kVaultWall),
          ix1(p.x0 + p.w - 1 - kVaultWall), iy1(p.y0 + p.h - 1 - kVaultWall) {}

    int width() const { return ix1 - ix0 + 1; }
    int cx() const { return (ix0 + ix1) / 2; }
    /// x measured from the wall opposite the doorway (so set dressing sits away from it).
    int far(int offset) const { return plan.doorSide > 0 ? ix0 + offset : ix1 - offset; }
    /// x measured from the doorway wall.
    int near(int offset) const { return plan.doorSide > 0 ? ix1 - offset : ix0 + offset; }

    void set(int x, int y, MatId m) {
        if (world.inBounds(x, y) && world.at(x, y).mat != mat::Bedrock) world.place(x, y, m);
    }
    void clear(int x, int y) {
        if (world.inBounds(x, y) && world.at(x, y).mat != mat::Bedrock) world.clear(x, y);
    }
    void fill(int x0, int y0, int x1, int y1, MatId m) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) set(x, y, m);
    }
    void fillEmpty(int x0, int y0, int x1, int y1, MatId m) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                if (world.inBounds(x, y) && world.at(x, y).mat == mat::Empty) world.place(x, y, m);
    }

    /// Solid shell, hollow interior and the doorway.
    void shell(MatId wall) {
        fill(plan.x0, plan.y0, plan.x0 + plan.w - 1, plan.y0 + plan.h - 1, wall);
        for (int y = iy0; y <= iy1; ++y)
            for (int x = ix0; x <= ix1; ++x) clear(x, y);
        const int door = vaultDoorRow(plan);
        const int wx0 = plan.doorSide > 0 ? ix1 + 1 : plan.x0, wx1 = wx0 + kVaultWall - 1;
        for (int y = door - kVaultDoorHeight + 1; y <= door; ++y)
            for (int x = wx0; x <= wx1; ++x) clear(x, y);
    }

    void spawn(SpawnKind kind, int x, int y) {
        spawns.push_back({kind, {static_cast<float>(x), static_cast<float>(y)}, 1.0f});
    }
    void lantern(int x) { spawn(SpawnKind::Lantern, x, iy0); }

    /// Plank with two legs; returns the row just above the plank.
    int table(int x, int halfWidth, int height) {
        const int top = iy1 - height;
        fill(x - halfWidth, top, x + halfWidth, top, mat::Wood);
        fill(x - halfWidth + 1, top + 1, x - halfWidth + 1, iy1, mat::Wood);
        fill(x + halfWidth - 1, top + 1, x + halfWidth - 1, iy1, mat::Wood);
        return top - 1;
    }

    /// Wooden keg packed with gunpowder, standing on row `floorY`.
    void keg(int x, int floorY) {
        for (int y = floorY - 6; y <= floorY; ++y)
            for (int kx = x - 3; kx <= x + 3; ++kx) {
                const bool hoop = y == floorY - 6 || y == floorY || kx == x - 3 || kx == x + 3;
                set(kx, y, hoop ? mat::Wood : mat::Gunpowder);
            }
    }
};

void buildLaboratory(Room& r) {
    r.shell(mat::Brick);
    // Shelves of vials and potions against the far wall.
    const int shelfLen = 26;
    for (int level = 0; level < 3; ++level) {
        const int sy = r.iy1 - 9 - level * 9;
        const int a = r.far(0), b = r.far(shelfLen);
        r.fill(std::min(a, b), sy, std::max(a, b), sy, mat::Wood);
        for (int k = 2; k < shelfLen - 2; k += 3)
            if (r.rng.chance(0.55f)) r.set(r.far(k), sy - 1, mat::Glass);
        if (level < 2) r.spawn(SpawnKind::Potion, r.far(shelfLen - 4 - level * 8), sy - 1);
    }
    // The cauldron, brimming with something precious.
    const int cx = r.cx() + (r.plan.doorSide > 0 ? -4 : 4);
    r.fill(cx - 8, r.iy1, cx + 8, r.iy1, mat::Metal);
    r.fill(cx - 8, r.iy1 - 7, cx - 8, r.iy1, mat::Metal);
    r.fill(cx + 8, r.iy1 - 7, cx + 8, r.iy1, mat::Metal);
    const float roll = r.rng.uniform();
    const MatId brew = roll < 0.55f ? mat::Vitae : roll < 0.8f ? mat::Quicksilver : mat::Midas;
    r.fill(cx - 7, r.iy1 - 6, cx + 7, r.iy1 - 1, brew);
    // A worktable with a rare spell left on it, set back so the doorway stays clear.
    const int tx = r.near(22);
    r.spawn(SpawnKind::RareSpell, tx, r.table(tx, 7, 5));
    r.lantern(r.cx() - r.width() / 4);
    r.lantern(r.cx() + r.width() / 4);
}

void buildMagazine(Room& r) {
    r.shell(mat::Brick);
    // A wooden lining: one stray spark and the whole store goes up.
    for (int y = r.iy0; y <= r.iy1; ++y) {
        r.set(r.ix0, y, mat::Wood);
        r.set(r.ix1, y, mat::Wood);
    }
    r.fill(r.ix0, r.iy0, r.ix1, r.iy0, mat::Wood);
    const int door = vaultDoorRow(r.plan);
    for (int y = door - kVaultDoorHeight + 1; y <= door; ++y) r.clear(r.plan.doorSide > 0 ? r.ix1 : r.ix0, y);
    // Two tiers of kegs stacked from the far wall.
    const int base = std::clamp((r.width() - 34) / 8, 3, 8);
    for (int i = 0; i < base; ++i) r.keg(r.far(5 + i * 8), r.iy1);
    for (int i = 0; i < base - 2; ++i) r.keg(r.far(9 + i * 8), r.iy1 - 7);
    for (int i = 0; i < base - 4; ++i) r.keg(r.far(13 + i * 8), r.iy1 - 14);
    r.spawn(SpawnKind::Wand, r.far(5 + (base - 1) * 4), r.iy1 - 7 * std::max(1, base - 3) - 1);
    r.spawn(SpawnKind::Chest, r.near(14), r.iy1);
    // Spilled powder on the floor.
    for (int k = 0; k < 18; ++k) r.set(r.near(20) + r.rng.rangeInt(-6, 6), r.iy1, mat::Gunpowder);
    r.lantern(r.cx());
}

void buildCrypt(Room& r) {
    r.shell(mat::Brick);
    const int door = vaultDoorRow(r.plan);
    // A ledge inside the door, then a drowned hall below.
    const int ledge0 = r.near(0), ledge1 = r.near(9);
    r.fill(std::min(ledge0, ledge1), door + 1, std::max(ledge0, ledge1), door + 2, mat::Brick);
    r.spawn(SpawnKind::Potion, r.near(5), door);
    // Sarcophagi on the floor.
    for (int i = 0; i < 3; ++i) {
        const int sx = r.far(8 + i * 22);
        r.fill(sx - 6, r.iy1 - 4, sx + 6, r.iy1, mat::Basalt);
        r.fill(sx - 5, r.iy1 - 5, sx + 5, r.iy1 - 5, mat::Bone);
    }
    for (int k = 0; k < 40; ++k) {
        const int x = r.rng.rangeInt(r.ix0, r.ix1);
        if (r.world.at(x, r.iy1).mat == mat::Empty) r.set(x, r.iy1, mat::Bone);
    }
    r.spawn(SpawnKind::Chest, r.far(19), r.iy1);
    r.fillEmpty(r.ix0, door + 4, r.ix1, r.iy1, mat::Water);
    r.lantern(r.cx());
}

void buildTreasury(Room& r) {
    r.shell(mat::Brick);
    // Gold seams run through the walls.
    for (int y = r.plan.y0; y < r.plan.y0 + r.plan.h; ++y)
        for (int x = r.plan.x0; x < r.plan.x0 + r.plan.w; ++x) {
            const bool inside = x >= r.ix0 && x <= r.ix1 && y >= r.iy0 && y <= r.iy1;
            if (!inside && r.world.at(x, y).mat == mat::Brick && hash2(x / 3, y / 2, 0x601du) % 100 < 22) r.set(x, y, mat::GoldOre);
        }
    // Pillars and a runestone pedestal holding a wand.
    for (int f = 1; f <= 2; ++f) {
        const int px = r.ix0 + r.width() * f / 3;
        r.fill(px - 1, r.iy0, px + 1, r.iy0 + 6, mat::Brick);
    }
    r.fill(r.cx() - 4, r.iy1 - 3, r.cx() + 4, r.iy1, mat::Rune);
    r.spawn(SpawnKind::Wand, r.cx(), r.iy1 - 4);
    r.spawn(SpawnKind::Chest, r.far(6), r.iy1);
    r.spawn(SpawnKind::Chest, r.far(16), r.iy1);
    for (int k = 0; k < 5; ++k) {
        const int gx = k < 3 ? r.far(26 + k * 9) : r.near(18 + (k - 3) * 10);
        for (int dy = 0; dy < 3; ++dy) r.fill(gx - 3 + dy, r.iy1 - dy, gx + 3 - dy, r.iy1 - dy, mat::GoldDust);
    }
    r.spawn(SpawnKind::Guardian, r.cx() - 12, r.iy1);
    r.spawn(SpawnKind::Guardian, r.cx() + 12, r.iy1);
    r.lantern(r.cx() - r.width() / 3);
    r.lantern(r.cx() + r.width() / 3);
}

void buildSpring(Room& r) {
    // A natural grotto: rounded rock, moss, glowing vines and a pool of Vitae.
    const VaultPlan& p = r.plan;
    r.fill(p.x0, p.y0, p.x0 + p.w - 1, p.y0 + p.h - 1, mat::Rock);
    const float cx = static_cast<float>(r.ix0 + r.ix1) * 0.5f, cy = static_cast<float>(r.iy0 + r.iy1) * 0.5f;
    const float rx = static_cast<float>(r.width()) * 0.5f, ry = static_cast<float>(r.iy1 - r.iy0 + 1) * 0.5f + 2.0f;
    for (int y = r.iy0; y <= r.iy1; ++y)
        for (int x = r.ix0; x <= r.ix1; ++x) {
            const float dx = (static_cast<float>(x) - cx) / rx, dy = (static_cast<float>(y) - cy) / ry;
            if (dx * dx + dy * dy < 1.0f || y > r.iy1 - 8) r.clear(x, y);
        }
    const int door = vaultDoorRow(p);
    const int wx0 = p.doorSide > 0 ? r.ix1 - 4 : p.x0, wx1 = p.doorSide > 0 ? p.x0 + p.w - 1 : r.ix0 + 4;
    for (int y = door - kVaultDoorHeight + 1; y <= door; ++y)
        for (int x = wx0; x <= wx1; ++x) r.clear(x, y);
    // The pool basin in the middle of the floor.
    const int pool0 = static_cast<int>(cx) - 12, pool1 = static_cast<int>(cx) + 12;
    for (int x = pool0; x <= pool1; ++x) {
        const int depth = 5 - std::abs(x - static_cast<int>(cx)) / 3;
        for (int d = 0; d < std::max(1, depth); ++d) r.clear(x, r.iy1 + 1 + d);
        r.fill(x, r.iy1 + 1 + std::max(1, depth), x, r.iy1 + 1 + std::max(1, depth), mat::Rock);
        for (int d = 0; d < std::max(1, depth); ++d) r.set(x, r.iy1 + 1 + d, mat::Vitae);
    }
    // Moss on the floor, glowing vines from the ceiling, a few mushrooms.
    for (int x = r.ix0; x <= r.ix1; ++x) {
        for (int y = r.iy0 - 2; y <= r.iy1 + 1; ++y) {
            if (!r.world.inBounds(x, y + 1)) continue;
            const MatId here = r.world.at(x, y).mat, below = r.world.at(x, y + 1).mat;
            if (here == mat::Empty && below == mat::Rock) r.set(x, y + 1, mat::Moss);
        }
        if (r.rng.chance(0.25f)) {
            int y = r.iy0 - 2;
            while (y < r.iy1 && r.world.at(x, y).mat != mat::Empty) ++y;
            const int len = r.rng.rangeInt(3, 12);
            for (int k = 0; k < len && r.world.at(x, y + k).mat == mat::Empty; ++k) r.set(x, y + k, mat::GlowVine);
        }
    }
    r.spawn(SpawnKind::Potion, r.far(6), r.iy1);
    r.spawn(SpawnKind::RareSpell, r.near(10), r.iy1);
}

void buildLair(Room& r) {
    // A guardian's hall: an arena of old brick, too narrow at the door for its keeper to
    // leave, with ledges to fight from and a hoard on a runestone dais at the far end.
    r.shell(mat::Brick);
    for (int y = r.plan.y0; y < r.plan.y0 + r.plan.h; ++y)
        for (int x = r.plan.x0; x < r.plan.x0 + r.plan.w; ++x) {
            const bool inside = x >= r.ix0 && x <= r.ix1 && y >= r.iy0 && y <= r.iy1;
            if (!inside && r.world.at(x, y).mat == mat::Brick && hash2(x / 4, y / 3, 0x1a12u) % 100 < 14) r.set(x, y, mat::Rune);
        }
    // Bones of the fallen litter the floor.
    for (int x = r.ix0 + 4; x <= r.ix1 - 4; ++x)
        if (hash2(x, r.iy1, 0xb0e5u) % 100 < 18) r.set(x, r.iy1, mat::Bone);
    // Two ledges for the alchemist, high enough for the tallest guardian (31 cells) to pass
    // beneath, low enough to reach with a jump and some levitation.
    const int ledgeY = r.iy1 - 36;
    for (int f = 1; f <= 2; ++f) {
        const int lx = r.ix0 + r.width() * f / 3;
        r.fill(lx - 10, ledgeY, lx + 10, ledgeY + 1, mat::Brick);
    }
    // The hoard.
    r.fill(std::min(r.far(2), r.far(30)), r.iy1 - 3, std::max(r.far(2), r.far(30)), r.iy1, mat::Rune);
    r.spawn(SpawnKind::Wand, r.far(16), r.iy1 - 4);
    r.spawn(SpawnKind::Chest, r.far(7), r.iy1 - 4);
    r.spawn(SpawnKind::Chest, r.far(25), r.iy1 - 4);
    for (int k = 0; k < 3; ++k) {
        const int gx = r.far(38 + k * 10);
        for (int dy = 0; dy < 3; ++dy) r.fill(gx - 3 + dy, r.iy1 - dy, gx + 3 - dy, r.iy1 - dy, mat::GoldDust);
    }
    r.spawn(SpawnKind::LairGuardian, r.cx(), r.iy1);
    r.lantern(r.ix0 + r.width() / 5);
    r.lantern(r.cx());
    r.lantern(r.ix1 - r.width() / 5);
}

} // namespace

void vaultSize(VaultKind kind, int& w, int& h) {
    switch (kind) {
    case VaultKind::Laboratory: w = 104; h = 46; return;
    case VaultKind::Magazine: w = 90; h = 42; return;
    case VaultKind::Crypt: w = 86; h = 62; return;
    case VaultKind::Treasury: w = 108; h = 42; return;
    case VaultKind::Spring: w = 96; h = 44; return;
    case VaultKind::Lair: w = 132; h = 64; return;
    case VaultKind::Count: break;
    }
    w = h = 0;
}

int vaultDoorRow(const VaultPlan& plan) {
    const int interiorTop = plan.y0 + kVaultWall;
    const int floorRow = plan.y0 + plan.h - 1 - kVaultWall;
    return plan.kind == VaultKind::Crypt ? interiorTop + kVaultDoorHeight + 1 : floorRow;
}

VaultKind pickVaultKind(BiomeId biome, Rng& rng, uint32_t usedMask) {
    // Weights: Laboratory, Magazine, Crypt, Treasury, Spring (Lair: never at random).
    std::array<float, static_cast<size_t>(VaultKind::Count)> w{};
    switch (biome) {
    case BiomeId::Nigredo: w = {3, 2, 3, 1, 3}; break;
    case BiomeId::Albedo: w = {3, 1, 2, 2, 3}; break;
    case BiomeId::Citrinitas: w = {2, 4, 1, 3, 1}; break;
    case BiomeId::Rubedo: w = {2, 2, 1, 3, 1}; break;
    default: w = {2, 1, 0, 3, 0}; break;
    }
    float total = 0.0f;
    for (size_t i = 0; i < w.size(); ++i) {
        if (usedMask & (1u << i)) w[i] = 0.0f;
        total += w[i];
    }
    if (total <= 0.0f) return VaultKind::Laboratory;
    float pick = rng.range(0.0f, total);
    for (size_t i = 0; i < w.size(); ++i) {
        pick -= w[i];
        if (pick <= 0.0f && w[i] > 0.0f) return static_cast<VaultKind>(i);
    }
    return VaultKind::Laboratory;
}

void buildVault(World& world, const VaultPlan& plan, Rng& rng, std::vector<SpawnPoint>& spawns) {
    Room room(world, plan, rng, spawns);
    switch (plan.kind) {
    case VaultKind::Laboratory: buildLaboratory(room); break;
    case VaultKind::Magazine: buildMagazine(room); break;
    case VaultKind::Crypt: buildCrypt(room); break;
    case VaultKind::Treasury: buildTreasury(room); break;
    case VaultKind::Spring: buildSpring(room); break;
    case VaultKind::Lair: buildLair(room); break;
    case VaultKind::Count: break;
    }
}

} // namespace pm
