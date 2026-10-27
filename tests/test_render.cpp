// Tests for the GL-free parts of the renderer: the light budget, the world layer and the sprite data.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/ThreadPool.hpp"
#include "render/LightBudget.hpp"
#include "render/Sprites.hpp"
#include "render/WorldRenderer.hpp"
#include "sim/World.hpp"
#include "tests/TestFramework.hpp"

using namespace pm;

namespace {

constexpr float kViewDiag = 550.0f; // roughly a 480x270 cell view
constexpr Rectf kViewArea{0.0f, 0.0f, 480.0f, 270.0f};

PointLight playerLight() { return PointLight{{240, 135}, 2.6f, 2.2f, 1.75f, 150.0f, 3.0f, 100.0f}; }

PointLight lavaLight() {
    return PointLight{{300, 200}, 3.0f, 1.2f, 0.3f, 120.0f, 2.5f, 3.0f, lightKey(LightKind::Emissive, 42)};
}

/// A fight's worth of projectiles, of varying colour and reach.
std::vector<PointLight> projectiles(int count) {
    std::vector<PointLight> out;
    for (int i = 0; i < count; ++i) {
        const float k = 0.6f + 0.05f * static_cast<float>(i % 23);
        out.push_back(PointLight{{100.0f + 5.0f * static_cast<float>(i), 120.0f}, k, k * 0.7f, k * 0.4f, 20.0f + static_cast<float>(i % 7) * 4.0f, 1.5f, 1.0f});
    }
    return out;
}

bool hasKey(const std::vector<PointLight>& lights, uint32_t key) {
    return std::any_of(lights.begin(), lights.end(), [key](const PointLight& l) { return l.key == key; });
}

} // namespace

TEST_CASE("light keys of different kinds never collide") {
    CHECK(lightKey(LightKind::Lantern, 5) != lightKey(LightKind::Emissive, 5));
    CHECK(lightKey(LightKind::Portal, 0) != 0);
    CHECK(lightKey(LightKind::Transmuter, 0x0FFFFFFFu) != lightKey(LightKind::Lantern, 0x0FFFFFFFu));
}

TEST_CASE("the light budget keeps lava lit through a swarm of projectiles") {
    std::vector<PointLight> lights = projectiles(40);
    lights.push_back(lavaLight()); // registered last, as emissive lights are
    selectLights(lights, 28, kViewDiag);
    CHECK(lights.size() == 28);
    CHECK(hasKey(lights, lavaLight().key));

    // The same holds through the fading selector, on High and on Low.
    for (int budget : {64, 28}) {
        LightSelector selector;
        for (int frame = 0; frame < 30; ++frame) {
            std::vector<PointLight> frameLights = projectiles(40);
            frameLights.push_back(lavaLight());
            selector.select(frameLights, budget, kViewDiag, kViewArea, 1.0f / 60.0f);
            CHECK(static_cast<int>(frameLights.size()) <= budget);
            CHECK(hasKey(frameLights, lavaLight().key));
        }
        CHECK(selector.fade(lavaLight().key) == 1.0f);
    }
}

TEST_CASE("a low light budget keeps the player and the highest scores") {
    std::vector<PointLight> lights = projectiles(40);
    lights.push_back(lavaLight());
    lights.push_back(playerLight());
    std::vector<float> scores;
    for (const PointLight& l : lights) scores.push_back(lightScore(l, kViewDiag));
    std::sort(scores.begin(), scores.end(), [](float a, float b) { return a > b; });

    selectLights(lights, 28, kViewDiag);
    CHECK(lights.size() == 28);
    CHECK(lights[0].priority == 100.0f); // the player
    CHECK(lights[1].key == lavaLight().key);
    for (size_t i = 0; i < lights.size(); ++i) CHECK(lightScore(lights[i], kViewDiag) == scores[i]);
    // Brighter and wider lights score higher at equal priority.
    PointLight dim = projectiles(1)[0], bright = dim;
    bright.r *= 2.0f;
    CHECK(lightScore(bright, kViewDiag) > lightScore(dim, kViewDiag));
}

