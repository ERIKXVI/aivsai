// GameScene presentation: HUD, world overlays, compass, inventory (wand editing), pause
// menu, end screens, title card and debug overlay. Run flow lives in GameScene.cpp.

#include "scenes/GameScene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/App.hpp"
#include "app/InputLatch.hpp"
#include "core/Profiler.hpp"
#include "render/ColorUtil.hpp"
#include "scenes/MenuScene.hpp"
#include "ui/SettingsPanel.hpp"
#include "ui/Ui.hpp"

namespace pm {

namespace {

std::string formatTime(float seconds) {
    int s = static_cast<int>(seconds);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

/// The wand editor's info card: details of `shown`, or the rules of wands when nothing is
/// under the cursor.
void drawSpellCard(Rectangle card, SpellId shown) {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::fontSize(std::max(1, ui::scale() - 1));
    ui::panel(card, Color{12, 9, 18, 230});
    const float cx = card.x + 8.0f * s, cw = card.width - 16.0f * s;
    float cy = card.y + 8.0f * s;
    if (shown != SpellId::None) {
        const SpellDef& d = spellDef(shown);
        ui::spellIcon(shown, {cx, cy, 24.0f * s, 24.0f * s});
        ui::text(d.name, cx + 30.0f * s, cy + 2.0f * s, small, ui::spellTypeColor(shown));
        static constexpr const char* kTypeNames[] = {"Projectile", "Modifier", "Multicast"};
        ui::text(kTypeNames[static_cast<int>(d.type)], cx + 30.0f * s, cy + 4.0f * s + static_cast<float>(small), small,
                 ui::color::kTextDim);
        cy += 32.0f * s;
        ui::textWrapped(spellDetails(shown), cx, cy, cw, small, ui::color::kText);
        return;
    }
    ui::text("The art of wands", cx, cy, small, ui::color::kGold);
    cy += static_cast<float>(small) + 6.0f * s;
    static constexpr const char* kRules[] = {
        "Spells cast from left to right; when the deck runs out the wand recharges.",
        "Green modifiers boost the projectile that follows them.",
        "Gold multicasts fire the next spells together.",
        "Triggers release the next spell where they land.",
    };
    for (const char* rule : kRules) cy += ui::textWrapped(rule, cx, cy, cw, small, ui::color::kTextDim) + 5.0f * s;
}

/// The squared circle, emblem of the philosophers' stone: a circle holding a triangle,
/// a square and a small inner circle. The triangle and square turn in opposite directions.
void drawStoneSigil(Vector2 c, float r, float time, Color color) {
    const float thick = std::max(1.0f, r * 0.045f);
    auto ring = [&](float radius) {
        constexpr int kSegments = 48;
        for (int i = 0; i < kSegments; ++i) {
            const float a0 = kTau * static_cast<float>(i) / kSegments, a1 = kTau * static_cast<float>(i + 1) / kSegments;
            DrawLineEx({c.x + std::cos(a0) * radius, c.y + std::sin(a0) * radius},
                       {c.x + std::cos(a1) * radius, c.y + std::sin(a1) * radius}, thick, color);
        }
    };
    auto polygon = [&](int sides, float radius, float angle) {
        for (int i = 0; i < sides; ++i) {
            const float a0 = angle + kTau * static_cast<float>(i) / static_cast<float>(sides);
            const float a1 = angle + kTau * static_cast<float>(i + 1) / static_cast<float>(sides);
            DrawLineEx({c.x + std::cos(a0) * radius, c.y + std::sin(a0) * radius},
                       {c.x + std::cos(a1) * radius, c.y + std::sin(a1) * radius}, thick, color);
        }
    };
    ring(r);
    const float spin = time * 0.35f;
    polygon(3, r, -kPi * 0.5f + spin);         // triangle inscribed in the circle
    polygon(4, r * 0.5f, kPi * 0.25f - spin);  // square inscribed in the triangle's incircle
    ring(r * 0.5f * 0.7071f);                  // circle inscribed in the square
}

} // namespace

void GameScene::drawWorldOverlays(float alpha) {
    const int s = ui::scale();
    // Enemy health bars.
    for (const auto& e : level_->enemies()) {
        if (!e->alive() || e->hp >= e->maxHp || e->type() == EnemyType::Boss || e->team == Team::Player) continue;
        Vec2 p = e->body.interpolated(alpha);
        Vector2 sp = scene_.worldToScreen({p.x, p.y - e->body.halfH - 4.0f});
        float w = 14.0f * s, h = 2.0f * s;
        ui::bar({sp.x - w * 0.5f, sp.y, w, h}, e->hp / e->maxHp, ui::color::kHealth);
    }
    // Floating damage numbers and pickups messages.
    for (const FloatText& t : level_->floatTexts()) {
        Vector2 sp = scene_.worldToScreen(t.pos);
        float k = clamp(t.life / 0.4f, 0.0f, 1.0f);
        int size = ui::fontSize(std::max(1, static_cast<int>(std::round(static_cast<float>(s) * t.scale))));
        ui::text(t.text, sp.x, sp.y, size, hexColor(t.color, static_cast<uint8_t>(255 * k)), ui::Align::Center);
    }
    // Transmutation circles explain themselves when the alchemist comes close.
    for (const Transmuter& t : level_->transmuters()) {
        if ((t.pos - level_->player().body.pos).length() > 110.0f) continue;
        Vector2 sp = scene_.worldToScreen(t.pos - Vec2{0.0f, 36.0f});
        const int small = ui::fontSize(std::max(1, s - 1));
        if (t.complete) {
            ui::text("The circle is spent", sp.x, sp.y, small, ui::color::kTextDim, ui::Align::Center);
            continue;
        }
        ui::text("Transmutation Circle", sp.x, sp.y, ui::fontSize(s), Color{200, 170, 255, 255}, ui::Align::Center);
        ui::text(std::string("Pour in ") + matDef(t.wants).name + " to receive a perk", sp.x,
                 sp.y + static_cast<float>(ui::fontSize(s)) + 2.0f, small, ui::color::kText, ui::Align::Center);
        const float w = 50.0f * s, h = 3.0f * s;
        Rgba m = materialColor(t.wants);
        ui::bar({sp.x - w * 0.5f, sp.y + static_cast<float>(ui::fontSize(s) + small) + 6.0f, w, h},
                static_cast<float>(t.progress) / static_cast<float>(t.required), Color{m.r, m.g, m.b, 255});
    }

    // Interaction prompt.
    if (mode_ == Mode::Playing && level_->player().alive()) {
        if (const Pickup* p = level_->focusedPickup()) {
            Vector2 sp = scene_.worldToScreen({p->body.pos.x, p->body.pos.y - 16.0f});
            std::string label = "[E] " + p->label();
            ui::text(label, sp.x, sp.y, ui::fontSize(s), p->price > 0 && run_.gold < p->price ? ui::color::kDanger : ui::color::kText,
                     ui::Align::Center);
            if (p->type == PickupType::Spell) {
                ui::text(spellDef(p->spell).description, sp.x, sp.y + 12.0f * s, ui::fontSize(std::max(1, s - 1)), ui::color::kTextDim,
                         ui::Align::Center);
            } else if (p->type == PickupType::PerkAltar) {
                ui::text(perkDef(p->perk).description, sp.x, sp.y + 12.0f * s, ui::fontSize(std::max(1, s - 1)), ui::color::kTextDim,
                         ui::Align::Center);
            }
        }
    }
}

void GameScene::drawCompass(float alpha) {
    // A faint arrow orbiting the alchemist points towards the exit portal.
    const Player& pl = level_->player();
    if (!pl.alive() || level_->isSanctum()) return;
    const Vec2 p = pl.body.interpolated(alpha);
    const Vec2 to = level_->exitPos() - p;
    const float dist = to.length();
    if (dist < 60.0f) return;
    const Vec2 dir = to / dist;
    const float s = static_cast<float>(ui::scale());
    Vector2 c = scene_.worldToScreen(p + dir * 22.0f);
    Vector2 tip{c.x + dir.x * 6.0f * s, c.y + dir.y * 6.0f * s};
    Vector2 left{c.x - dir.y * 3.0f * s, c.y + dir.x * 3.0f * s};
    Vector2 right{c.x + dir.y * 3.0f * s, c.y - dir.x * 3.0f * s};
    const float pulse = 0.55f + 0.25f * std::sin(time_ * 3.0f);
    Color col{200, 170, 255, static_cast<unsigned char>(255.0f * pulse)};
    DrawTriangle(tip, right, left, col);
    DrawTriangle(tip, left, right, col);
}

void GameScene::drawHud() {
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale());
    const int small = ui::fontSize(std::max(1, ui::scale() - 1));
    const Player& pl = level_->player();
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());

