#include "ui/Ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "app/InputLatch.hpp"
#include "audio/Audio.hpp"
#include "game/Codex.hpp"
#include "render/ColorUtil.hpp"

namespace pm::ui {

namespace {

int g_scale = 2;
int g_screenW = 1280, g_screenH = 720;
Audio* g_audio = nullptr;
Rectangle g_hoverRect{};

/// The tooltip requested this frame; drawn by endFrame() above everything else.
struct PendingTooltip {
    bool active = false;
    std::string title, body;
    Color color{};
    bool anchored = false; ///< placed beside `anchor` instead of at the mouse
    Rectangle anchor{};
} g_tooltip;

bool sameRect(Rectangle a, Rectangle b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

} // namespace

void beginFrame(int screenW, int screenH) {
    g_screenW = screenW;
    g_screenH = screenH;
    g_tooltip.active = false;
    // Every screen is laid out for a 640x360 design area (480 wide at minimum), so the
    // scale is the largest integer that still fits it: 720p -> 2, 1080p -> 3, 1440p -> 4.
    g_scale = std::max(1, std::min(screenH / 360, screenW / 480));
}

int scale() { return g_scale; }
int screenW() { return g_screenW; }
int screenH() { return g_screenH; }
void setAudio(Audio* audio) { g_audio = audio; }

int fontSize(int step) { return 10 * std::max(1, step); }

// Two thirds of the body size, rounded down to the 10-pixel font steps: 10 px at 720p and 20 px
// at 1080p as before, but never proportionally larger than at 1080p, so small text stays inside
// boxes laid out in design units at 1440p and 4K.
int smallFontSize() { return fontSize(std::max(1, g_scale * 2 / 3)); }

int measure(const std::string& s, int size) { return MeasureText(s.c_str(), size); }

void text(const std::string& s, float x, float y, int size, Color c, Align align, bool shadow) {
    if (align != Align::Left) {
        const float w = static_cast<float>(measure(s, size));
        x -= align == Align::Center ? w * 0.5f : w;
    }
    int ix = static_cast<int>(std::round(x)), iy = static_cast<int>(std::round(y));
    if (shadow) {
        int off = std::max(1, size / 10);
        DrawText(s.c_str(), ix + off, iy + off, size, Color{0, 0, 0, static_cast<unsigned char>(c.a * 0.7f)});
    }
    DrawText(s.c_str(), ix, iy, size, c);
}

std::string formatClock(float seconds) {
    const int s = static_cast<int>(seconds);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

namespace {
/// Greedy word wrap: calls emit(line, offsetY) per line and returns the total height.
template <typename Emit> float wrapLines(const std::string& s, float width, int size, Emit&& emit) {
    std::istringstream words(s);
    std::string word, line;
    float dy = 0.0f;
    const float lineH = static_cast<float>(size) * 1.25f;
    while (words >> word) {
        std::string candidate = line.empty() ? word : line + " " + word;
        if (measure(candidate, size) > width && !line.empty()) {
            emit(line, dy);
            dy += lineH;
            line = word;
        } else {
            line = candidate;
        }
    }
    if (!line.empty()) {
        emit(line, dy);
        dy += lineH;
    }
    return dy;
}
} // namespace

float textWrapped(const std::string& s, float x, float y, float width, int size, Color c, Align align) {
    // Each line lines up within the box, so the anchor is its left edge, middle or right edge.
    const float anchor = align == Align::Center ? x + width * 0.5f : align == Align::Right ? x + width : x;
    return wrapLines(s, width, size, [&](const std::string& line, float dy) { text(line, anchor, y + dy, size, c, align); });
}

float wrappedHeight(const std::string& s, float width, int size) {
    return wrapLines(s, width, size, [](const std::string&, float) {});
}

void panel(Rectangle r, Color fill, Color border) {
    const float s = static_cast<float>(g_scale);
    DrawRectangleRec(r, fill);
    DrawRectangleLinesEx(r, s, border);
    // Corner studs for a slightly ornate alchemical look.
    const float k = s * 2.0f;
    DrawRectangleRec({r.x - s, r.y - s, k, k}, border);
    DrawRectangleRec({r.x + r.width - k + s, r.y - s, k, k}, border);
    DrawRectangleRec({r.x - s, r.y + r.height - k + s, k, k}, border);
    DrawRectangleRec({r.x + r.width - k + s, r.y + r.height - k + s, k, k}, border);
}

void bar(Rectangle r, float fraction, Color fill, Color back, float trail) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    DrawRectangleRec(r, back);
    if (trail > fraction)
        DrawRectangleRec({r.x + r.width * fraction, r.y, r.width * (std::min(trail, 1.0f) - fraction), r.height}, Color{255, 235, 200, 230});
    DrawRectangleRec({r.x, r.y, r.width * fraction, r.height}, fill);
    Color hi{static_cast<unsigned char>(std::min(255, fill.r + 50)), static_cast<unsigned char>(std::min(255, fill.g + 50)),
             static_cast<unsigned char>(std::min(255, fill.b + 50)), fill.a};
    DrawRectangleRec({r.x, r.y, r.width * fraction, std::max(1.0f, r.height * 0.25f)}, hi);
    DrawRectangleLinesEx(r, 1.0f, Color{0, 0, 0, 160});
}

void BarTrail::update(float fraction, float dt) {
    constexpr float kHold = 0.35f, kDrainPerSecond = 0.8f;
    if (fraction < last) hold = kHold; // a fresh hit holds the chip a moment
    last = fraction;
    if (shown < fraction) {
        shown = fraction; // heals and fresh bars snap
        return;
    }
    if (hold > 0.0f) hold -= dt;
    else shown = std::max(fraction, shown - kDrainPerSecond * dt);
}

void toast(const ToastLook& look, const std::string& header, const std::string& title, const std::string& detail, float cx,
           float top, float shown, float duration) {
    const float s = static_cast<float>(g_scale);
    const int size = fontSize(g_scale), small = smallFontSize();
    // Slides down from the top, holds, then fades.
    const float in = std::min(1.0f, shown / 0.25f), out = std::clamp((duration - shown) / 0.5f, 0.0f, 1.0f);
    const float a = std::min(in, out);
    const int widest = std::max({measure(header, small), measure(title, size), measure(detail, small)});
    const float w = static_cast<float>(widest) + 24.0f * s;
    const float h = static_cast<float>(small * 2 + size) + 14.0f * s;
    const Rectangle r{cx - w * 0.5f, top - (1.0f - in) * 12.0f * s, w, h};
    const auto al = [&](float k) { return static_cast<unsigned char>(255.0f * a * k); };
    DrawRectangleRec(r, Color{18, 13, 28, al(0.9f)});
    DrawRectangleLinesEx(r, s, withAlpha(look.frame, al(0.8f)));
    float y = r.y + 4.0f * s;
    text(header, cx, y, small, withAlpha(look.header, al(1.0f)), Align::Center);
    y += static_cast<float>(small) + 2.0f * s;
    text(title, cx, y, size, withAlpha(look.title, al(1.0f)), Align::Center);
    y += static_cast<float>(size) + 2.0f * s;
    text(detail, cx, y, small, Color{205, 195, 215, al(1.0f)}, Align::Center);
}

void discoveryToast(CodexEntry entry, float cx, float top, float shown, float duration) {
    static constexpr ToastLook kLook{Color{120, 214, 180, 255}, Color{196, 246, 226, 255}, color::kVerdigris};
    toast(kLook, "TRANSMUTATION DISCOVERED", codexFormula(entry), codexDef(entry).flavour, cx, top, shown, duration);
}

void dimScreen(unsigned char alpha) { DrawRectangle(0, 0, g_screenW, g_screenH, Color{0, 0, 0, alpha}); }

bool hovered(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }

bool button(Rectangle r, const std::string& label, bool enabled, bool focused) {
    // The hover sound follows the mouse only; keyboard focus just changes the highlight (and
    // NavList plays its own sound), so the two never fight over g_hoverRect.
    const bool mouseOver = enabled && hovered(r);
    const bool hover = mouseOver || (enabled && focused);
    if (mouseOver && !sameRect(g_hoverRect, r)) {
        g_hoverRect = r;
        if (g_audio) g_audio->play(Sfx::UiHover, 0.5f);
    }
    if (!mouseOver && sameRect(g_hoverRect, r)) g_hoverRect = Rectangle{};
    Color fill = hover ? Color{58, 42, 86, 240} : Color{28, 22, 38, 230};
    Color border = hover ? color::kAccent : color::kPanelBorder;
    if (!enabled) {
        fill = Color{24, 20, 28, 200};
        border = Color{60, 54, 66, 255};
    }
    panel(r, fill, border);
    int size = fontSize(g_scale);
    text(label, r.x + r.width * 0.5f, r.y + (r.height - static_cast<float>(size)) * 0.5f, size,
         enabled ? (hover ? WHITE : color::kText) : color::kTextDim, Align::Center);
    const bool clicked = mouseOver && input::mousePressed(MOUSE_BUTTON_LEFT);
    if (clicked && g_audio) g_audio->play(Sfx::UiClick, 0.7f);
    return clicked;
}

void NavList::update(int count, bool wrap) {
    confirm_ = false;
    if (count <= 0) return;
    const Vector2 delta = GetMouseDelta();
    if (delta.x * delta.x + delta.y * delta.y > 4.0f) active_ = false;
    const bool up = navUpPressed();
    const bool down = navDownPressed();
    if (up || down) {
        if (active_) index_ = wrap ? (index_ + (down ? 1 : -1) + count) % count : std::clamp(index_ + (down ? 1 : -1), 0, count - 1);
        active_ = true;
        if (g_audio) g_audio->play(Sfx::UiHover, 0.5f);
    }
    index_ = std::min(index_, count - 1);
    const bool confirm = input::keyPressed(KEY_ENTER) || input::keyPressed(KEY_SPACE) ||
                         input::padPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    if (confirm && active_) {
        confirm_ = true;
        if (g_audio) g_audio->play(Sfx::UiClick, 0.7f);
    } else if (input::padPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
        active_ = true; // a gamepad's first A press reveals the focus
    }
}

bool backPressed() { return input::keyPressed(KEY_ESCAPE) || input::padPressed(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT); }

bool confirmPressed() { return input::keyPressed(KEY_ENTER) || input::padPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN); }

namespace {
/// One menu direction: its arrow key, its WASD key or its D-pad button, pressed this frame.
bool navPressed(int arrow, int wasd, int dpad) {
    return input::keyPressed(arrow) || input::keyPressed(wasd) || input::padPressed(dpad);
}
} // namespace

bool navUpPressed() { return navPressed(KEY_UP, KEY_W, GAMEPAD_BUTTON_LEFT_FACE_UP); }
bool navDownPressed() { return navPressed(KEY_DOWN, KEY_S, GAMEPAD_BUTTON_LEFT_FACE_DOWN); }
bool navLeftPressed() { return navPressed(KEY_LEFT, KEY_A, GAMEPAD_BUTTON_LEFT_FACE_LEFT); }
bool navRightPressed() { return navPressed(KEY_RIGHT, KEY_D, GAMEPAD_BUTTON_LEFT_FACE_RIGHT); }

Color spellTypeColor(SpellId spell) {
    switch (spellDef(spell).type) {
    case SpellType::Projectile: return Color{90, 140, 230, 255};
    case SpellType::Modifier: return Color{90, 200, 120, 255};
    case SpellType::Multicast: return Color{230, 190, 80, 255};
    }
    return WHITE;
}

namespace {

/// Draws the icon's dark background and its frame: white when highlighted, otherwise the type colour `frame`.
void drawIconFrame(Rectangle r, Color frame, bool highlight) {
    const float s = static_cast<float>(g_scale);
    DrawRectangleRec(r, Color{14, 12, 20, 240});
    DrawRectangleLinesEx(r, s, highlight ? WHITE : frame);
}

/// Draws a projectile's in-flight look in its colour, centred on `c` and laid out in icon grid units `u`.
void drawProjectileGlyph(const ProjectileDef& proj, Vector2 c, float u) {
    const Color pc = hexColor(proj.color);
    switch (proj.visual) {
    case ProjVisual::Spark:
        DrawLineEx({c.x - 3 * u, c.y + 3 * u}, {c.x + 2 * u, c.y - 2 * u}, u * 1.2f, Color{pc.r, pc.g, pc.b, 160});
        DrawCircleV({c.x + 2 * u, c.y - 2 * u}, u * 1.3f, WHITE);
        break;
    case ProjVisual::Bolt:
        DrawLineEx({c.x - 3.5f * u, c.y + 3.5f * u}, {c.x + 3 * u, c.y - 3 * u}, u * 1.5f, pc);
        DrawCircleV({c.x + 3 * u, c.y - 3 * u}, u, WHITE);
        break;
    case ProjVisual::Orb:
        DrawCircleV(c, 2.5f * u, pc);
        DrawCircleV({c.x - u * 0.7f, c.y - u * 0.7f}, u * 0.8f, WHITE);
        break;
    case ProjVisual::BigOrb:
        DrawCircleV(c, 3.4f * u, pc);
        DrawRing(c, 3.4f * u, 3.9f * u, 0, 360, 24, Color{255, 255, 255, 120});
        DrawCircleV({c.x - u, c.y - u}, u, WHITE);
        break;
    case ProjVisual::Bomb:
        DrawCircleV({c.x, c.y + u}, 2.8f * u, Color{50, 46, 52, 255});
        DrawLineEx({c.x + u, c.y - u}, {c.x + 2.5f * u, c.y - 3 * u}, u * 0.8f, Color{160, 120, 80, 255});
        DrawCircleV({c.x + 2.7f * u, c.y - 3.2f * u}, u, pc);
        break;
    case ProjVisual::Shard:
        DrawTriangle({c.x, c.y - 3.5f * u}, {c.x - 1.6f * u, c.y + 2 * u}, {c.x + 1.6f * u, c.y + 2 * u}, pc);
        DrawLineEx({c.x, c.y - 3 * u}, {c.x, c.y + 1.5f * u}, u * 0.6f, WHITE);
        break;
    case ProjVisual::Saw:
        DrawPoly(c, 8, 3.4f * u, 0, pc);
        DrawCircleV(c, 1.4f * u, Color{30, 30, 36, 255});
        break;
    case ProjVisual::Beam: {
        const Vector2 pts[4] = {{c.x - 1 * u, c.y - 4 * u}, {c.x + 1.5f * u, c.y - u},
                                {c.x - 1.5f * u, c.y + 0.5f * u}, {c.x + 1.0f * u, c.y + 4 * u}};
        for (int i = 0; i < 3; ++i) DrawLineEx(pts[i], pts[i + 1], u * 1.1f, pc);
        break;
    }
    case ProjVisual::Glob:
        DrawCircleV({c.x, c.y + u}, 2.6f * u, pc);
        DrawTriangle({c.x, c.y - 3.5f * u}, {c.x - 2.2f * u, c.y}, {c.x + 2.2f * u, c.y}, pc);
        DrawCircleV({c.x - u, c.y + 0.3f * u}, u * 0.7f, Color{255, 255, 255, 180});
        break;
    case ProjVisual::Star:
        DrawPoly(c, 4, 3.6f * u, 45, pc);
        DrawPoly(c, 4, 3.6f * u, 0, pc);
        DrawCircleV(c, u, WHITE);
        break;
    }
}

/// The short label a modifier or multicast icon shows in place of a picture ("?" for a spell without one).
const char* spellGlyph(SpellId spell) {
    switch (spell) {
    case SpellId::DamageUp: return "+D";
    case SpellId::SpeedUp: return ">>";
    case SpellId::Homing: return "@";
    case SpellId::Piercing: return "->";
    case SpellId::Bouncy: return "~";
    case SpellId::Explosive: return "*";
    case SpellId::FireTrail: return "F";
    case SpellId::WaterTrail: return "W";
    case SpellId::OilTrail: return "O";
    case SpellId::AcidTrail: return "A";
    case SpellId::RimeTrail: return "=";
    case SpellId::Heavy: return "v";
    case SpellId::Weightless: return "^";
    case SpellId::Accuracy: return "|";
    case SpellId::Critical: return "!";
    case SpellId::Incendiary: return "i";
    case SpellId::Frost: return "#";
    case SpellId::Larger: return "O+";
    case SpellId::ManaRefund: return "+M";
    case SpellId::DoubleCast: return "x2";
    case SpellId::TripleCast: return "x3";
    case SpellId::QuadScatter: return "x4";
    case SpellId::Formation: return "<|>";
    default: break;
    }
    return "?";
}

} // namespace

void spellIcon(SpellId spell, Rectangle r, bool highlight) {
    const SpellDef& d = spellDef(spell);
    const Color frame = spell == SpellId::None ? Color{70, 64, 80, 255} : spellTypeColor(spell);
    drawIconFrame(r, frame, highlight);
    if (spell == SpellId::None) return;

    const Vector2 c{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    const float u = r.width / 10.0f; // icon grid unit
    if (d.type == SpellType::Projectile) {
        drawProjectileGlyph(d.proj, c, u);
        if (d.trigger) DrawCircleV({r.x + r.width - 2.2f * u, r.y + r.height - 2.2f * u}, u * 1.2f, Color{255, 120, 200, 255});
    } else {
        // Modifiers and multicasts use short glyph labels.
        const int size = std::max(10, static_cast<int>(r.height * 0.5f) / 10 * 10);
        text(spellGlyph(spell), c.x, c.y - static_cast<float>(size) * 0.5f, size, frame, Align::Center, true);
    }
}

namespace {

void drawTooltip(const std::string& title, const std::string& body, Color titleColor, const Rectangle* anchor) {
    const float s = static_cast<float>(g_scale);
    const int titleSize = fontSize(g_scale);
    const int bodySize = smallFontSize();
    const float width = 150.0f * s;
    const float bodyW = width - 8.0f * s;
    const float h = static_cast<float>(titleSize) + 10.0f * s + wrappedHeight(body, bodyW, bodySize);
    const Vector2 m = GetMousePosition();
    Rectangle r{m.x + 12.0f * s, m.y + 12.0f * s, width, h};
    if (anchor) {
        // Beside the control it explains, so it never covers the next one's label.
        r = {anchor->x + anchor->width + 6.0f * s, anchor->y, width, h};
        if (r.x + r.width > static_cast<float>(g_screenW)) r.x = anchor->x - r.width - 6.0f * s;
    } else if (r.x + r.width > static_cast<float>(g_screenW)) {
        r.x = m.x - r.width - 6.0f * s;
    }
    if (r.y + r.height > static_cast<float>(g_screenH)) r.y = static_cast<float>(g_screenH) - r.height - 4.0f;
    panel(r, Color{12, 10, 18, 245});
    text(title, r.x + 4.0f * s, r.y + 3.0f * s, titleSize, titleColor);
    textWrapped(body, r.x + 4.0f * s, r.y + 6.0f * s + static_cast<float>(titleSize), bodyW, bodySize, color::kTextDim);
}

} // namespace

void tooltip(const std::string& title, const std::string& body, Color titleColor) {
    g_tooltip = PendingTooltip{true, title, body, titleColor};
}

void tooltipBeside(Rectangle anchor, const std::string& title, const std::string& body, Color titleColor) {
    g_tooltip = PendingTooltip{true, title, body, titleColor, true, anchor};
}

void endFrame() {
    if (!g_tooltip.active) return;
    drawTooltip(g_tooltip.title, g_tooltip.body, g_tooltip.color, g_tooltip.anchored ? &g_tooltip.anchor : nullptr);
    g_tooltip.active = false;
}

} // namespace pm::ui
