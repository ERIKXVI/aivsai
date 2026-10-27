// GameScene presentation: HUD, world overlays, compass and reticle, title card, notices,
// loading screen and debug overlay. The wand editor is in GameSceneEditor.cpp, the pause menu
// and end screens in GameSceneMenus.cpp; run flow lives in GameScene.cpp.

#include "scenes/GameScene.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "app/App.hpp"
#include "core/Profiler.hpp"
#include "render/ColorUtil.hpp"
#include "ui/Ui.hpp"

namespace pm {

namespace {

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

void GameScene::drawWorldOverlays(float alpha) const {
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
        const float popScale = t.scale * (1.0f + 0.5f * t.pop); // a number that just grew swells for a moment
        int size = ui::fontSize(std::max(1, static_cast<int>(std::round(static_cast<float>(s) * popScale))));
        ui::text(t.text, sp.x, sp.y, size, hexColor(t.color, static_cast<uint8_t>(255 * k)), ui::Align::Center);
    }
    // Transmutation circles explain themselves when the alchemist comes close.
    for (const Transmuter& t : level_->transmuters()) {
        if ((t.pos - level_->player().body.pos).length() > 110.0f) continue;
        Vector2 sp = scene_.worldToScreen(t.pos - Vec2{0.0f, 36.0f});
        const int small = ui::smallFontSize();
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
            std::string label = (usingGamepad_ ? "[B] " : "[E] ") + p->label();
            if (p->rare) label += " - a rare find";
            const Color labelColor = p->price > 0 && run_.gold < p->price ? ui::color::kDanger
                                     : p->rare                            ? ui::color::kGold
                                                                          : ui::color::kText;
            ui::text(label, sp.x, sp.y, ui::fontSize(s), labelColor, ui::Align::Center);
            if (p->type == PickupType::Spell) {
                ui::text(spellDef(p->spell).description, sp.x, sp.y + 12.0f * s, ui::smallFontSize(),
                         ui::color::kTextDim, ui::Align::Center);
            } else if (p->type == PickupType::PerkAltar) {
                ui::text(perkDef(p->perk).description, sp.x, sp.y + 12.0f * s, ui::smallFontSize(),
                         ui::color::kTextDim, ui::Align::Center);
            } else if (p->type == PickupType::Wand && p->wand) {
                drawWandCard(*p->wand, sp.x, sp.y - 3.0f * s); // above the prompt, clear of the wand itself
            }
        }
    }
}