    // Health, mana, levitation, breath.
    float x = 10.0f * s, y = 10.0f * s;
    float barW = 110.0f * s;
    ui::bar({x, y, barW, 7.0f * s}, pl.hp / pl.maxHp, ui::color::kHealth);
    ui::text(TextFormat("%d / %d", static_cast<int>(std::ceil(pl.hp)), static_cast<int>(pl.maxHp)), x + 3.0f * s,
             y + 3.5f * s - static_cast<float>(small) * 0.5f, small, WHITE);
    y += 9.0f * s;
    if (!run_.wands.empty()) {
        const Wand& w = run_.wands[static_cast<size_t>(run_.activeWand)];
        ui::bar({x, y, barW, 4.0f * s}, w.mana / w.manaMax, ui::color::kMana);
        y += 5.0f * s;
        float reload = w.cooldownTotal > 0.0f ? 1.0f - w.cooldown / w.cooldownTotal : 1.0f;
        ui::bar({x, y, barW, 2.0f * s}, reload, w.recharging ? Color{255, 170, 60, 255} : Color{200, 200, 220, 255});
        y += 3.0f * s;
    }
    if (pl.levitation() < 0.999f) {
        ui::bar({x, y, barW * 0.6f, 2.0f * s}, pl.levitation(), Color{190, 170, 255, 255});
        y += 3.0f * s;
    }
    if (pl.breath() < 0.999f) {
        ui::bar({x, y, barW * 0.6f, 2.0f * s}, pl.breath(), Color{90, 200, 255, 255});
        y += 3.0f * s;
    }
    // Status chips, each in its own colour; an elixir also counts down and blinks as it fades.
    struct Chip {
        const char* name;
        float left;
        Color color;
        bool elixir;
    };
    const Chip chips[] = {
        {"BURNING", pl.status.burning, {255, 140, 60, 255}, false},   {"WET", pl.status.wet, {120, 175, 255, 255}, false},
        {"OILED", pl.status.oiled, {200, 160, 100, 255}, false},      {"POISONED", pl.status.poisoned, {150, 220, 90, 255}, false},
        {"CHILLED", pl.status.chilled, {160, 225, 255, 255}, false},  {"HASTE", pl.status.hasted, {220, 228, 240, 255}, true},
        {"FURY", pl.status.fury, {255, 165, 60, 255}, true},
    };
    float chipX = x;
    for (const Chip& c : chips) {
        if (c.left <= 0.0f || !pl.alive()) continue;
        const std::string label = c.elixir ? TextFormat("%s %d", c.name, static_cast<int>(std::ceil(c.left))) : c.name;
        Color color = c.color;
        if (c.elixir && c.left < 2.0f && std::fmod(time_, 0.3f) < 0.15f) color.a = 110;
        ui::text(label, chipX, y + 2.0f * s, small, color);
        chipX += static_cast<float>(ui::measure(label, small)) + 8.0f * s;
    }

