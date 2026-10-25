#pragma once
// Immediate-mode UI helpers drawn at screen resolution: crisp pixel text, panels, buttons,
// bars, procedural spell icons and tooltips. Sizes scale with the window height.

#include <string>

#include "game/Spells.hpp"
#include "raylib.h"

namespace pm {
class Audio;
}

namespace pm::ui {

enum class Align { Left, Center, Right };

namespace color {
constexpr Color kText{236, 228, 214, 255};
constexpr Color kTextDim{160, 150, 140, 255};
constexpr Color kGold{255, 208, 90, 255};
constexpr Color kPanel{18, 14, 24, 225};
constexpr Color kPanelBorder{92, 76, 120, 255};
constexpr Color kAccent{170, 130, 255, 255};
constexpr Color kHealth{214, 52, 64, 255};
constexpr Color kMana{80, 150, 255, 255};
constexpr Color kDanger{255, 90, 80, 255};
} // namespace color

/// Integer UI scale derived from the window size (720p -> 2, 1080p -> 3).
void beginFrame(int screenW, int screenH);
int scale();
int screenW();
int screenH();
void setAudio(Audio* audio);

/// Font sizes are multiples of the 10px pixel font so glyphs stay crisp.
int fontSize(int step);
int measure(const std::string& text, int size);
void text(const std::string& text, float x, float y, int size, Color c, Align align = Align::Left, bool shadow = true);
/// Draws text wrapped to `width`; returns the height used.
float textWrapped(const std::string& text, float x, float y, float width, int size, Color c);

void panel(Rectangle r, Color fill = color::kPanel, Color border = color::kPanelBorder);
void bar(Rectangle r, float fraction, Color fill, Color back = Color{30, 24, 36, 230});
bool hovered(Rectangle r);
/// Button with hover feedback; returns true when clicked (or activated by keyboard focus).
bool button(Rectangle r, const std::string& label, bool enabled = true, bool focused = false);

/// Keyboard and gamepad navigation for a list of buttons: Up/Down (W/S, D-pad) move the
/// focus and Enter/Space (A) activates. The focus only appears once those are used, and
/// moving the mouse hands control back to it.
class NavList {
public:
    /// Call once per frame before drawing the list.
    void update(int count);
    bool focused(int index) const { return active_ && index == index_; }
    /// True when the focused entry was activated this frame.
    bool activated(int index) const { return focused(index) && confirm_; }

private:
    int index_ = 0;
    bool active_ = false;
    bool confirm_ = false;
};

/// "Back" in menus and panels: Esc or the gamepad's B button.
bool backPressed();
/// "Confirm" in menus: Enter or the gamepad's A button.
bool confirmPressed();

/// Spell icon with a frame coloured by spell type.
void spellIcon(SpellId spell, Rectangle r, bool highlight = false);
Color spellTypeColor(SpellId spell);
/// Tooltip box anchored near the mouse, drawn at endFrame() so later widgets never cover it.
void tooltip(const std::string& title, const std::string& body, Color titleColor = color::kText);
/// Finishes the UI for this frame (draws the pending tooltip). The App calls it after each render.
void endFrame();

} // namespace pm::ui
