#include "ui/Ui.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

#include "audio/Audio.hpp"

namespace pm::ui {

namespace {

int g_scale = 2;
int g_screenW = 1280, g_screenH = 720;
Audio* g_audio = nullptr;
const void* g_lastHover = nullptr;
Rectangle g_hoverRect{};

bool sameRect(Rectangle a, Rectangle b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

} // namespace

void beginFrame(int screenW, int screenH) {
    g_screenW = screenW;
    g_screenH = screenH;
    g_scale = std::max(1, static_cast<int>(std::floor(static_cast<float>(screenH) / 400.0f + 0.25f)));
}

int scale() { return g_scale; }
int screenW() { return g_screenW; }
int screenH() { return g_screenH; }
void setAudio(Audio* audio) { g_audio = audio; }

int fontSize(int step) { return 10 * std::max(1, step); }

int measure(const std::string& s, int size) { return MeasureText(s.c_str(), size); }

void text(const std::string& s, float x, float y, int size, Color c, Align align, bool shadow) {
    int w = measure(s, size);
    if (align == Align::Center) x -= static_cast<float>(w) * 0.5f;
    else if (align == Align::Right) x -= static_cast<float>(w);
    int ix = static_cast<int>(std::round(x)), iy = static_cast<int>(std::round(y));
    if (shadow) {
        int off = std::max(1, size / 10);
        DrawText(s.c_str(), ix + off, iy + off, size, Color{0, 0, 0, static_cast<unsigned char>(c.a * 0.7f)});
    }
    DrawText(s.c_str(), ix, iy, size, c);
}

float textWrapped(const std::string& s, float x, float y, float width, int size, Color c) {
    std::istringstream words(s);
    std::string word, line;
    float cy = y;
    const float lineH = static_cast<float>(size) * 1.25f;
    while (words >> word) {
        std::string candidate = line.empty() ? word : line + " " + word;
        if (measure(candidate, size) > width && !line.empty()) {
            text(line, x, cy, size, c);
            cy += lineH;
            line = word;
        } else {
            line = candidate;
        }
    }
    if (!line.empty()) {
        text(line, x, cy, size, c);
        cy += lineH;
    }
    return cy - y;
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

void bar(Rectangle r, float fraction, Color fill, Color back) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    DrawRectangleRec(r, back);
    DrawRectangleRec({r.x, r.y, r.width * fraction, r.height}, fill);
    Color hi{static_cast<unsigned char>(std::min(255, fill.r + 50)), static_cast<unsigned char>(std::min(255, fill.g + 50)),
             static_cast<unsigned char>(std::min(255, fill.b + 50)), fill.a};
    DrawRectangleRec({r.x, r.y, r.width * fraction, std::max(1.0f, r.height * 0.25f)}, hi);
    DrawRectangleLinesEx(r, 1.0f, Color{0, 0, 0, 160});
}

bool hovered(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }

bool button(Rectangle r, const std::string& label, bool enabled, bool focused) {
    const bool hover = enabled && (hovered(r) || focused);
    if (hover && !sameRect(g_hoverRect, r)) {
        g_hoverRect = r;
        if (g_audio && hovered(r)) g_audio->play(Sfx::UiHover, 0.5f);
    }
    if (!hovered(r) && sameRect(g_hoverRect, r)) g_hoverRect = Rectangle{};
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
    bool clicked = enabled && hovered(r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (clicked && g_audio) g_audio->play(Sfx::UiClick, 0.7f);
    (void)g_lastHover;
    return clicked;
}

Color spellTypeColor(SpellId spell) {
    switch (spellDef(spell).type) {
    case SpellType::Projectile: return Color{90, 140, 230, 255};
    case SpellType::Modifier: return Color{90, 200, 120, 255};
    case SpellType::Multicast: return Color{230, 190, 80, 255};
    }
    return WHITE;
}

void spellIcon(SpellId spell, Rectangle r, bool highlight) {
    const SpellDef& d = spellDef(spell);
    const float s = static_cast<float>(g_scale);
    Color frame = spellTypeColor(spell);
    DrawRectangleRec(r, Color{14, 12, 20, 240});
    DrawRectangleLinesEx(r, s, highlight ? WHITE : frame);
    if (spell == SpellId::None) return;

    const Vector2 c{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    const float u = r.width / 10.0f; // icon grid unit
    const uint32_t hex = d.proj.color;
    Color pc{static_cast<unsigned char>((hex >> 16) & 0xFF), static_cast<unsigned char>((hex >> 8) & 0xFF),
             static_cast<unsigned char>(hex & 0xFF), 255};

    if (d.type == SpellType::Projectile) {
        switch (d.proj.visual) {
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
            Vector2 pts[5] = {{c.x - 1 * u, c.y - 4 * u}, {c.x + 1.5f * u, c.y - u}, {c.x - 1.5f * u, c.y + 0.5f * u},
                              {c.x + 1.0f * u, c.y + 4 * u}, {c.x + 1.0f * u, c.y + 4 * u}};
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
        if (d.trigger) DrawCircleV({r.x + r.width - 2.2f * u, r.y + r.height - 2.2f * u}, u * 1.2f, Color{255, 120, 200, 255});
    } else {
        // Modifiers and multicasts use short glyph labels.
        const char* glyph = "?";
        switch (spell) {
        case SpellId::DamageUp: glyph = "+D"; break;
        case SpellId::SpeedUp: glyph = ">>"; break;
        case SpellId::Homing: glyph = "@"; break;
        case SpellId::Piercing: glyph = "->"; break;
        case SpellId::Bouncy: glyph = "~"; break;
        case SpellId::Explosive: glyph = "*"; break;
        case SpellId::FireTrail: glyph = "F"; break;
        case SpellId::WaterTrail: glyph = "W"; break;
        case SpellId::OilTrail: glyph = "O"; break;
        case SpellId::AcidTrail: glyph = "A"; break;
        case SpellId::Heavy: glyph = "v"; break;
        case SpellId::Weightless: glyph = "^"; break;
        case SpellId::Accuracy: glyph = "|"; break;
        case SpellId::Critical: glyph = "!"; break;
        case SpellId::Incendiary: glyph = "i"; break;
        case SpellId::Frost: glyph = "#"; break;
        case SpellId::Larger: glyph = "O+"; break;
        case SpellId::ManaRefund: glyph = "+M"; break;
        case SpellId::DoubleCast: glyph = "x2"; break;
        case SpellId::TripleCast: glyph = "x3"; break;
        case SpellId::QuadScatter: glyph = "x4"; break;
        case SpellId::Formation: glyph = "<|>"; break;
        default: break;
        }
        int size = std::max(10, static_cast<int>(r.height * 0.5f) / 10 * 10);
        text(glyph, c.x, c.y - static_cast<float>(size) * 0.5f, size, frame, Align::Center, true);
    }
}

void tooltip(const std::string& title, const std::string& body, Color titleColor) {
    const float s = static_cast<float>(g_scale);
    const int titleSize = fontSize(g_scale);
    const int bodySize = fontSize(std::max(1, g_scale - 1)) + (g_scale > 2 ? 0 : 0);
    const float width = 150.0f * s;
    // Estimate body height by wrapping into a dry run.
    std::istringstream words(body);
    std::string word, line;
    int lines = 0;
    std::vector<std::string> wrapped;
    while (words >> word) {
        std::string candidate = line.empty() ? word : line + " " + word;
        if (measure(candidate, bodySize) > width - 8.0f * s && !line.empty()) {
            wrapped.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
    }
    if (!line.empty()) wrapped.push_back(line);
    lines = static_cast<int>(wrapped.size());
    float h = static_cast<float>(titleSize) + 10.0f * s + static_cast<float>(lines) * static_cast<float>(bodySize) * 1.25f;
    Vector2 m = GetMousePosition();
    Rectangle r{m.x + 12.0f * s, m.y + 12.0f * s, width, h};
    if (r.x + r.width > static_cast<float>(g_screenW)) r.x = m.x - r.width - 6.0f * s;
    if (r.y + r.height > static_cast<float>(g_screenH)) r.y = static_cast<float>(g_screenH) - r.height - 4.0f;
    panel(r, Color{12, 10, 18, 245});
    text(title, r.x + 4.0f * s, r.y + 3.0f * s, titleSize, titleColor);
    float y = r.y + 6.0f * s + static_cast<float>(titleSize);
    for (const std::string& l : wrapped) {
        text(l, r.x + 4.0f * s, y, bodySize, color::kTextDim);
        y += static_cast<float>(bodySize) * 1.25f;
    }
}

} // namespace pm::ui