    // Gold and stage.
    // A fresh coin pops the counter up a size and flashes it pale gold for a moment.
    const float pop = goldPop_ / 0.25f;
    const int goldSize = pop > 0.5f ? ui::fontSize(ui::scale() + 1) : size;
    const Color goldColor = ColorLerp(ui::color::kGold, Color{255, 248, 210, 255}, pop);
    ui::text(TextFormat("%d gold", run_.gold), sw - 10.0f * s, 10.0f * s - static_cast<float>(goldSize - size) * 0.5f, goldSize, goldColor,
             ui::Align::Right);
    std::string stage = run_.inSanctum ? "Sanctum" : std::string(stageNumeral(run_.levelIndex)) + ". " + biomeDef(level_->biome()).name;
    if (const Omen omen = level_->layout().omen; omen != Omen::None) stage += std::string(" - ") + omenDef(omen).name;
    ui::text(stage, sw - 10.0f * s, 10.0f * s + static_cast<float>(size) + 2.0f * s, small, ui::color::kTextDim, ui::Align::Right);
    if (!level_->isSanctum() && mode_ != Mode::Map)
        minimap_.drawCorner(*level_, sw, 10.0f * s + static_cast<float>(size + small) + 8.0f * s, time_);

    // Wands and the active deck.
    const float icon = 13.0f * s;
    float wx = 10.0f * s, wy = sh - icon - 10.0f * s;
    for (size_t i = 0; i < run_.wands.size(); ++i) {
        const Wand& w = run_.wands[i];
        bool active = static_cast<int>(i) == run_.activeWand;
        Rectangle r{wx, wy, icon, icon};
        ui::panel(r, active ? Color{60, 44, 90, 235} : Color{20, 16, 28, 220}, active ? ui::color::kAccent : ui::color::kPanelBorder);
        // Tiny wand glyph.
        Vector2 a{r.x + 3.0f * s, r.y + r.height - 3.0f * s}, b{r.x + r.width - 3.0f * s, r.y + 3.0f * s};
        DrawLineEx(a, b, 1.5f * s, hexColor(w.handleColor));
        DrawCircleV(b, 1.5f * s, hexColor(w.gemColor));
        ui::text(std::to_string(i + 1), r.x + 1.0f * s, r.y, small, ui::color::kTextDim);
        wx += icon + 3.0f * s;
    }
    if (!run_.wands.empty()) {
        const Wand& w = run_.wands[static_cast<size_t>(run_.activeWand)];
        float dx = wx + 8.0f * s;
        ui::text(w.name, dx, wy - static_cast<float>(small) - 2.0f * s, small, ui::color::kText);
        const float slot = 11.0f * s;
        int nextSlot = w.deckPos;
        for (int i = 0; i < w.capacity; ++i) {
            Rectangle r{dx + static_cast<float>(i) * (slot + 2.0f * s), wy + (icon - slot) * 0.5f, slot, slot};
            SpellId spell = w.order.empty() ? w.slots[static_cast<size_t>(i)] : w.slots[static_cast<size_t>(i)];
            ui::spellIcon(spell, r, i == nextSlot && !w.shuffle);
        }
    }

    // Flask.
    {
        const Flask& f = run_.flask;
        float fw = 12.0f * s, fh = 18.0f * s;
        Rectangle r{sw - fw - 12.0f * s, sh - fh - 10.0f * s, fw, fh};
        DrawRectangleRec(r, Color{20, 24, 34, 220});
        if (!f.empty()) {
            Rgba c = cellColor(makeCell(f.mat, 128), 0, 0, 0);
            float frac = static_cast<float>(f.amount) / Flask::kCapacity;
            DrawRectangleRec({r.x, r.y + r.height * (1.0f - frac), r.width, r.height * frac}, Color{c.r, c.g, c.b, 255});
        }
        DrawRectangleLinesEx(r, s, Color{190, 220, 240, 200});
        DrawRectangleRec({r.x + r.width * 0.3f, r.y - 3.0f * s, r.width * 0.4f, 3.0f * s}, Color{120, 80, 50, 255});
        std::string label = f.empty() ? "Empty flask" : std::string(matDef(f.mat).name) + TextFormat(" %d%%", f.amount * 100 / Flask::kCapacity);
        ui::text(label, r.x - 4.0f * s, r.y + r.height - static_cast<float>(small), small, ui::color::kTextDim, ui::Align::Right);
        ui::text("RMB pour  Q absorb  F drink", r.x - 4.0f * s, r.y + r.height - static_cast<float>(small) * 2.2f, small,
                 Color{120, 112, 104, 200}, ui::Align::Right);
    }

    // Boss (or guardian) health.
    if (const Enemy* boss = level_->bossBarTarget()) {
        float bw = sw * 0.5f;
        Rectangle r{(sw - bw) * 0.5f, 14.0f * s, bw, 6.0f * s};
        ui::text(boss->def().name, sw * 0.5f, r.y - static_cast<float>(small) - 2.0f, small, Color{255, 220, 140, 255}, ui::Align::Center);
        ui::bar(r, boss->hp / boss->maxHp, Color{220, 160, 40, 255});
    }
}

void GameScene::drawFeatToast() {
    if (toasts_.empty()) return;
    const Toast& t = toasts_.front();
    const FeatDef& def = featDef(t.feat);
    const float sw = static_cast<float>(app_.screenWidth());
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale()), small = ui::fontSize(std::max(1, ui::scale() - 1));
    // Slides down from the top, holds, then fades.
    const float in = std::min(1.0f, t.time / 0.25f), out = clamp((3.5f - t.time) / 0.5f, 0.0f, 1.0f);
    const float a = std::min(in, out);
    const float w = static_cast<float>(std::max(ui::measure(def.name, size), ui::measure(def.description, small))) + 24.0f * s;
    const float h = static_cast<float>(small * 2 + size) + 14.0f * s;
    const Rectangle r{sw * 0.5f - w * 0.5f, 26.0f * s - (1.0f - in) * 12.0f * s, w, h};
    const auto al = [&](float k) { return static_cast<unsigned char>(255.0f * a * k); };
    DrawRectangleRec(r, Color{18, 13, 28, al(0.9f)});
    DrawRectangleLinesEx(r, s, Color{ui::color::kGold.r, ui::color::kGold.g, ui::color::kGold.b, al(0.8f)});
    float y = r.y + 4.0f * s;
    ui::text("FEAT EARNED", sw * 0.5f, y, small, Color{200, 170, 100, al(1.0f)}, ui::Align::Center);
    y += static_cast<float>(small) + 2.0f * s;
    ui::text(def.name, sw * 0.5f, y, size, Color{255, 222, 140, al(1.0f)}, ui::Align::Center);
    y += static_cast<float>(size) + 2.0f * s;
    ui::text(def.description, sw * 0.5f, y, small, Color{205, 195, 215, al(1.0f)}, ui::Align::Center);
}

