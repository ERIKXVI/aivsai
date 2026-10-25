// Tests for the GL-free parts of the renderer: the light budget.
#include <algorithm>
#include <vector>

#include "render/LightBudget.hpp"
#include "tests/TestFramework.hpp"

using namespace pm;

namespace {

constexpr float kViewDiag = 550.0f; // roughly a 480x270 cell view

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
            selector.select(frameLights, budget, kViewDiag, 1.0f / 60.0f);
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
        selector.select(lights, 8, kViewDiag, dt);
    }
    CHECK(selector.fade(lantern) == 1.0f);

    // Eight brighter lights now claim every fresh slot: the lantern keeps a fading slot and dims.
    float last = 1.0f;
    int framesLit = 0;
    for (int frame = 0; frame < 30; ++frame) {
        std::vector<PointLight> lights{lanternLight};
        for (int i = 0; i < 8; ++i) lights.push_back(PointLight{{10.0f * static_cast<float>(i), 0}, 2, 2, 2, 100.0f, 0.0f, 50.0f});
        selector.select(lights, 8, kViewDiag, dt);
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
        selector.select(lights, 8, kViewDiag, dt);
    }
    CHECK(selector.trackedCount() < 10);
    selector.reset();
    CHECK(selector.trackedCount() == 0);
}