void GameScene::drawWandCard(const Wand& w, float cx, float bottom) const {
    // A wand on the floor or in a shop: its slots and numbers, each number green or red against
    // the wand in hand (lower is better for the delays).
    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    const Wand* held = run_.currentWand();
    constexpr Color kBetter{130, 230, 130, 255}, kWorse{240, 110, 100, 255};
    auto judge = [&](float mine, float theirs, bool lowerIsBetter) {
        if (!held || std::fabs(mine - theirs) < 1e-3f) return ui::color::kText;
        return (mine < theirs) == lowerIsBetter ? kBetter : kWorse;
    };
    using Segment = std::pair<std::string, Color>;
    const std::vector<Segment> timing = {
        {"delay ", ui::color::kTextDim}, {TextFormat("%.2fs", w.castDelay), judge(w.castDelay, held ? held->castDelay : 0.0f, true)},
        {"  recharge ", ui::color::kTextDim}, {TextFormat("%.2fs", w.rechargeTime), judge(w.rechargeTime, held ? held->rechargeTime : 0.0f, true)}};
    std::vector<Segment> mana = {
        {"mana ", ui::color::kTextDim}, {TextFormat("%d", static_cast<int>(w.manaMax)), judge(w.manaMax, held ? held->manaMax : 0.0f, false)},
        {" (+", ui::color::kTextDim}, {TextFormat("%d", static_cast<int>(w.manaCharge)), judge(w.manaCharge, held ? held->manaCharge : 0.0f, false)},
        {"/s)", ui::color::kTextDim}};
    if (w.spellsPerCast > 1) mana.push_back({"  multicast", ui::color::kAccent});
    if (w.shuffle) mana.push_back({"  shuffle", ui::color::kTextDim});
    const bool full = static_cast<int>(run_.wands.size()) >= RunState::kMaxWands;
    const std::string swap = full && held ? "Replaces your " + held->name + ", which drops here" : std::string{};

    auto widthOf = [&](const std::vector<Segment>& line) {
        int width = 0;
        for (const Segment& seg : line) width += ui::measure(seg.first, small);
        return static_cast<float>(width);
    };
    const float icon = 10.0f * s, gap = 2.0f * s;
    const float iconsW = static_cast<float>(w.capacity) * (icon + gap) - gap;
    const float lineH = static_cast<float>(small) + 2.0f * s;
    float cardW = std::max({iconsW, widthOf(timing), widthOf(mana), static_cast<float>(ui::measure(swap, small))}) + 10.0f * s;
    const float cardH = 4.0f * s + icon + 3.0f * s + lineH * (swap.empty() ? 2.0f : 3.0f) + 2.0f * s;
    Rectangle card{cx - cardW * 0.5f, std::max(4.0f * s, bottom - cardH), cardW, cardH};
    card.x = clamp(card.x, 4.0f * s, static_cast<float>(app_.screenWidth()) - cardW - 4.0f * s);
    DrawRectangleRec(card, Color{10, 8, 16, 190});
    const float mid = card.x + cardW * 0.5f;
    float y = card.y + 4.0f * s;
    for (int i = 0; i < w.capacity; ++i) {
        const SpellId spell = w.slots[static_cast<size_t>(i)];
        const Rectangle r{mid - iconsW * 0.5f + static_cast<float>(i) * (icon + gap), y, icon, icon};
        if (spell == SpellId::None) DrawRectangleLinesEx(r, std::max(1.0f, s * 0.5f), Color{90, 80, 110, 200});
        else ui::spellIcon(spell, r);
    }
    y += icon + 3.0f * s;
    auto drawLine = [&](const std::vector<Segment>& line) {
        float x = mid - widthOf(line) * 0.5f;
        for (const Segment& seg : line) {
            ui::text(seg.first, x, y, small, seg.second);
            x += static_cast<float>(ui::measure(seg.first, small));
        }
        y += lineH;
    };
    drawLine(timing);
    drawLine(mana);
    if (!swap.empty()) ui::text(swap, mid, y, small, Color{255, 180, 110, 255}, ui::Align::Center);
}

void GameScene::drawCompass(float alpha) const {
    // An arrow orbiting the alchemist points the way to the exit portal: along the stage's route,
    // a stretch ahead, so it leads through the tunnels instead of straight into the rock.
    const Player& pl = level_->player();
    if (!pl.alive() || level_->isSanctum()) return;
    const Vec2 p = pl.body.interpolated(alpha);
    if ((level_->exitPos() - p).length() < 60.0f) return;
    Vec2 to = guide_.target(p) - p;
    if (to.lengthSq() < 1.0f) to = level_->exitPos() - p;
    const Vec2 dir = to / to.length();
    const float s = static_cast<float>(ui::scale());
    const Vector2 base = scene_.worldToScreen(p + dir * 22.0f);
    const Vector2 c{base.x + dir.x * 3.0f * s, base.y + dir.y * 3.0f * s}; // the arrow's centroid
    auto arrow = [&](float size, Color col) {
        const Vector2 tip{c.x + dir.x * 6.0f * size, c.y + dir.y * 6.0f * size};
        const Vector2 back{c.x - dir.x * 3.0f * size, c.y - dir.y * 3.0f * size};
        const Vector2 left{back.x - dir.y * 4.5f * size, back.y + dir.x * 4.5f * size};
        const Vector2 right{back.x + dir.y * 4.5f * size, back.y - dir.x * 4.5f * size};
        DrawTriangle(tip, right, left, col);
        DrawTriangle(tip, left, right, col);
    };
    const float pulse = 0.8f + 0.2f * std::sin(time_ * 3.0f);
    arrow(s * 1.35f, Color{10, 6, 16, static_cast<unsigned char>(200.0f * pulse)}); // dark rim for pale stages
    arrow(s, Color{200, 170, 255, static_cast<unsigned char>(255.0f * pulse)});
}

