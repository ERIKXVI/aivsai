#include "ui/Minimap.hpp"

#include <algorithm>
#include <cmath>

#include "game/Level.hpp"
#include "render/ColorUtil.hpp"
#include "sim/World.hpp"
#include "ui/Ui.hpp"

namespace pm {

namespace {

constexpr int kRevealRadius = 18;   ///< blocks revealed around the player
constexpr int kRevealInterval = 6;  ///< ticks between reveals
constexpr int kCornerW = 104, kCornerH = 68; ///< corner view, in blocks (= design units)

constexpr Color kOpen{14, 11, 20, 175};
constexpr Color kUnknown{0, 0, 0, 0};

Color shade(Rgba c, float k, uint8_t alpha) {
    return Color{static_cast<unsigned char>(static_cast<float>(c.r) * k), static_cast<unsigned char>(static_cast<float>(c.g) * k),
                 static_cast<unsigned char>(static_cast<float>(c.b) * k), alpha};
}

} // namespace

Minimap::~Minimap() {
    if (texture_.id != 0) UnloadTexture(texture_);
}

void Minimap::reset(const Level& level) {
    const World& world = level.world();
    bw_ = world.width() / kBlock;
    bh_ = world.height() / kBlock;
    explored_.assign(static_cast<size_t>(bw_) * bh_, 0);
    pixels_.assign(explored_.size(), kUnknown);
    if (texture_.id != 0) UnloadTexture(texture_);
    Image img{pixels_.data(), bw_, bh_, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    texture_ = LoadTextureFromImage(img);
    tick_ = 0;
}

bool Minimap::explored(int bx, int by) const {
    return bx >= 0 && by >= 0 && bx < bw_ && by < bh_ && explored_[static_cast<size_t>(by) * bw_ + bx] != 0;
}

bool Minimap::exploredNear(Vec2 p) const {
    const int bx = static_cast<int>(p.x) / kBlock, by = static_cast<int>(p.y) / kBlock;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (explored(bx + dx, by + dy)) return true;
    return false;
}

void Minimap::sample(const Level& level, int bx, int by) {
    // Classify the block from four of its cells: solid wins, then liquid, else open.
    const World& world = level.world();
    MatId solid = mat::Empty, liquid = mat::Empty;
    for (int k = 0; k < 4; ++k) {
        const int x = bx * kBlock + 1 + (k & 1) * 2, y = by * kBlock + 1 + (k >> 1) * 2;
        const MatId m = world.at(x, y).mat;
        if (isSolidForCollision(m)) solid = m;
        else if (phaseOf(m) == Phase::Liquid) liquid = m;
    }
    Color& px = pixels_[static_cast<size_t>(by) * bw_ + bx];
    if (solid != mat::Empty) px = shade(materialColor(solid), 0.62f, 255);
    else if (liquid != mat::Empty) px = shade(materialColor(liquid), 0.9f, 235);
    else px = kOpen;
}

void Minimap::update(const Level& level) {
    if (texture_.id == 0 || ++tick_ % kRevealInterval != 0) return;
    const Vec2 p = level.player().center();
    const int cx = static_cast<int>(p.x) / kBlock, cy = static_cast<int>(p.y) / kBlock;
    const int x0 = std::max(0, cx - kRevealRadius), x1 = std::min(bw_ - 1, cx + kRevealRadius);
    const int y0 = std::max(0, cy - kRevealRadius), y1 = std::min(bh_ - 1, cy + kRevealRadius);
    if (x0 > x1 || y0 > y1) return;
    for (int by = y0; by <= y1; ++by)
        for (int bx = x0; bx <= x1; ++bx) {
            const int dx = bx - cx, dy = by - cy;
            if (dx * dx + dy * dy > kRevealRadius * kRevealRadius) continue;
            explored_[static_cast<size_t>(by) * bw_ + bx] = 1;
            sample(level, bx, by);
        }
    // Upload just the touched rectangle.
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    scratch_.resize(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y)
        std::copy_n(&pixels_[static_cast<size_t>(y0 + y) * bw_ + x0], w, &scratch_[static_cast<size_t>(y) * w]);
    UpdateTextureRec(texture_, {static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(w), static_cast<float>(h)},
                     scratch_.data());
}

void Minimap::revealAll(const Level& level) {
    if (texture_.id == 0) return;
    std::fill(explored_.begin(), explored_.end(), uint8_t{1});
    for (int by = 0; by < bh_; ++by)
        for (int bx = 0; bx < bw_; ++bx) sample(level, bx, by);
    UpdateTexture(texture_, pixels_.data());
}

void Minimap::drawMarkers(const Level& level, Rectangle dest, Rectangle src, float time, float m) const {
    const float sx = dest.width / src.width, sy = dest.height / src.height;
    auto toScreen = [&](Vec2 world) {
        return Vector2{dest.x + (world.x / kBlock - src.x) * sx, dest.y + (world.y / kBlock - src.y) * sy};
    };
    auto inside = [&](Vector2 v) { return v.x >= dest.x && v.y >= dest.y && v.x <= dest.x + dest.width && v.y <= dest.y + dest.height; };

    for (const Rectf& v : level.layout().vaults) {
        if (!exploredNear({v.x + v.w * 0.5f, v.y + v.h * 0.5f}) && !exploredNear({v.x, v.y + v.h}) && !exploredNear({v.x + v.w, v.y + v.h}))
            continue;
        Vector2 a = toScreen({v.x, v.y}), b = toScreen({v.x + v.w, v.y + v.h});
        a.x = std::max(a.x, dest.x);
        a.y = std::max(a.y, dest.y);
        b.x = std::min(b.x, dest.x + dest.width);
        b.y = std::min(b.y, dest.y + dest.height);
        if (b.x > a.x && b.y > a.y) DrawRectangleLinesEx({a.x, a.y, b.x - a.x, b.y - a.y}, std::max(1.0f, m * 0.25f), Color{255, 200, 90, 220});
    }
    for (const Transmuter& t : level.transmuters()) {
        if (t.complete || !exploredNear(t.pos)) continue;
        const Vector2 s = toScreen(t.pos);
        if (inside(s)) DrawCircleV(s, m * 0.6f, Color{120, 230, 255, 230});
    }
    // A lair's guardian is known by rumour: marked from the start, so the fight can be sought.
    for (const auto& e : level.enemies()) {
        if (!e->guardian() || !e->alive()) continue;
        const Vector2 g = toScreen(e->body.pos);
        if (!inside(g)) continue;
        const float r = m * (0.75f + 0.1f * std::sin(time * 3.0f));
        DrawPoly(g, 3, r, 90.0f, Color{230, 70, 60, 240});
    }
    if (!level.isSanctum() && exploredNear(level.exitPos())) {
        const Vector2 s = toScreen(level.exitPos());
        if (inside(s)) {
            const float r = m * (0.9f + 0.15f * std::sin(time * 4.0f));
            DrawPoly(s, 4, r, 45.0f, Color{200, 140, 255, 255});
        }
    }
    const Vector2 s = toScreen(level.player().center());
    if (inside(s)) {
        DrawCircleV(s, m * 0.75f, Color{0, 0, 0, 200});
        DrawCircleV(s, m * 0.5f, std::fmod(time, 1.0f) < 0.75f ? WHITE : Color{255, 230, 160, 255});
    }
}

void Minimap::drawCorner(const Level& level, float screenW, float top, float time) const {
    if (texture_.id == 0) return;
    const float s = static_cast<float>(ui::scale());
    const float w = std::min(static_cast<float>(kCornerW), static_cast<float>(bw_)), h = std::min(static_cast<float>(kCornerH), static_cast<float>(bh_));
    const Vec2 p = level.player().center();
    // Window centred on the player, kept inside the map.
    Rectangle src{clamp(p.x / kBlock - w * 0.5f, 0.0f, static_cast<float>(bw_) - w), clamp(p.y / kBlock - h * 0.5f, 0.0f, static_cast<float>(bh_) - h), w, h};
    Rectangle dest{screenW - (w + 10.0f) * s, top, w * s, h * s};
    DrawRectangleRec({dest.x - s, dest.y - s, dest.width + 2.0f * s, dest.height + 2.0f * s}, Color{6, 5, 10, 150});
    DrawTexturePro(texture_, src, dest, {0.0f, 0.0f}, 0.0f, WHITE);
    DrawRectangleLinesEx({dest.x - s, dest.y - s, dest.width + 2.0f * s, dest.height + 2.0f * s}, 1.0f, Color{92, 76, 120, 200});
    drawMarkers(level, dest, src, time, 2.5f * s);
}

void Minimap::drawFull(const Level& level, float screenW, float screenH, float time) const {
    if (texture_.id == 0) return;
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale() + 1), small = ui::fontSize(std::max(1, ui::scale() - 1));
    DrawRectangle(0, 0, static_cast<int>(screenW), static_cast<int>(screenH), Color{0, 0, 0, 200});
    const float scale = std::min((screenH - 70.0f * s) / static_cast<float>(bh_), (screenW - 40.0f * s) / static_cast<float>(bw_));
    const float w = static_cast<float>(bw_) * scale, h = static_cast<float>(bh_) * scale;
    Rectangle dest{(screenW - w) * 0.5f, (screenH - h) * 0.5f + 6.0f * s, w, h};
    Rectangle src{0.0f, 0.0f, static_cast<float>(bw_), static_cast<float>(bh_)};
    DrawRectangleRec(dest, Color{4, 3, 8, 230});
    DrawTexturePro(texture_, src, dest, {0.0f, 0.0f}, 0.0f, WHITE);
    DrawRectangleLinesEx({dest.x - 2.0f, dest.y - 2.0f, dest.width + 4.0f, dest.height + 4.0f}, 2.0f, ui::color::kPanelBorder);
    drawMarkers(level, dest, src, time, std::max(3.0f * s, scale * 2.0f));

    ui::text("MAP", screenW * 0.5f, dest.y - static_cast<float>(size) - 6.0f * s, size, ui::color::kGold, ui::Align::Center);
    // Legend.
    const float lx = dest.x + dest.width + 12.0f * s;
    float ly = dest.y + 4.0f * s;
    auto legend = [&](Color c, const char* label) {
        DrawRectangleRec({lx, ly + static_cast<float>(small) * 0.25f, 6.0f * s, 6.0f * s}, c);
        ui::text(label, lx + 10.0f * s, ly, small, ui::color::kTextDim);
        ly += static_cast<float>(small) * 1.6f;
    };
    if (lx + 60.0f * s < screenW) {
        legend(WHITE, "You");
        legend(Color{200, 140, 255, 255}, "Portal");
        legend(Color{255, 200, 90, 255}, "Vault");
        legend(Color{230, 70, 60, 240}, "Guardian");
        legend(Color{120, 230, 255, 255}, "Circle");
    }
    ui::text("M / Esc to close", screenW * 0.5f, dest.y + dest.height + 6.0f * s, small, ui::color::kTextDim, ui::Align::Center);
}

} // namespace pm
