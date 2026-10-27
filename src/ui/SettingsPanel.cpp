#include "ui/SettingsPanel.hpp"

#include <cmath>
#include <string>

#include "app/App.hpp"
#include "app/InputLatch.hpp"
#include "ui/Ui.hpp"

namespace pm {

namespace {

/// Keyboard and gamepad focus over the panel's rows: the three sliders, the toggles and Back.
ui::NavList g_nav;
constexpr int kRows = 3 + 6 + 1; ///< the sliders, the toggle rows (Fullscreen among them) and Back

/// Horizontal slider; returns true while the value changes. With the focus, left and right
/// step it by 5%.
bool slider(Rectangle r, const char* label, float& value, bool focused) {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    ui::text(label, r.x, r.y - static_cast<float>(small) - 2.0f * s, small, focused ? ui::color::kGold : ui::color::kTextDim);
    Rectangle track{r.x, r.y + r.height * 0.35f, r.width, r.height * 0.3f};
    ui::bar(track, value, ui::color::kAccent);
    float knobX = r.x + r.width * value;
    DrawRectangleRec({knobX - 2.0f * s, r.y, 4.0f * s, r.height}, WHITE);
    ui::text(TextFormat("%d%%", static_cast<int>(std::round(value * 100.0f))), r.x + r.width + 6.0f * s,
             r.y + r.height * 0.5f - static_cast<float>(small) * 0.5f, small, ui::color::kText);
    if (input::mouseDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), {r.x - 4.0f, r.y - 4.0f, r.width + 8.0f, r.height + 8.0f})) {
        value = clamp((GetMousePosition().x - r.x) / r.width, 0.0f, 1.0f);
        return true;
    }
    if (focused) {
        const int step = (ui::navRightPressed() ? 1 : 0) - (ui::navLeftPressed() ? 1 : 0);
        if (step != 0) {
            value = clamp(std::round(value * 20.0f + static_cast<float>(step)) / 20.0f, 0.0f, 1.0f);
            return true;
        }
    }
    return false;
}

} // namespace

bool drawSettingsPanel(App& app) {
    Settings& st = app.settings();
    const float sw = static_cast<float>(app.screenWidth()), sh = static_cast<float>(app.screenHeight());
    const float s = static_cast<float>(ui::scale());
    Rectangle panelR{sw * 0.5f - 110.0f * s, sh * 0.5f - 135.0f * s, 220.0f * s, 270.0f * s};
    ui::panel(panelR);
    ui::text("SETTINGS", sw * 0.5f, panelR.y + 8.0f * s, ui::fontSize(ui::scale() + 1), ui::color::kGold, ui::Align::Center);

    bool changed = false;
    g_nav.update(kRows, false);
    int row = 0;
    float x = panelR.x + 16.0f * s, w = panelR.width - 60.0f * s, y = panelR.y + 44.0f * s, h = 8.0f * s;
    changed |= slider({x, y, w, h}, "Master volume", st.masterVolume, g_nav.focused(row++));
    y += 26.0f * s;
    changed |= slider({x, y, w, h}, "Music volume", st.musicVolume, g_nav.focused(row++));
    y += 26.0f * s;
    changed |= slider({x, y, w, h}, "Effects volume", st.sfxVolume, g_nav.focused(row++));
    y += 20.0f * s;

    const float bw = panelR.width - 32.0f * s, bh = 16.0f * s;
    // One full-width button row at y: keeps its rect in `last`, advances y and returns whether it
    // was clicked or activated with the focus. toggle makes it a "Name: On/Off" row that flips
    // `flag` and returns the row's rect.
    Rectangle last{};
    auto pressRow = [&](const std::string& label) {
        last = {x, y, bw, bh};
        const int index = row++;
        y += bh + 4.0f * s;
        return ui::button(last, label, true, g_nav.focused(index)) || g_nav.activated(index);
    };
    auto toggle = [&](const char* name, bool& flag, const char* on = "On", const char* off = "Off") {
        if (pressRow(std::string(name) + ": " + (flag ? on : off))) {
            flag = !flag;
            changed = true;
        }
        return last;
    };

    toggle("Screen shake", st.screenShake);
    const bool fullscreen = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    if (pressRow(std::string("Fullscreen: ") + (fullscreen ? "On" : "Off"))) {
        app.toggleFullscreen();
        changed = true;
    }
    toggle("Show FPS", st.showFps);
    if (ui::hovered(toggle("Graphics", st.highQuality, "High", "Low")))
        ui::tooltip("Graphics", "Low shortens shadow rays and uses fewer lights, for weaker GPUs.");
    toggle("Hints", st.hints);
    if (ui::hovered(toggle("Matter lens", st.matterLens)))
        ui::tooltip("Matter lens", "Rest the cursor on a substance or creature to see what it is and what it does.");
    if (changed) app.applySettings();

    const int backRow = row++;
    bool back = ui::button({x, panelR.y + panelR.height - bh - 8.0f * s, bw, bh}, "Back", true, g_nav.focused(backRow)) ||
                g_nav.activated(backRow);
    if (back || ui::backPressed()) {
        saveSettings(st);
        return true;
    }
    return false;
}

} // namespace pm
