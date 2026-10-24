#include "scenes/Hints.hpp"

#include <algorithm>

#include "game/Level.hpp"
#include "game/Run.hpp"
#include "ui/Ui.hpp"

namespace pm {

namespace {

constexpr float kShowTime = 7.0f;

struct HintText {
    const char* keyboard;
    const char* gamepad;
};

constexpr HintText kTexts[static_cast<int>(Hint::Count)] = {
    {"Hold Q to draw liquids and powders into your flask - right-click pours them out",
     "Hold Y to draw liquids and powders into your flask - LT pours them out"},
    {"A new spell is in your satchel - press Tab and drag it onto a wand",
     "A new spell is in your satchel - press Back and drag it onto a wand"},
    {"Wounded? Pink Vitae heals - absorb it with Q, then drink with F",
     "Wounded? Pink Vitae heals - absorb it with Y, then drink with X"},
    {"A sanctum: buy spells with E, then claim one perk at the altars",
     "A sanctum: buy spells with B, then claim one perk at the altars"},
    {"A transmutation circle - pour the matter it craves into it to earn a perk",
     "A transmutation circle - pour the matter it craves into it to earn a perk"},
    {"The arrow at the edge of the screen always points towards the portal",
     "The arrow at the edge of the screen always points towards the portal"},
    {"Gold ore crumbles into gold dust when blasted - walk over the dust to collect it",
     "Gold ore crumbles into gold dust when blasted - walk over the dust to collect it"},
};

int satchelCount(const RunState& run) {
    return static_cast<int>(std::count_if(run.inventory.begin(), run.inventory.end(), [](SpellId s) { return s != SpellId::None; }));
}

/// Looks for a cell matching `pred` in a square around `center`, sampling every other cell.
template <typename Pred>
bool nearby(const World& world, Vec2 center, int radius, Pred pred) {
    const int cx = floorToInt(center.x), cy = floorToInt(center.y);
    for (int y = cy - radius; y <= cy + radius; y += 2)
        for (int x = cx - radius; x <= cx + radius; x += 2)
            if (world.inBounds(x, y) && pred(world.at(x, y).mat)) return true;
    return false;
}

} // namespace

void HintTracker::reset() {
    active_ = Hint::Count;
    timer_ = 0.0f;
    baselineSpells_ = -1;
}

void HintTracker::show(Hint h) {
    active_ = h;
    timer_ = kShowTime;
    seen_ |= 1u << static_cast<unsigned>(h);
}

void HintTracker::update(const Level& level, const RunState& run, float dt) {
    if (baselineSpells_ < 0) baselineSpells_ = satchelCount(run);
    if (active_ != Hint::Count) {
        timer_ -= dt;
        if (timer_ <= 0.0f) active_ = Hint::Count;
        return;
    }
    const float t = level.time();
    if (t < 2.0f) return;
    const Player& player = level.player();
    if (level.isSanctum()) {
        if (!seen(Hint::Sanctum)) show(Hint::Sanctum);
        return;
    }
    // The opening controls card covers the first half minute of the first stage.
    if (run.levelIndex == 0 && t < 30.0f) return;

    if (!seen(Hint::Satchel) && satchelCount(run) > baselineSpells_) {
        show(Hint::Satchel);
        return;
    }
    if (!seen(Hint::Heal) && player.hp < player.maxHp * 0.4f) {
        show(Hint::Heal);
        return;
    }
    if (!seen(Hint::Transmuter)) {
        for (const Transmuter& tm : level.transmuters()) {
            if (!tm.complete && (tm.pos - player.center()).lengthSq() < 70.0f * 70.0f) {
                show(Hint::Transmuter);
                return;
            }
        }
    }
    if (!seen(Hint::Compass) && run.levelIndex == 0 && t > 75.0f) {
        show(Hint::Compass);
        return;
    }
    // World scans are throttled.
    scanTimer_ -= dt;
    if (scanTimer_ > 0.0f) return;
    scanTimer_ = 0.5f;
    const World& world = level.world();
    if (!seen(Hint::Absorb) && run.flask.amount < Flask::kCapacity / 2 &&
        nearby(world, player.center(), 14, [](MatId m) { return matDef(m).phase == Phase::Liquid; })) {
        show(Hint::Absorb);
        return;
    }
    if (!seen(Hint::Gold) && nearby(world, player.center(), 20, [](MatId m) { return m == mat::GoldOre; })) {
        show(Hint::Gold);
        return;
    }
}

void HintTracker::draw(float screenW, float screenH, bool gamepad) const {
    if (active_ == Hint::Count) return;
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale());
    const HintText& h = kTexts[static_cast<int>(active_)];
    const char* text = gamepad ? h.gamepad : h.keyboard;
    const float a = clamp(std::min((kShowTime - timer_) * 3.0f, timer_ * 1.5f), 0.0f, 1.0f);
    const float w = static_cast<float>(ui::measure(text, size)) + 20.0f * s, hgt = static_cast<float>(size) + 10.0f * s;
    Rectangle r{screenW * 0.5f - w * 0.5f, screenH - 64.0f * s - (1.0f - a) * 6.0f * s, w, hgt};
    DrawRectangleRec(r, Color{14, 10, 22, static_cast<unsigned char>(210.0f * a)});
    DrawRectangleLinesEx(r, 1.0f, Color{ui::color::kGold.r, ui::color::kGold.g, ui::color::kGold.b, static_cast<unsigned char>(150.0f * a)});
    ui::text(text, screenW * 0.5f, r.y + 5.0f * s, size, Color{240, 230, 210, static_cast<unsigned char>(255.0f * a)}, ui::Align::Center);
}

} // namespace pm