TEST_CASE("a keyed light that loses its slot fades out instead of popping") {
    const uint32_t lantern = lightKey(LightKind::Lantern, 0);
    const PointLight lanternLight{{50, 50}, 1.6f, 1.1f, 0.55f, 85.0f, 4.0f, 3.0f, lantern};
    LightSelector selector;
    const float dt = 1.0f / 60.0f;
    for (int frame = 0; frame < 20; ++frame) {
        std::vector<PointLight> lights{lanternLight};
        selector.select(lights, 8, kViewDiag, kViewArea, dt);
    }
    CHECK(selector.fade(lantern) == 1.0f);

    // Eight brighter lights now claim every fresh slot: the lantern keeps a fading slot and dims.
    float last = 1.0f;
    int framesLit = 0;
    for (int frame = 0; frame < 30; ++frame) {
        std::vector<PointLight> lights{lanternLight};
        for (int i = 0; i < 8; ++i) lights.push_back(PointLight{{10.0f * static_cast<float>(i), 0}, 2, 2, 2, 100.0f, 0.0f, 50.0f});
        selector.select(lights, 8, kViewDiag, kViewArea, dt);
        CHECK(lights.size() <= 8);
        auto it = std::find_if(lights.begin(), lights.end(), [&](const PointLight& l) { return l.key == lantern; });
        if (it == lights.end()) continue;
        ++framesLit;
        CHECK(it->r < last * lanternLight.r + 1e-4f);
        last = it->r / lanternLight.r;
    }
    CHECK(framesLit >= 5 && framesLit <= 9); // ~1/7 s of fade
    CHECK(selector.fade(lantern) == 0.0f);
    CHECK(selector.trackedCount() == 0); // spent fades are pruned

    // Lights that leave the view are forgotten too, and a reset clears everything.
    for (uint32_t i = 0; i < 100; ++i) {
        std::vector<PointLight> lights{PointLight{{0, 0}, 1, 1, 1, 50.0f, 0.0f, 3.0f, lightKey(LightKind::Emissive, i)}};
        selector.select(lights, 8, kViewDiag, kViewArea, dt);
    }
    CHECK(selector.trackedCount() < 10);
    selector.reset();
    CHECK(selector.trackedCount() == 0);
}