void GameScene::drawTitleCard() {
    if (titleTimer_ <= 0.0f) return;
    float a = std::min(1.0f, titleTimer_ / 1.0f) * std::min(1.0f, (4.0f - titleTimer_) * 2.0f);
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const int s = ui::scale();
    unsigned char alpha = static_cast<unsigned char>(255.0f * clamp(a, 0.0f, 1.0f));
    ui::text(titleMain_, sw * 0.5f, sh * 0.22f, ui::fontSize(s * 3), Color{240, 226, 200, alpha}, ui::Align::Center);
    const float subY = sh * 0.22f + static_cast<float>(ui::fontSize(s * 3)) + 6.0f * s;
    ui::text(titleSub_, sw * 0.5f, subY, ui::fontSize(s + 1), Color{190, 170, 220, alpha}, ui::Align::Center);
    if (!titleOmen_.empty())
        ui::text(titleOmen_, sw * 0.5f, subY + static_cast<float>(ui::fontSize(s + 1)) * 1.5f, ui::fontSize(s), Color{235, 190, 110, alpha},
                 ui::Align::Center);
}

void GameScene::drawControlsHint() {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::fontSize(std::max(1, ui::scale() - 1));
    float t = level_->time();
    float a = clamp(std::min((t - 1.0f) * 2.0f, (30.0f - t) / 2.0f), 0.0f, 1.0f);
    if (a <= 0.0f) return;
    static constexpr const char* kKeyboard[] = {"A/D move   W/Space jump - hold to levitate", "Mouse aim   LMB cast   1-4 / wheel switch wand",
                                                "Wand 2 digs through rock   Tab edit wands", "E interact   M map   Follow the arrow to the portal"};
    static constexpr const char* kGamepad[] = {"Left stick move   A jump - hold to levitate", "Right stick aim   RT cast   LB / RB switch wand",
                                               "Wand 2 digs through rock   Back edit wands", "B interact   D-pad down map   Follow the arrow"};
    const auto& lines = usingGamepad_ ? kGamepad : kKeyboard;
    float x = 10.0f * s, y = 60.0f * s;
    Rectangle r{x - 4.0f * s, y - 4.0f * s, 170.0f * s, (static_cast<float>(small) * 1.3f) * 4.0f + 8.0f * s};
    unsigned char al = static_cast<unsigned char>(200.0f * a);
    DrawRectangleRec(r, Color{10, 8, 16, static_cast<unsigned char>(160.0f * a)});
    for (const char* l : lines) {
        ui::text(l, x, y, small, Color{220, 210, 200, al});
        y += static_cast<float>(small) * 1.3f;
    }
}

SpellId& GameScene::editorSlot(int wand, int slot) {
    return wand < 0 ? run_.inventory[static_cast<size_t>(slot)] : run_.wands[static_cast<size_t>(wand)].slots[static_cast<size_t>(slot)];
}

void GameScene::dropDragOnto(int wand, int slot) {
    std::swap(editorSlot(drag_.wand, drag_.slot), editorSlot(wand, slot));
    app_.audio().play(Sfx::UiClick, 0.7f);
    if (wand >= 0) {
        // Show what the change did to the wand's first cast.
        firstCastWand_ = wand;
        firstCastFlash_ = 1.5f;
        if (drag_.wand < 0) {
            hints_.markSeen(Hint::WandGuide);
            earnFeat(FeatId::Spellwright);
        }
    }
    drag_ = Drag{};
}

void GameScene::updateEditorPad() {
    if (!IsGamepadAvailable(0)) return;
    // The cursor walks a grid: one row per wand, then the satchel's two rows of eight.
    const int wands = static_cast<int>(run_.wands.size());
    const int satchelCols = RunState::kInventorySize / 2;
    auto rowWidth = [&](int row) { return row < wands ? run_.wands[static_cast<size_t>(row)].capacity : satchelCols; };
    int row = padCursor_.wand >= 0 ? padCursor_.wand : wands + padCursor_.slot / satchelCols;
    int col = padCursor_.wand >= 0 ? padCursor_.slot : padCursor_.slot % satchelCols;
    row = std::clamp(row, 0, wands + 1);
    col = std::clamp(col, 0, rowWidth(row) - 1);

    int dx = 0, dy = 0;
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) dx = -1;
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) dx = 1;
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP)) dy = -1;
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) dy = 1;
    // A flick of the left stick moves one slot.
    const float lx = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X), ly = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
    const int sx = lx < -0.6f ? -1 : (lx > 0.6f ? 1 : 0), sy = ly < -0.6f ? -1 : (ly > 0.6f ? 1 : 0);
    if (sx != stickX_ && sx != 0) dx = sx;
    if (sy != stickY_ && sy != 0) dy = sy;
    stickX_ = sx;
    stickY_ = sy;
    if (dx != 0 || dy != 0) {
        row = std::clamp(row + dy, 0, wands + 1);
        col = std::clamp(col + dx, 0, rowWidth(row) - 1);
        app_.audio().play(Sfx::UiHover, 0.4f);
    }
    padCursor_ = row < wands ? SlotRef{row, col} : SlotRef{-1, (row - wands) * satchelCols + col};

    SpellId& at = editorSlot(padCursor_.wand, padCursor_.slot);
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
        if (drag_.active) dropDragOnto(padCursor_.wand, padCursor_.slot);
        else if (at != SpellId::None) drag_ = {true, at, padCursor_.wand, padCursor_.slot};
    }
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP) && !drag_.active && padCursor_.wand >= 0 && at != SpellId::None) {
        if (run_.addSpell(at)) at = SpellId::None;
        app_.audio().play(Sfx::UiClick, 0.6f);
    }
}