bool GameScene::showReticle() const {
    const bool still = app_.options().automated() && !app_.options().autoplay;
    return mode_ == Mode::Playing && level_ && level_->player().alive() && !exiting_ && !still;
}

void GameScene::drawReticle(float alpha) const {
    const float s = static_cast<float>(ui::scale());
    const Vector2 c = app_.options().autoplay ? scene_.worldToScreen(botAim_)
                      : usingGamepad_         ? scene_.worldToScreen(level_->player().body.interpolated(alpha) + padAim_ * 60.0f)
                                              : GetMousePosition();
    // Four ticks around an open centre, each with a dark rim so it reads on lava and on salt.
    const float gap = 2.0f * s, len = 3.0f * s, w = std::max(1.0f, s);
    auto ticks = [&](float grow, Color col) {
        DrawRectangleRec({c.x - gap - len - grow, c.y - w * 0.5f - grow, len + grow * 2.0f, w + grow * 2.0f}, col);
        DrawRectangleRec({c.x + gap - grow, c.y - w * 0.5f - grow, len + grow * 2.0f, w + grow * 2.0f}, col);
        DrawRectangleRec({c.x - w * 0.5f - grow, c.y - gap - len - grow, w + grow * 2.0f, len + grow * 2.0f}, col);
        DrawRectangleRec({c.x - w * 0.5f - grow, c.y + gap - grow, w + grow * 2.0f, len + grow * 2.0f}, col);
    };
    ticks(w, Color{10, 6, 16, 200});
    ticks(0.0f, Color{235, 228, 255, 235});
    // Around it, the held wand's rhythm: the ring fills while it recharges, and burns red when
    // its mana runs low.
    const Wand* wand = run_.currentWand();
    if (!wand) return;
    const float ready = wand->cooldownTotal > 0.0f ? clamp(1.0f - wand->cooldown / wand->cooldownTotal, 0.0f, 1.0f) : 1.0f;
    const bool lowMana = wand->mana < wand->manaMax * 0.12f;
    if (ready >= 1.0f && !lowMana) return;
    const float r = 7.5f * s;
    DrawRing(c, r - w, r + w * 0.5f, 0.0f, 360.0f, 32, Color{10, 6, 16, 120});
    const Color fill = lowMana ? Color{230, 70, 60, 230} : wand->recharging ? Color{255, 170, 60, 230} : Color{235, 228, 255, 200};
    DrawRing(c, r - w * 0.5f, r, -90.0f, -90.0f + 360.0f * (lowMana ? 1.0f : ready), 32, fill);
}