namespace {

PointLight lanternAt(uint32_t id, float priority = 3.0f) {
    return PointLight{{50.0f + 10.0f * static_cast<float>(id), 50}, 1.6f, 1.1f, 0.55f, 85.0f, 4.0f, priority,
                      lightKey(LightKind::Lantern, id)};
}

/// `count` bright unkeyed lights that outrank every lantern.
void addBright(std::vector<PointLight>& lights, int count, float priority = 50.0f) {
    for (int i = 0; i < count; ++i) lights.push_back(PointLight{{5.0f * static_cast<float>(i), 0}, 2, 2, 2, 100.0f, 0.0f, priority});
}

const PointLight* findKey(const std::vector<PointLight>& lights, uint32_t key) {
    auto it = std::find_if(lights.begin(), lights.end(), [key](const PointLight& l) { return l.key == key; });
    return it == lights.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("a keyed light ranked inside the budget fades in and never flickers") {
    // Between N-4 and N candidates: all fit, so the lantern must climb to full and stay there.
    const int budget = 28;
    const float dt = 1.0f / 60.0f;
    for (int candidates = budget - 3; candidates <= budget; ++candidates) {
        LightSelector selector;
        float last = 0.0f;
        for (int frame = 0; frame < 40; ++frame) {
            std::vector<PointLight> lights;
            addBright(lights, candidates - 1);
            lights.push_back(lanternAt(0));
            selector.select(lights, budget, kViewDiag, kViewArea, dt);
            CHECK(static_cast<int>(lights.size()) == candidates);
            const PointLight* l = findKey(lights, lanternAt(0).key);
            REQUIRE(l != nullptr);
            CHECK(l->r >= last - 1e-5f);
            last = l->r;
        }
        CHECK(last == lanternAt(0).r);
        CHECK(selector.fade(lanternAt(0).key) == 1.0f);
    }
}

TEST_CASE("a lit lantern that slips a few ranks inside the budget stays at full brightness") {
    const int budget = 28;
    const float dt = 1.0f / 60.0f;
    LightSelector selector;
    auto frameLights = [&](int above) {
        std::vector<PointLight> lights;
        addBright(lights, above);
        lights.push_back(lanternAt(0));
        for (const PointLight& p : projectiles(10)) lights.push_back(p); // ranked below it
        return lights;
    };
    for (int frame = 0; frame < 20; ++frame) { // rank N-5
        std::vector<PointLight> lights = frameLights(budget - 5);
        selector.select(lights, budget, kViewDiag, kViewArea, dt);
    }
    CHECK(selector.fade(lanternAt(0).key) == 1.0f);
    for (int frame = 0; frame < 30; ++frame) { // rank N-3
        std::vector<PointLight> lights = frameLights(budget - 3);
        selector.select(lights, budget, kViewDiag, kViewArea, dt);
        CHECK(static_cast<int>(lights.size()) == budget);
        const PointLight* l = findKey(lights, lanternAt(0).key);
        CHECK(l != nullptr && l->r == lanternAt(0).r);
    }
}

TEST_CASE("fading lights take only the lowest slots and never displace a lit keyed light") {
    const int budget = 8;
    const float dt = 1.0f / 60.0f;
    LightSelector selector;
    for (int frame = 0; frame < 20; ++frame) {
        std::vector<PointLight> lights;
        for (uint32_t i = 0; i < 8; ++i) lights.push_back(lanternAt(i));
        selector.select(lights, budget, kViewDiag, kViewArea, dt);
    }

    // Eight brighter lights arrive: the lanterns fade out in the lowest kFadeSlots slots.
    {
        std::vector<PointLight> lights;
        addBright(lights, 8);
        for (uint32_t i = 0; i < 8; ++i) lights.push_back(lanternAt(i));
        LightSelector copy = selector;
        copy.select(lights, budget, kViewDiag, kViewArea, dt);
        CHECK(static_cast<int>(lights.size()) == budget);
        const auto fading = std::count_if(lights.begin(), lights.end(), [](const PointLight& l) { return l.key != 0; });
        CHECK(fading == LightSelector::kFadeSlots);
    }

    // Four brighter lights arrive: lanterns 0-3 keep the slots they hold, at full brightness,
    // and lanterns 4-7 fade out unseen rather than take those slots from them.
    for (int frame = 0; frame < 20; ++frame) {
        std::vector<PointLight> lights;
        addBright(lights, 4);
        for (uint32_t i = 0; i < 8; ++i) lights.push_back(lanternAt(i));
        selector.select(lights, budget, kViewDiag, kViewArea, dt);
        CHECK(static_cast<int>(lights.size()) == budget);
        for (uint32_t i = 0; i < 4; ++i) {
            const PointLight* l = findKey(lights, lanternAt(i).key);
            CHECK(l != nullptr && l->r == lanternAt(i).r);
        }
    }
    CHECK(selector.fade(lanternAt(7).key) == 0.0f);
}

TEST_CASE("a keyed light that stops registering fades out where it was") {
    const float dt = 1.0f / 60.0f;
    LightSelector selector;
    for (int frame = 0; frame < 20; ++frame) {
        std::vector<PointLight> lights{lanternAt(3)};
        selector.select(lights, 8, kViewDiag, kViewArea, dt);
    }
    float last = 1.0f;
    int framesLit = 0;
    for (int frame = 0; frame < 30; ++frame) {
        std::vector<PointLight> lights;
        addBright(lights, 2);
        selector.select(lights, 8, kViewDiag, kViewArea, dt);
        const PointLight* l = findKey(lights, lanternAt(3).key);
        if (!l) continue;
        ++framesLit;
        CHECK(l->pos.x == lanternAt(3).pos.x && l->pos.y == lanternAt(3).pos.y);
        CHECK(l->r < last * lanternAt(3).r);
        last = l->r / lanternAt(3).r;
    }
    CHECK(framesLit >= 5 && framesLit <= 9);
    CHECK(selector.trackedCount() == 0);
}

TEST_CASE("a lantern that leaves the view does not fade out in a visible light's slot") {
    const int budget = 8;
    const float dt = 1.0f / 60.0f;
    PointLight far = lanternAt(0);
    far.pos.x = 440.0f;
    LightSelector selector;
    for (int frame = 0; frame < 20; ++frame) {
        std::vector<PointLight> lights{far};
        for (uint32_t i = 1; i < 8; ++i) lights.push_back(lanternAt(i));
        selector.select(lights, budget, kViewDiag, kViewArea, dt);
    }
    CHECK(selector.fade(far.key) == 1.0f);

    // The view pans left: the far lantern no longer reaches it and a dim torch enters. The
    // budget is full of visible lights, so every one of them is drawn and the lantern is not.
    const Rectf panned{-200.0f, 0.0f, 480.0f, 270.0f};
    CHECK(!lightReaches(far, panned));
    const PointLight torch{{-150, 100}, 0.3f, 0.2f, 0.1f, 20.0f, 0.0f, 0.0f};
    for (int frame = 0; frame < 10; ++frame) {
        std::vector<PointLight> lights;
        for (uint32_t i = 1; i < 8; ++i) lights.push_back(lanternAt(i));
        lights.push_back(torch);
        selector.select(lights, budget, kViewDiag, panned, dt);
        CHECK(static_cast<int>(lights.size()) == budget);
        CHECK(findKey(lights, far.key) == nullptr);
        CHECK(std::any_of(lights.begin(), lights.end(), [&](const PointLight& l) { return l.pos.x == torch.pos.x; }));
        for (uint32_t i = 1; i < 8; ++i) {
            const PointLight* l = findKey(lights, lanternAt(i).key);
            CHECK(l != nullptr && l->r == lanternAt(i).r);
        }
    }
    CHECK(selector.trackedCount() == 7); // the far lantern's fade ran out unseen
}

namespace {

struct BlockSample {
    float x, y, weight;
};

/// The emissive blocks lying wholly inside a view of `w` x `h` cells at (ox, oy), keyed by
/// their world block, after checking each centroid lies in the block its index names.
std::vector<BlockSample> wholeBlocks(const WorldRenderer& wr, int ox, int oy, int w, int h) {
    constexpr int kShift = WorldRenderer::kBlockShift, kSize = 1 << kShift;
    std::vector<BlockSample> out;
    const std::vector<EmissiveBlock>& blocks = wr.emissiveBlocks();
    const int bw = wr.emissiveBlocksWide();
    for (size_t i = 0; i < blocks.size(); ++i) {
        const EmissiveBlock& b = blocks[i];
        if (b.weight <= 0.0f) continue;
        const int bx = (ox >> kShift) + static_cast<int>(i) % bw, by = (oy >> kShift) + static_cast<int>(i) / bw;
        CHECK(static_cast<int>(std::floor(b.x)) >> kShift == bx);
        CHECK(static_cast<int>(std::floor(b.y)) >> kShift == by);
        if (bx * kSize >= ox && by * kSize >= oy && (bx + 1) * kSize <= ox + w && (by + 1) * kSize <= oy + h)
            out.push_back({b.x, b.y, b.weight});
    }
    std::sort(out.begin(), out.end(), [](const BlockSample& a, const BlockSample& b) { return a.y < b.y || (a.y == b.y && a.x < b.x); });
    return out;
}

} // namespace

TEST_CASE("emissive blocks stay fixed to the world while the view pans and shakes") {
    initMaterials();
    ThreadPool pool(3);
    World world(256, 128, 7);
    for (int y = 60; y < 74; ++y)
        for (int x = 70; x < 170; ++x) world.place(x, y, mat::Lava);
    // A view of 150 x 90 cells (not a multiple of the block size) pans one cell per frame and
    // shakes by a cell either way, built in parallel and serially, with no GL context.
    constexpr int kW = 150, kH = 90;
    WorldRenderer wr;
    wr.resize(kW, kH);
    std::vector<BlockSample> reference;
    for (int frame = 0; frame < 24; ++frame) {
        const int ox = 40 + frame + (frame % 3 == 1 ? 1 : frame % 3 == 2 ? -1 : 0);
        const int oy = 20 + (frame % 2 == 0 ? 1 : -1);
        wr.build(world, ox, oy, 100, frame % 2 == 0 ? &pool : nullptr);
        wr.upload();
        // The lake's blocks from x=80 to 160 stay inside the view throughout.
        std::vector<BlockSample> inner;
        for (const BlockSample& b : wholeBlocks(wr, ox, oy, kW, kH))
            if (b.x >= 80.0f && b.x < 160.0f) inner.push_back(b);
        CHECK(inner.size() == 10); // two block rows of five
        if (frame == 0) reference = inner;
        CHECK(inner.size() == reference.size());
        for (size_t i = 0; i < std::min(inner.size(), reference.size()); ++i) {
            CHECK(inner[i].x == reference[i].x && inner[i].y == reference[i].y);
            CHECK(inner[i].weight == reference[i].weight);
        }
    }
}

TEST_CASE("rock gets a lit lip under open air and a shaded underside; timber stays flat") {
    initMaterials();
    World world(128, 64, 1);
    for (int y = 10; y < 20; ++y) {
        for (int x = 8; x < 32; ++x) world.place(x, y, mat::Rock);
        for (int x = 32; x < 56; ++x) world.place(x, y, mat::Wood);
    }
    WorldRenderer wr;
    wr.resize(128, 64);
    wr.build(world, 0, 0, 0);
    auto drawn = [&](int x, int y) { return wr.colors()[static_cast<size_t>(y) * 128 + x]; };
    auto base = [&](int x, int y) {
        const Cell& c = world.at(x, y);
        return materials().palette[c.mat][c.var];
    };
    auto sum = [](Rgba c) { return c.r + c.g + c.b; };
    auto sameRgb = [](Rgba a, Rgba b) { return a.r == b.r && a.g == b.g && a.b == b.b; }; // Rgba has no operator==
    for (int x = 8; x < 32; ++x) {
        CHECK(sum(drawn(x, 10)) > sum(base(x, 10)));
        CHECK(sum(drawn(x, 19)) < sum(base(x, 19)));
        CHECK(sameRgb(drawn(x, 15), base(x, 15)));
    }
    for (int x = 32; x < 56; ++x) {
        CHECK(sameRgb(drawn(x, 10), base(x, 10)));
        CHECK(sameRgb(drawn(x, 19), base(x, 19)));
    }
}

TEST_CASE("every sprite is registered once and drawn in the palette") {
    constexpr int kIds = static_cast<int>(SpriteId::Count);
    int count = 0;
    const SpriteSource* sources = spriteSources(count);
    CHECK(count == kIds);
    std::array<int, kIds> seen{};
    for (int s = 0; s < count; ++s) {
        const SpriteSource& src = sources[s];
        const int id = static_cast<int>(src.id);
        CHECK(id >= 0 && id < kIds);
        if (id < 0 || id >= kIds) continue;
        ++seen[static_cast<size_t>(id)];
        CHECK(src.w > 0 && src.h > 0 && src.frames > 0);
        for (int r = 0; r < src.h * src.frames; ++r) {
            const char* row = src.rows[r];
            CHECK(static_cast<int>(std::strlen(row)) == src.w);
            for (const char* c = row; *c; ++c) {
                bool emissive = false;
                CHECK(*c == '.' || spritePaletteColor(*c, emissive).a > 0);
            }
        }
    }
    for (int n : seen) CHECK(n == 1);
}

TEST_CASE("a sprite frame lists exactly its opaque pixels, and clamps its frame") {
    int count = 0;
    const SpriteSource* sources = spriteSources(count);
    const SpriteFrame* first = &spriteFrame(sources[0].id, 0);
    for (int s = 0; s < count; ++s) {
        const SpriteSource& src = sources[s];
        for (int f = 0; f < src.frames; ++f) {
            const SpriteFrame& fr = spriteFrame(src.id, f);
            CHECK(fr.w == src.w && fr.h == src.h);
            // '.' is the only transparent character (see the test above).
            size_t expected = 0;
            for (int y = 0; y < src.h; ++y) {
                const char* row = src.rows[f * src.h + y];
                for (int x = 0; x < src.w && row[x] != '\0'; ++x) expected += row[x] != '.';
            }
            CHECK(fr.pixels.size() == expected);
            for (const SpritePixel& p : fr.pixels) {
                CHECK(p.x >= 0 && p.x < src.w && p.y >= 0 && p.y < src.h);
                if (p.x < 0 || p.x >= src.w || p.y < 0 || p.y >= src.h) continue;
                bool emissive = false;
                const Color c = spritePaletteColor(src.rows[f * src.h + p.y][p.x], emissive);
                CHECK(c.r == p.color.r && c.g == p.color.g && c.b == p.color.b && c.a == p.color.a);
                CHECK(emissive == p.emissive);
            }
        }
    }
    for (int s = 0; s < count; ++s) {
        const SpriteSource& src = sources[s];
        CHECK(&spriteFrame(src.id, src.frames + 3) == &spriteFrame(src.id, src.frames - 1));
        CHECK(&spriteFrame(src.id, -1) == &spriteFrame(src.id, 0));
    }
    CHECK(&spriteFrame(sources[0].id, 0) == first); // references survive building every other sprite's frames
}
