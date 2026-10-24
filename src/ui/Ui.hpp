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

/// Integer UI scale derived from the window height (1080p -> 2).
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

/// Spell icon with a frame coloured by spell type.
void spellIcon(SpellId spell, Rectangle r, bool highlight = false);
Color spellTypeColor(SpellId spell);
/// Tooltip box anchored near the mouse.
void tooltip(const std::string& title, const std::string& body, Color titleColor = color::kText);

} // namespace pm::ui