void GameScene::drawInventory() {
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale());
    const int small = ui::fontSize(std::max(1, ui::scale() - 1));
    const bool pad = usingGamepad_;
    DrawRectangle(0, 0, app_.screenWidth(), app_.screenHeight(), Color{0, 0, 0, 160});

    const float slot = 16.0f * s, gap = 2.0f * s;
    // Tall enough for every wand row, the satchel and the info card.
    const float panelH = std::max(200.0f, 104.0f + 36.4f * static_cast<float>(run_.wands.size())) * s;
    Rectangle panelR{sw * 0.5f - 260.0f * s, sh * 0.5f - panelH * 0.5f, 520.0f * s, panelH};
    ui::panel(panelR);
    ui::text("SATCHEL OF SPELLS", panelR.x + panelR.width * 0.5f, panelR.y + 6.0f * s, size, ui::color::kGold, ui::Align::Center);
    ui::text(pad ? "D-pad moves. A picks up and places a spell, Y returns it to the satchel, B closes."
                 : "Drag spells between wands and satchel. Right-click returns a spell to the satchel. Tab closes.",
             panelR.x + panelR.width * 0.5f, panelR.y + 8.0f * s + static_cast<float>(size), small, ui::color::kTextDim,
             ui::Align::Center);

    // Guided first edit: until a satchel spell has been moved onto a wand once, the first
    // satchel spell and an empty wand slot glow, with a ghost icon drifting between them.
    int guideFrom = -1, guideWand = -1, guideSlot = -1;
    if (!hints_.seen(Hint::WandGuide)) {
        for (int i = 0; i < RunState::kInventorySize && guideFrom < 0; ++i) {
            const SpellId sp = run_.inventory[static_cast<size_t>(i)];
            if (sp != SpellId::None && spellDef(sp).proj.explosionRadius <= 5.0f) guideFrom = i;
        }
        auto firstEmpty = [&](int wi) {
            const auto& slots = run_.wands[static_cast<size_t>(wi)].slots;
            const auto it = std::find(slots.begin(), slots.end(), SpellId::None);
            return it == slots.end() ? -1 : static_cast<int>(it - slots.begin());
        };
        for (int k = -1; k < static_cast<int>(run_.wands.size()) && guideSlot < 0; ++k) {
            const int wi = k < 0 ? run_.activeWand : k; // the active wand first
            if (wi < 0 || wi >= static_cast<int>(run_.wands.size())) continue;
            guideSlot = firstEmpty(wi);
            guideWand = wi;
        }
    }
    const bool guide = guideFrom >= 0 && guideSlot >= 0;
    Rectangle guideFromR{}, guideToR{}, cursorR{};

    struct Hover {
        SpellId spell = SpellId::None;
        int wand = -2;
        int slot = -1;
    } hover;
    const Vector2 mouse = GetMousePosition();
    // With a pad, the cursor slot is what the mouse would be pointing at.
    auto pointedAt = [&](Rectangle r, int wand, int slotIndex) {
        if (pad) return padCursor_.wand == wand && padCursor_.slot == slotIndex;
        return CheckCollisionPointRec(mouse, r);
    };
    std::string wandTip, wandTipTitle;
    const Color statColor{140, 130, 150, 255};

    float y = panelR.y + 32.0f * s;
    for (size_t wi = 0; wi < run_.wands.size(); ++wi) {
        Wand& w = run_.wands[wi];
        float x = panelR.x + 10.0f * s;
        Rectangle nameR{x, y, 104.0f * s, slot};
        ui::text(w.name, x, y, small, static_cast<int>(wi) == run_.activeWand ? ui::color::kText : ui::color::kTextDim);
        ui::text(TextFormat("delay %.2fs  recharge %.2fs", w.castDelay, w.rechargeTime), x, y + static_cast<float>(small) + 1.0f * s,
                 small, statColor);
        ui::text(TextFormat("mana %d (+%d/s)%s%s", static_cast<int>(w.manaMax), static_cast<int>(w.manaCharge), w.shuffle ? "  shuffle" : "",
                            w.spellsPerCast > 1 ? "  multicast" : ""),
                 x, y + (static_cast<float>(small) + 1.0f * s) * 2.0f, small, statColor);
        if (!pad && ui::hovered({nameR.x, nameR.y, nameR.width, slot * 1.6f})) {
            wandTipTitle = w.name;
            wandTip = TextFormat("Spread %.1f deg. Spells per cast %d. Capacity %d.%s", w.spreadDeg, w.spellsPerCast, w.capacity,
                                 w.shuffle ? " Shuffle: casts spells in random order." : "");
        }
        const float sx = panelR.x + 118.0f * s;
        for (int i = 0; i < w.capacity; ++i) {
            Rectangle r{sx + static_cast<float>(i) * (slot + gap), y, slot, slot};
            SpellId sp = w.slots[static_cast<size_t>(i)];
            bool isDragSrc = drag_.active && drag_.wand == static_cast<int>(wi) && drag_.slot == i;
            const bool pointed = pointedAt(r, static_cast<int>(wi), i);
            ui::spellIcon(isDragSrc ? SpellId::None : sp, r, pointed);
            if (pointed) {
                hover = {sp, static_cast<int>(wi), i};
                cursorR = r;
            }
            if (guide && guideWand == static_cast<int>(wi) && guideSlot == i) guideToR = r;
        }
        // What the first cast of this deck does, so the effect of modifiers is visible. It
        // flashes gold after an edit changes it.
        int pos = 0;
        const CastShot first = buildShot(w.slots, w.order, pos, w.spellsPerCast);
        float damage = 0.0f;
        for (const ProjectileLaunch& l : first.projectiles) damage += l.def.damage + l.def.explosionDamage;
        const int count = static_cast<int>(first.projectiles.size());
        Color firstColor = statColor;
        if (firstCastWand_ == static_cast<int>(wi) && firstCastFlash_ > 0.0f)
            firstColor = ColorLerp(statColor, ui::color::kGold, std::min(1.0f, firstCastFlash_ / 0.5f));
        if (count > 0)
            ui::text(TextFormat("first cast: %d projectile%s, %d damage, %d mana", count, count == 1 ? "" : "s", static_cast<int>(damage),
                                static_cast<int>(first.mana)),
                     sx, y + slot + 3.0f * s, small, firstColor);
        y += slot * 1.9f + 6.0f * s;
    }

    // Satchel grid, right under the wands.
    y += static_cast<float>(small) + 4.0f * s;
    ui::text("Satchel", panelR.x + 10.0f * s, y - static_cast<float>(small) - 2.0f * s, small, ui::color::kTextDim);
    for (int i = 0; i < RunState::kInventorySize; ++i) {
        int col = i % 8, row = i / 8;
        Rectangle r{panelR.x + 10.0f * s + static_cast<float>(col) * (slot + gap), y + static_cast<float>(row) * (slot + gap), slot, slot};
        SpellId sp = run_.inventory[static_cast<size_t>(i)];
        bool isDragSrc = drag_.active && drag_.wand == -1 && drag_.slot == i;
        const bool pointed = pointedAt(r, -1, i);
        ui::spellIcon(isDragSrc ? SpellId::None : sp, r, pointed);
        if (pointed) {
            hover = {sp, -1, i};
            cursorR = r;
        }
        if (guide && guideFrom == i) guideFromR = r;
    }
    const float satchelBottom = y + 2.0f * (slot + gap);

    // Perks beside the satchel.
    float px = panelR.x + 10.0f * s + 8.0f * (slot + gap) + 14.0f * s;
    ui::text("Perks", px, y - static_cast<float>(small) - 2.0f * s, small, ui::color::kTextDim);
    float py = y;
    for (int p = 0; p < kPerkCount; ++p) {
        int n = run_.perkCount(static_cast<PerkId>(p));
        if (n == 0) continue;
        const PerkDef& d = perkDef(static_cast<PerkId>(p));
        std::string label = n > 1 ? std::string(d.name) + " x" + std::to_string(n) : d.name;
        ui::text(label, px, py, small, hexColor(d.color));
        if (!pad && ui::hovered({px, py, 100.0f * s, static_cast<float>(small)})) {
            wandTipTitle = d.name;
            wandTip = d.description;
        }
        py += static_cast<float>(small) * 1.2f;
        if (py > panelR.y + panelR.height - 14.0f * s) break;
    }
    ui::text(TextFormat("Kills %d   Gold %d   Time %s", run_.stats.kills, run_.gold, formatTime(run_.stats.time).c_str()),
             panelR.x + panelR.width - 10.0f * s, panelR.y + panelR.height - static_cast<float>(small) - 6.0f * s, small,
             ui::color::kTextDim, ui::Align::Right);

    // Info card: the spell under the cursor, or the rules of wands.
    drawSpellCard({panelR.x + panelR.width - 140.0f * s, panelR.y + 32.0f * s, 130.0f * s, panelR.height - 56.0f * s},
                  drag_.active ? drag_.spell : hover.spell);

    if (guide && !drag_.active) {
        const float t = static_cast<float>(GetTime());
        const unsigned char glow = static_cast<unsigned char>(140.0f + 100.0f * std::sin(t * 6.0f));
        const Color gold{ui::color::kGold.r, ui::color::kGold.g, ui::color::kGold.b, glow};
        const float m = 2.0f * s;
        DrawRectangleLinesEx({guideFromR.x - m, guideFromR.y - m, guideFromR.width + 2 * m, guideFromR.height + 2 * m}, s, gold);
        DrawRectangleLinesEx({guideToR.x - m, guideToR.y - m, guideToR.width + 2 * m, guideToR.height + 2 * m}, s, gold);
        // The ghost eases from the satchel to the slot, fading in and out at the ends.
        const float k = std::fmod(t * 0.7f, 1.0f), e = k * k * (3.0f - 2.0f * k);
        const Rectangle ghost{guideFromR.x + (guideToR.x - guideFromR.x) * e, guideFromR.y + (guideToR.y - guideFromR.y) * e, slot, slot};
        ui::spellIcon(run_.inventory[static_cast<size_t>(guideFrom)], ghost, false);
        DrawRectangleRec(ghost, Color{14, 12, 20, static_cast<unsigned char>(255.0f * (1.0f - std::sin(k * PI)))});
        ui::text(HintTracker::text(Hint::WandGuide, pad), panelR.x + 10.0f * s, satchelBottom + 3.0f * s, small, ui::color::kGold);
    }
    if (pad && !hints_.seen(Hint::WandGuide) && cursorR.width <= 0.0f) cursorR = guideFromR;
    if (pad && cursorR.width > 0.0f) {
        const float m = 1.0f * s;
        DrawRectangleLinesEx({cursorR.x - m, cursorR.y - m, cursorR.width + 2 * m, cursorR.height + 2 * m}, s, ui::color::kGold);
    }

    // Drag & drop.
    if (!pad) {
        if (input::mousePressed(MOUSE_BUTTON_LEFT) && hover.wand != -2 && hover.spell != SpellId::None) {
            drag_ = {true, hover.spell, hover.wand, hover.slot};
        }
        if (input::mousePressed(MOUSE_BUTTON_RIGHT) && hover.wand >= 0 && hover.spell != SpellId::None) {
            if (run_.addSpell(hover.spell)) editorSlot(hover.wand, hover.slot) = SpellId::None;
            app_.audio().play(Sfx::UiClick, 0.6f);
        }
    }
    if (drag_.active) {
        // The held spell follows the mouse, or floats above the pad cursor.
        const Rectangle r = pad ? Rectangle{cursorR.x + slot * 0.4f, cursorR.y - slot * 0.6f, slot, slot}
                                : Rectangle{mouse.x - slot * 0.5f, mouse.y - slot * 0.5f, slot, slot};
        ui::spellIcon(drag_.spell, r, true);
        if (!pad && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (hover.wand != -2) dropDragOnto(hover.wand, hover.slot);
            else drag_ = Drag{};
        }
    } else if (hover.spell == SpellId::None && !wandTip.empty()) {
        ui::tooltip(wandTipTitle, wandTip, ui::color::kGold);
    }
}

