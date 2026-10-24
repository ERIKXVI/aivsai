#include "ui/SettingsPanel.hpp"

#include <cmath>
#include <string>

#include "app/App.hpp"
#include "app/InputLatch.hpp"
#include "ui/Ui.hpp"

namespace pm {

namespace {

/// Horizontal slider; returns true while the value changes.
bool slider(Rectangle r, const char* label, float& value) {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::fontSize(std::max(1, ui::scale() - 1));
    ui::text(label, r.x, r.y - static_cast<float>(small) - 2.0f * s, small, ui::color::kTextDim);
    Rectangle track{r.x, r.y + r.height * 0.35f, r.width, r.height * 0.3f};
    ui::bar(track, value, ui::color::kAccent);
    float knobX = r.x + r.width * value;
    DrawRectangleRec({knobX - 2.0f * s, r.y, 4.0f * s, r.height}, WHITE);
    ui::text(TextFormat("%d%%", static_cast<int>(std::round(value * 100.0f))), r.x + r.width + 6.0f * s,
             r.y + r.height * 0.5f - static_cast<float>(small) * 0.5f, small, ui::color::kText);
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), {r.x - 4.0f, r.y - 4.0f, r.width + 8.0f, r.height + 8.0f})) {
        value = clamp((GetMousePosition().x - r.x) / r.width, 0.0f, 1.0f);
        return true;
    }
    return false;
}

} // namespace

bool drawSettingsPanel(App& app) {
    Settings& st = app.settings();
    const float sw = static_cast<float>(app.screenWidth()), sh = static_cast<float>(app.screenHeight());
    const float s = static_cast<float>(ui::scale());
    Rectangle panelR{sw * 0.5f - 110.0f * s, sh * 0.5f - 115.0f * s, 220.0f * s, 230.0f * s};
    ui::panel(panelR);
    ui::text("SETTINGS", sw * 0.5f, panelR.y + 8.0f * s, ui::fontSize(ui::scale() + 1), ui::color::kGold, ui::Align::Center);

    bool changed = false;
    float x = panelR.x + 16.0f * s, w = panelR.width - 60.0f * s, y = panelR.y + 44.0f * s, h = 8.0f * s;
    changed |= slider({x, y, w, h}, "Master volume", st.masterVolume);
    y += 26.0f * s;
    changed |= slider({x, y, w, h}, "Music volume", st.musicVolume);
    y += 26.0f * s;
    changed |= slider({x, y, w, h}, "Effects volume", st.sfxVolume);
    y += 20.0f * s;

    float bw = panelR.width - 32.0f * s, bh = 16.0f * s;
    if (ui::button({x, y, bw, bh}, std::string("Screen shake: ") + (st.screenShake ? "On" : "Off"))) {
        st.screenShake = !st.screenShake;
        changed = true;
    }
    y += bh + 4.0f * s;
    const bool fullscreen = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    if (ui::button({x, y, bw, bh}, std::string("Fullscreen: ") + (fullscreen ? "On" : "Off"))) {
        app.toggleFullscreen();
        changed = true;
    }
    y += bh + 4.0f * s;
    if (ui::button({x, y, bw, bh}, std::string("Show FPS: ") + (st.showFps ? "On" : "Off"))) {
        st.showFps = !st.showFps;
        changed = true;
    }
    y += bh + 4.0f * s;
    if (ui::button({x, y, bw, bh}, std::string("Hints: ") + (st.hints ? "On" : "Off"))) {
        st.hints = !st.hints;
        changed = true;
    }
    if (changed) app.applySettings();

    bool back = ui::button({x, panelR.y + panelR.height - bh - 8.0f * s, bw, bh}, "Back");
    if (back || ui::backPressed()) {
        saveSettings(st);
        return true;
    }
    return false;
}

} // namespace pm