void GameScene::drawHud() const {
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale());
    const int small = ui::smallFontSize();
    const Player& pl = level_->player();
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());

    // Health, mana, levitation, breath.
    float x = 10.0f * s, y = 10.0f * s;
    float barW = 110.0f * s;
    ui::bar({x, y, barW, 7.0f * s}, pl.hp / pl.maxHp, ui::color::kHealth);
    ui::text(TextFormat("%d / %d", static_cast<int>(std::ceil(pl.hp)), static_cast<int>(pl.maxHp)), x + 3.0f * s,
             y + 3.5f * s - static_cast<float>(small) * 0.5f, small, WHITE);
    y += 9.0f * s;
    if (const Wand* wand = run_.currentWand()) {
        const Wand& w = *wand;
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
    const float pop = goldPop_ / kGoldPopSeconds;
    const int goldSize = pop > 0.5f ? ui::fontSize(ui::scale() + 1) : size;
    const Color goldColor = ColorLerp(ui::color::kGold, Color{255, 248, 210, 255}, pop);
    ui::text(TextFormat("%d gold", run_.gold), sw - 10.0f * s, 10.0f * s - static_cast<float>(goldSize - size) * 0.5f, goldSize, goldColor,
             ui::Align::Right);
    std::string stage = run_.inSanctum ? "Sanctum" : std::string(stageNumeral(run_.levelIndex)) + ". " + biomeDef(level_->biome()).name;
    if (const Omen omen = level_->layout().omen; omen != Omen::None) stage += std::string(" - ") + omenDef(omen).name;
    ui::text(stage, sw - 10.0f * s, 10.0f * s + static_cast<float>(size) + 2.0f * s, small, ui::color::kTextDim, ui::Align::Right);
    const float essenceY = 10.0f * s + static_cast<float>(size + small) + 8.0f * s;
    drawEssences(sw - 10.0f * s, essenceY);
    if (!level_->isSanctum() && mode_ != Mode::Map) minimap_.drawCorner(*level_, sw, essenceY + 8.0f * s, time_);

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
    if (const Wand* wand = run_.currentWand()) {
        const Wand& w = *wand;
        float dx = wx + 8.0f * s;
        ui::text(w.name, dx, wy - static_cast<float>(small) - 2.0f * s, small, ui::color::kText);
        const float slot = 11.0f * s;
        int nextSlot = w.deckPos;
        for (int i = 0; i < w.capacity; ++i) {
            Rectangle r{dx + static_cast<float>(i) * (slot + 2.0f * s), wy + (icon - slot) * 0.5f, slot, slot};
            // Slot order, not cast order: a shuffle wand gets no read-head highlight.
            const SpellId spell = w.slots[static_cast<size_t>(i)];
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
        // The flask's name and its controls, on a dark plate so they read over ice and sulphur.
        const char* legend = usingGamepad_ ? "LT pour  Y absorb  X drink" : "RMB pour  Q absorb  F drink";
        const float plateW = static_cast<float>(std::max(ui::measure(label, small), ui::measure(legend, small))) + 6.0f * s;
        const float plateY = r.y + r.height - static_cast<float>(small) * 2.2f - 2.0f * s;
        DrawRectangleRec({r.x - 4.0f * s - plateW, plateY, plateW + 2.0f * s, r.y + r.height - plateY + 2.0f * s}, Color{10, 8, 16, 150});
        ui::text(label, r.x - 4.0f * s, r.y + r.height - static_cast<float>(small), small, ui::color::kText, ui::Align::Right);
        ui::text(legend, r.x - 4.0f * s, r.y + r.height - static_cast<float>(small) * 2.2f, small, ui::color::kTextDim, ui::Align::Right);
    }

    // Boss (or guardian) health.
    if (const Enemy* boss = level_->bossBarTarget()) {
        float bw = sw * 0.5f;
        Rectangle r{(sw - bw) * 0.5f, 14.0f * s, bw, 6.0f * s};
        ui::text(boss->def().name, sw * 0.5f, r.y - static_cast<float>(small) - 2.0f, small, Color{255, 220, 140, 255}, ui::Align::Center);
        ui::bar(r, boss->hp / boss->maxHp, Color{220, 160, 40, 255});
    }
}

void GameScene::drawEssences(float right, float y) const {
    // The guardians' essences gathered this run: four sockets, each lit as its keeper falls;
    // all four (the Quintessence) shimmer.
    const float s = static_cast<float>(ui::scale());
    const float r = 3.0f * s, gap = 9.0f * s;
    for (int i = 0; i < kGuardianCount; ++i) {
        const bool held = (run_.essences >> i) & 1u;
        const Vector2 c{right - r - static_cast<float>(kGuardianCount - 1 - i) * gap, y};
        Color color = held ? hexColor(essenceColor(i)) : Color{46, 40, 56, 255};
        if (held && run_.quintessence()) color = ColorBrightness(color, 0.25f * std::sin(time_ * 4.0f + static_cast<float>(i)));
        DrawPoly(c, 4, r + s, 0.0f, Color{12, 8, 18, 220});
        DrawPoly(c, 4, r, 0.0f, color);
    }
}

void GameScene::drawFeatToast() const {
    if (toasts_.empty()) return;
    const Toast& t = toasts_.front();
    const FeatDef& def = featDef(t.feat);
    const float sw = static_cast<float>(app_.screenWidth());
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale()), small = ui::smallFontSize();
    // Slides down from the top, holds, then fades.
    const float in = std::min(1.0f, t.time / 0.25f), out = clamp((kToastSeconds - t.time) / 0.5f, 0.0f, 1.0f);
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

void GameScene::drawTitleCard() const {
    if (titleTimer_ <= 0.0f) return;
    float a = std::min(1.0f, titleTimer_ / 1.0f) * std::min(1.0f, (kTitleCardSeconds - titleTimer_) * 2.0f);
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

void GameScene::drawEventNotice() const {
    constexpr float kShow = 3.5f;
    if (eventNotice_.text.empty() || eventNoticeTime_ > kShow) return;
    const float a = clamp(std::min(eventNoticeTime_ * 3.0f, (kShow - eventNoticeTime_) / 0.8f), 0.0f, 1.0f);
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    Color c = hexColor(eventNotice_.color);
    c.a = static_cast<unsigned char>(255.0f * a);
    ui::text(eventNotice_.text, sw * 0.5f, sh * 0.3f, ui::fontSize(ui::scale() + 1), c, ui::Align::Center);
}

void GameScene::drawControlsHint() const {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    float t = level_->time();
    float a = clamp(std::min((t - 1.0f) * 2.0f, (30.0f - t) / 2.0f), 0.0f, 1.0f);
    if (a <= 0.0f) return;
    static constexpr const char* kKeyboard[] = {"A/D move   W/Space jump - hold to levitate", "Mouse aim   LMB cast   1-4 / wheel switch wand",
                                                "Wand 2 digs through rock   Tab edit wands", "Q absorb   RMB pour   F drink from the flask",
                                                "E interact   M map   Follow the arrow to the portal", "Esc pauses - How to Play lists every control"};
    static constexpr const char* kGamepad[] = {"Left stick move   A jump - hold to levitate", "Right stick aim   RT cast   LB / RB switch wand",
                                               "Wand 2 digs through rock   Back edit wands", "Y absorb   LT pour   X drink from the flask",
                                               "B interact   D-pad down map   Follow the arrow", "Start pauses - How to Play lists every control"};
    static_assert(std::size(kKeyboard) == std::size(kGamepad));
    const auto& lines = usingGamepad_ ? kGamepad : kKeyboard;
    float x = 10.0f * s, y = 60.0f * s;
    float widest = 0.0f;
    for (const char* l : lines) widest = std::max(widest, static_cast<float>(ui::measure(l, small)));
    Rectangle r{x - 4.0f * s, y - 4.0f * s, widest + 8.0f * s,
                (static_cast<float>(small) * 1.3f) * static_cast<float>(std::size(lines)) + 8.0f * s};
    unsigned char al = static_cast<unsigned char>(200.0f * a);
    DrawRectangleRec(r, Color{10, 8, 16, static_cast<unsigned char>(160.0f * a)});
    for (const char* l : lines) {
        ui::text(l, x, y, small, Color{220, 210, 200, al});
        y += static_cast<float>(small) * 1.3f;
    }
}

void GameScene::drawDebugOverlay() const {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
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

void GameScene::drawLoading() const {
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
    static_assert(std::size(kStageMottos) == kStageCount, "a motto for every stage");
    static constexpr Motto kSanctumMotto = {"Ora, lege, lege, lege, relege, labora et invenies",
                                            "Pray, read, read, read, reread, work, and you will find."};

    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const int s = ui::scale();
    std::string text = run_.inSanctum ? "Entering the sanctum" : "Descending";
    int dots = static_cast<int>(time_ * 3.0f) % 4;
    ui::text(text + std::string(static_cast<size_t>(dots), '.'), sw * 0.5f, sh * 0.42f, ui::fontSize(s + 1), ui::color::kTextDim,
             ui::Align::Center);

    const Motto& motto = run_.inSanctum ? kSanctumMotto : kStageMottos[clamp(run_.levelIndex, 0, kFinalStage)];
    const auto alpha = static_cast<unsigned char>(255.0f * clamp(loadTimer_ * 2.5f - 0.3f, 0.0f, 1.0f));
    drawStoneSigil({sw * 0.5f, sh * 0.29f}, 24.0f * static_cast<float>(s), time_, Color{214, 186, 120, static_cast<unsigned char>(alpha / 2)});
    const float font = static_cast<float>(ui::fontSize(s));
    ui::text(motto.latin, sw * 0.5f, sh * 0.52f, ui::fontSize(s), Color{214, 186, 120, alpha}, ui::Align::Center);
    ui::text(motto.english, sw * 0.5f, sh * 0.52f + font * 1.5f, ui::fontSize(s), Color{150, 140, 165, alpha}, ui::Align::Center);
}

} // namespace pm