void GameScene::drawPauseMenu() {
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const float s = static_cast<float>(ui::scale());
    DrawRectangle(0, 0, app_.screenWidth(), app_.screenHeight(), Color{0, 0, 0, 150});
    Rectangle panelR{sw * 0.5f - 80.0f * s, sh * 0.5f - 66.0f * s, 160.0f * s, 132.0f * s};
    ui::panel(panelR);
    ui::text("PAUSED", sw * 0.5f, panelR.y + 8.0f * s, ui::fontSize(ui::scale() + 1), ui::color::kGold, ui::Align::Center);
    float bw = 130.0f * s, bh = 18.0f * s, x = sw * 0.5f - bw * 0.5f, y = panelR.y + 30.0f * s;
    pauseNav_.update(4);
    int index = 0;
    auto item = [&](const char* label) {
        const bool pressed = ui::button({x, y, bw, bh}, label, true, pauseNav_.focused(index)) || pauseNav_.activated(index);
        ++index;
        y += bh + 5.0f * s;
        return pressed;
    };
    if (item("Resume")) mode_ = Mode::Playing;
    if (item("Settings")) mode_ = Mode::Settings;
    if (item("Abandon Run")) {
        recordProfile(false);
        app_.setScene(std::make_unique<MenuScene>(app_));
    }
    // Quitting keeps the run suspended at the start of this stage.
    if (item("Save & Quit")) app_.requestQuit();
}

