#include "ui/HelpPanel.hpp"

#include <algorithm>
#include <iterator>

#include "app/App.hpp"
#include "ui/Ui.hpp"

namespace pm {

namespace {

struct Binding {
    const char* keys;
    const char* action;
};

// Controls as a two-column key / action table, read left to right.
constexpr Binding kKeyboard[] = {
    {"A / D", "move"},                       {"Right click", "pour the flask"},
    {"W / Space", "jump, hold to levitate"}, {"Q (hold)", "absorb into the flask"},
    {"Mouse", "aim"},                        {"F", "drink from the flask"},
    {"Left click", "cast"},                  {"E", "pick up, open, buy"},
    {"1-4 / wheel", "switch wand"},          {"M", "map"},
    {"Tab", "edit wands"},                   {"Esc", "pause"},
};
constexpr Binding kGamepad[] = {
    {"Left stick", "move"},          {"LT", "pour the flask"},
    {"A", "jump, hold to levitate"}, {"Y (hold)", "absorb into the flask"},
    {"Right stick", "aim"},          {"X", "drink from the flask"},
    {"RT", "cast"},                  {"B", "pick up, open, buy"},
    {"LB / RB", "switch wand"},      {"D-pad down", "map"},
    {"Back", "edit wands"},          {"Start", "pause"},
};
static_assert(std::size(kKeyboard) == std::size(kGamepad), "every action has a key and a button");

struct Tip {
    const char* title;
    const char* body;
};

// The rules of matter, one short paragraph each.
constexpr Tip kTips[] = {
    {"Matter", "Every pixel is simulated. Fire spreads through wood, oil and gunpowder; water cools lava into obsidian; "
               "acid eats stone. Blast gold ore and walk over the dust to collect it."},
    {"Wands", "Spells cast in order. Modifiers boost the next projectile, multicasts fire several at once and triggers "
              "release the next spell where they land."},
    {"Flask", "Carry any liquid or powder. Douse fires, dissolve walls, set oil traps - or drink: Vitae heals, "
              "quicksilver lends haste and ichor fury. Pour what a transmutation circle craves into it to earn a perk."},
    {"The Work", "Find the portal at the bottom of each stage: the violet arrow beside you follows the way, and the map "
                 "marks the portal from the start. Sanctums between stages heal you and sell spells. Vaults hide "
                 "treasure off the route; rest the cursor on anything to learn what it is."},
};

} // namespace

bool drawHelpPanel(App& app, bool gamepad) {
    const float sw = static_cast<float>(app.screenWidth()), sh = static_cast<float>(app.screenHeight());
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale());
    const int small = ui::smallFontSize();
    const float margin = 12.0f * s, header = 30.0f * s; // shared by the measuring pass and the drawing
    const auto& bindings = gamepad ? kGamepad : kKeyboard;

    const float rowH = static_cast<float>(small) * 1.45f;
    const float tableH = rowH * static_cast<float>((std::size(bindings) + 1) / 2) + 8.0f * s;

    // Size the panel from its content: at small UI scales the body font does not shrink, so the
    // panel widens (fewer wrapped lines) and grows instead of letting text run over the Back button.
    auto contentHeight = [&](float width) {
        float h = header + tableH;
        for (const Tip& tip : kTips)
            h += static_cast<float>(size) + 1.0f * s + ui::wrappedHeight(tip.body, width - 2.0f * margin, small) + 4.0f * s;
        return h + 28.0f * s; // room for the Back button
    };
    float width = 420.0f * s;
    float height = std::max(200.0f * s, contentHeight(width));
    if (height > sh - 16.0f * s) {
        width = std::max(width, std::min(sw - 16.0f * s, 640.0f * s));
        height = std::max(200.0f * s, contentHeight(width));
    }
    height = std::min(height, sh - 8.0f * s);

    ui::dimScreen(150);
    const Rectangle r{sw * 0.5f - width * 0.5f, sh * 0.5f - height * 0.5f, width, height};
    ui::panel(r);
    ui::text("HOW TO PLAY", sw * 0.5f, r.y + 8.0f * s, ui::fontSize(ui::scale() + 1), ui::color::kGold, ui::Align::Center);

    const float colW = (r.width - 2.0f * margin) * 0.5f, keyW = 62.0f * s;
    float y = r.y + header;
    for (size_t i = 0; i < std::size(bindings); ++i) {
        const float cx = r.x + margin + static_cast<float>(i % 2) * colW, cy = y + static_cast<float>(i / 2) * rowH;
        ui::text(bindings[i].keys, cx + keyW, cy, small, ui::color::kGold, ui::Align::Right);
        ui::text(bindings[i].action, cx + keyW + 8.0f * s, cy, small, ui::color::kText);
    }
    y += tableH;

    for (const Tip& tip : kTips) {
        ui::text(tip.title, r.x + margin, y, size, ui::color::kAccent);
        y += static_cast<float>(size) + 1.0f * s;
        y += ui::textWrapped(tip.body, r.x + margin, y, r.width - 2.0f * margin, small, ui::color::kTextDim) + 4.0f * s;
    }
    return ui::button({sw * 0.5f - 50.0f * s, r.y + r.height - 24.0f * s, 100.0f * s, 16.0f * s}, "Back") || ui::backPressed();
}

} // namespace pm
