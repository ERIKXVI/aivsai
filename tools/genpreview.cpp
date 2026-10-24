// Developer tool: generates levels and writes full-size PNG previews (no window needed).
//   pm_genpreview <outdir> [seed]
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <string>

#include "core/ThreadPool.hpp"
#include "gen/LevelGenerator.hpp"
#include "raylib.h"
#include "render/WorldRenderer.hpp"
#include "sim/World.hpp"

int main(int argc, char** argv) {
    using namespace pm;
    std::string outDir = argc > 1 ? argv[1] : ".";
    uint32_t seed = argc > 2 ? static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10)) : 1234u;
    initMaterials();
    ThreadPool pool;
    for (int level = 0; level < 5; ++level) {
        auto t0 = std::chrono::steady_clock::now();
        LevelLayout layout = generateLevel(biomeForDepth(level), level, seed + static_cast<uint32_t>(level), pool);
        auto t1 = std::chrono::steady_clock::now();
        const World& w = *layout.world;
        Image img = GenImageColor(w.width(), w.height(), Color{20, 16, 20, 255});
        auto* px = static_cast<Color*>(img.data);
        for (int y = 0; y < w.height(); ++y)
            for (int x = 0; x < w.width(); ++x) {
                const Cell& c = w.at(x, y);
                if (c.mat == mat::Empty) continue;
                Rgba col = cellColor(c, x, y, 0);
                float a = col.a / 255.0f;
                Color& d = px[y * w.width() + x];
                d.r = static_cast<unsigned char>(col.r * a + d.r * (1 - a));
                d.g = static_cast<unsigned char>(col.g * a + d.g * (1 - a));
                d.b = static_cast<unsigned char>(col.b * a + d.b * (1 - a));
            }
        for (const auto& s : layout.spawns) {
            Color c = s.kind == SpawnKind::GroundEnemy ? RED
                    : s.kind == SpawnKind::AirEnemy   ? MAGENTA
                    : s.kind == SpawnKind::Lantern    ? YELLOW
                                                      : GREEN;
            ImageDrawCircle(&img, static_cast<int>(s.pos.x), static_cast<int>(s.pos.y) - 3, 3, c);
        }
        ImageDrawCircle(&img, static_cast<int>(layout.playerStart.x), static_cast<int>(layout.playerStart.y) - 5, 6, SKYBLUE);
        ImageDrawCircle(&img, static_cast<int>(layout.exitPos.x), static_cast<int>(layout.exitPos.y) - 5, 6, WHITE);
        std::vector<uint8_t> reached;
        bool ok = pathExists(w, layout.playerStart, layout.exitPos, &reached);
        for (int by = 0; by < w.height() / 4; ++by)
            for (int bx = 0; bx < w.width() / 4; ++bx)
                if (reached[static_cast<size_t>(by) * (w.width() / 4) + bx]) {
                    Color& d = px[(by * 4 + 2) * w.width() + bx * 4 + 2];
                    d = Color{0, 255, 120, 255};
                }
        for (size_t i = 1; i < layout.mainPath.size(); ++i) {
            bool seg = pathExists(w, layout.mainPath[i - 1], layout.mainPath[i]);
            if (!seg) std::printf("  segment %zu (%.0f,%.0f)->(%.0f,%.0f) blocked\n", i, layout.mainPath[i - 1].x,
                                  layout.mainPath[i - 1].y, layout.mainPath[i].x, layout.mainPath[i].y);
        }
        std::string path = outDir + "/level" + std::to_string(level) + ".png";
        ExportImage(img, path.c_str());
        UnloadImage(img);
        std::printf("level %d (%s): gen %.0f ms, path %s, spawns %zu\n", level, biomeDef(layout.biome).name,
                    std::chrono::duration<double, std::milli>(t1 - t0).count(), ok ? "OK" : "BLOCKED", layout.spawns.size());
    }
    return 0;
}