void GameScene::drawEndScreen(bool victory) {
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const float s = static_cast<float>(ui::scale());
    float a = clamp((endTimer_ - (victory ? 0.5f : 1.6f)) * 1.5f, 0.0f, 1.0f);
    DrawRectangle(0, 0, app_.screenWidth(), app_.screenHeight(), Color{0, 0, 0, static_cast<unsigned char>(170.0f * a)});
    if (a <= 0.0f) return;
    unsigned char al = static_cast<unsigned char>(255.0f * a);
    const int font = ui::fontSize(ui::scale());
    const float fontH = static_cast<float>(font);
    const char* title = victory ? "MAGNUM OPUS" : "THE WORK IS UNDONE";
    const char* sub = victory ? "The Prima Materia is perfected. You have become the Stone."
                              : "Your matter returns to the prima materia...";
    ui::text(title, sw * 0.5f, sh * 0.2f, ui::fontSize(ui::scale() * 3), victory ? Color{255, 220, 120, al} : Color{230, 80, 80, al},
             ui::Align::Center);
    const float subY = sh * 0.2f + static_cast<float>(ui::fontSize(ui::scale() * 3)) + 8.0f * s;
    ui::text(sub, sw * 0.5f, subY, font, Color{210, 200, 220, al}, ui::Align::Center);
    if (!victory && level_) {
        const Player& player = level_->player();
        const std::string cause = deathCause(player);
        const float causeY = subY + fontH * 1.6f;
        if (!cause.empty()) {
            ui::text(cause, sw * 0.5f, causeY, font, Color{255, 150, 110, al}, ui::Align::Center);
            // The killer's portrait beside the line, so the culprit reads at a glance.
            if (const auto killer = player.lastHitSelf ? std::nullopt : enemyNamed(player.lastHitCause)) {
                const EnemyDef& def = enemyDef(*killer);
                const SpriteInfo& info = app_.sprites().info(def.sprite);
                const int scale = std::max(1, static_cast<int>(fontH * 1.6f) / std::max(1, info.h));
                const float w = static_cast<float>(info.w * scale), h = static_cast<float>(info.h * scale);
                const float x = sw * 0.5f - static_cast<float>(ui::measure(cause, font)) * 0.5f - w - 6.0f * s;
                Color tint = enemyTint(*killer);
                tint.a = al;
                app_.sprites().drawScaled(def.sprite, 0, {x, causeY + fontH * 0.5f - h * 0.5f}, scale, false, tint);
            }
        }
        if (const char* advice = deathAdvice(player))
            ui::text(advice, sw * 0.5f, subY + fontH * 2.8f, font, Color{170, 160, 185, al}, ui::Align::Center);
    }

    // The run in two columns either side of a central gutter.
    const RunStats& st = run_.stats;
    const float lineH = fontH * 1.35f;
    std::string reached = run_.levelIndex <= 4 ? biomeDef(biomeForDepth(run_.levelIndex)).name : "The Athanor";
    const std::string left[] = {
        std::string("Reached: ") + (run_.inSanctum ? "Sanctum after " : "") + reached,
        "Time: " + formatTime(st.time),
        "Creatures slain: " + std::to_string(st.kills),
        "Gold gathered: " + std::to_string(st.goldCollected),
    };
    const std::string right[] = {
        "Spells cast: " + std::to_string(st.spellsCast),
        "Chests opened: " + std::to_string(st.chestsOpened),
        TextFormat("Seed: %u%s", run_.seed, run_.daily ? " (Daily Rite)" : ""),
    };
    const Color statColor{220, 210, 200, al};
    float y = std::max(sh * 0.38f, subY + lineH * 3.7f);
    const float gutter = 14.0f * s;
    for (size_t i = 0; i < std::size(left); ++i)
        ui::text(left[i], sw * 0.5f - gutter, y + lineH * static_cast<float>(i), font, statColor, ui::Align::Right);
    for (size_t i = 0; i < std::size(right); ++i)
        ui::text(right[i], sw * 0.5f + gutter, y + lineH * static_cast<float>(i), font, statColor);
    y += lineH * static_cast<float>(std::size(left)) + lineH * 0.3f;

    // Lasting progress: personal bests, new Grimoire entries, unlocked paths and the next one.
    if (!record_.records.empty()) {
        std::string line;
        for (const std::string& r : record_.records) line += (line.empty() ? "" : "  -  ") + r;
        ui::text(line, sw * 0.5f, y, font, Color{255, 220, 120, al}, ui::Align::Center);
        y += lineH;
    }
    if (!record_.discoveries.empty()) {
        ui::text("New in the Grimoire: " + record_.discoveries, sw * 0.5f, y, font, Color{190, 160, 255, al}, ui::Align::Center);
        y += lineH;
    }
    for (const std::string& path : record_.unlockedPaths) {
        ui::text("Path unlocked: the " + path, sw * 0.5f, y, font, Color{255, 210, 110, al}, ui::Align::Center);
        y += lineH;
    }
    if (record_.nearestPath) {
        const PathProgress& np = *record_.nearestPath;
        std::string label = TextFormat("Next path, the %s: %s %d/%d", np.name.c_str(), np.requirement.c_str(), np.now, np.target);
        if (np.now > np.was) label += TextFormat("  (+%d this run)", np.now - np.was);
        ui::text(label, sw * 0.5f, y, font, Color{200, 190, 170, al}, ui::Align::Center);
        y += fontH * 1.25f;
        // The bar: earlier progress dim, this run's gain bright.
        const Rectangle bar{sw * 0.5f - 110.0f * s, y, 220.0f * s, 4.0f * s};
        const float target = static_cast<float>(std::max(1, np.target));
        const float wasW = bar.width * std::min(1.0f, static_cast<float>(np.was) / target);
        const float nowW = bar.width * std::min(1.0f, static_cast<float>(np.now) / target);
        DrawRectangleRec(bar, Color{40, 34, 52, al});
        DrawRectangleRec({bar.x, bar.y, wasW, bar.height}, Color{150, 120, 70, al});
        DrawRectangleRec({bar.x + wasW, bar.y, nowW - wasW, bar.height}, Color{255, 210, 110, al});
        y += bar.height + lineH * 0.5f;
    }
    if (a < 1.0f) return;

    const float bw = 100.0f * s, bh = 18.0f * s, gap = 6.0f * s;
    float bx = sw * 0.5f - bw * 1.5f - gap;
    y += 6.0f * s;
    auto startRun = [&](uint32_t seed) {
        RunState fresh = newRun(seed, run_.difficulty, false, run_.loadout);
        app_.setScene(std::make_unique<GameScene>(app_, std::move(fresh)));
    };
    if (ui::button({bx, y, bw, bh}, victory ? "New Work" : "Try Again") || ui::confirmPressed() || input::keyPressed(KEY_R)) {
        startRun(static_cast<uint32_t>(GetRandomValue(1, 0x7FFFFFFF)));
        return;
    }
    bx += bw + gap;
    const bool retryKey = input::keyPressed(KEY_T) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP);
    if (ui::button({bx, y, bw, bh}, usingGamepad_ ? "Retry Seed (Y)" : "Retry Seed (T)") || retryKey) {
        startRun(run_.seed); // the same stages and treasures, outside the Daily Rite
        return;
    }
    if (ui::hovered({bx, y, bw, bh}))
        ui::tooltip("Retry Seed", "Walk the same stages again: the same layout, wands and treasures.", ui::color::kAccent);
    bx += bw + gap;
    if (ui::button({bx, y, bw, bh}, "Main Menu") || ui::backPressed()) app_.setScene(std::make_unique<MenuScene>(app_));
}

void GameScene::drawDebugOverlay() {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::fontSize(std::max(1, ui::scale() - 1));
    float x = 10.0f * s, y = static_cast<float>(app_.screenHeight()) * 0.45f;
    ui::text(TextFormat("%d FPS", GetFPS()), x, y, small, GREEN);
    y += static_cast<float>(small) * 1.3f;
    if (!debugOverlay_) return;
    const Profiler& prof = Profiler::instance();
    for (int i = 0; i < static_cast<int>(ProfileSection::Count); ++i) {
        auto sec = static_cast<ProfileSection>(i);
        ui::text(TextFormat("%-12s %5.2f ms (peak %5.2f)", Profiler::name(sec), prof.average(sec), prof.peak(sec)), x, y, small, GREEN);
        y += static_cast<float>(small) * 1.2f;
    }
    if (level_) {
        ui::text(TextFormat("chunks %d  particles %zu  projectiles %zu  enemies %zu", level_->world().activeChunkCount(),
                            level_->particles().count(), level_->projectiles().size(), level_->enemies().size()),
                 x, y, small, GREEN);
        y += static_cast<float>(small) * 1.2f;
        ui::text(TextFormat("lights %d/%d", scene_.lightsSelected(), scene_.lightCandidates()), x, y, small, GREEN);
    }
}

void GameScene::drawLoading() {
    // An alchemical motto for each step of the descent: the Latin, then its meaning.
    struct Motto {
        const char* latin;
        const char* english;
    };
    static constexpr Motto kStageMottos[] = {
        {"Visita interiora terrae, rectificando invenies occultum lapidem",
         "Visit the interior of the earth; by rectifying, you will find the hidden stone."},
        {"Solve et coagula", "Dissolve, and bind together."},
        {"Quod est inferius est sicut quod est superius", "That which is below is as that which is above."},
        {"Igne natura renovatur integra", "Through fire, nature is renewed whole."},
        {"Ars totum requirit hominem", "The Art demands the whole of you."},
    };
    static constexpr Motto kSanctumMotto = {"Ora, lege, lege, lege, relege, labora et invenies",
                                            "Pray, read, read, read, reread, work, and you will find."};

    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const int s = ui::scale();
    std::string text = run_.inSanctum ? "Entering the sanctum" : "Descending";
    int dots = static_cast<int>(time_ * 3.0f) % 4;
    ui::text(text + std::string(static_cast<size_t>(dots), '.'), sw * 0.5f, sh * 0.42f, ui::fontSize(s + 1), ui::color::kTextDim,
             ui::Align::Center);

    const Motto& motto = run_.inSanctum ? kSanctumMotto : kStageMottos[clamp(run_.levelIndex, 0, 4)];
    const auto alpha = static_cast<unsigned char>(255.0f * clamp(loadTimer_ * 2.5f - 0.3f, 0.0f, 1.0f));
    drawStoneSigil({sw * 0.5f, sh * 0.29f}, 24.0f * static_cast<float>(s), time_, Color{214, 186, 120, static_cast<unsigned char>(alpha / 2)});
    const float font = static_cast<float>(ui::fontSize(s));
    ui::text(motto.latin, sw * 0.5f, sh * 0.52f, ui::fontSize(s), Color{214, 186, 120, alpha}, ui::Align::Center);
    ui::text(motto.english, sw * 0.5f, sh * 0.52f + font * 1.5f, ui::fontSize(s), Color{150, 140, 165, alpha}, ui::Align::Center);
}

} // namespace pm
