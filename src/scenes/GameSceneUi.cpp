// GameScene presentation: HUD, world overlays, compass and reticle, title card, notices, portal
// bloom, loading screen and debug overlay. The wand editor is in GameSceneEditor.cpp, the pause
// menu and end screens in GameSceneMenus.cpp; run flow lives in GameScene.cpp.

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

constexpr float kTitleTopHigh = 0.16f, kTitleTopLow = 0.58f; ///< the title card's two places, as fractions of the screen height

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

constexpr Color kPlateColor{10, 8, 16, 190}; ///< dark backing for world prompts and cards

/// World prompts sit on a dark plate so they read over lit rock and lantern glow.
void drawPlate(float cx, float top, float w, float h) {
    const float fs = static_cast<float>(ui::scale());
    DrawRectangleRec({cx - w * 0.5f - 4.0f * fs, top - 2.0f * fs, w + 8.0f * fs, h + 4.0f * fs}, kPlateColor);
}

/// Matter that puts out the alchemist's flames when drunk (Player's drink effects).
bool dousesWhenDrunk(MatId m) { return m == mat::Water || m == mat::Brine || m == mat::Ichor || m == mat::Blood; }

/// Burning, and hurt by it (Salamander Skin makes the flames harmless).
bool burningForReal(const Player& p) { return p.status.burning > 0.0f && p.resist[static_cast<size_t>(DamageType::Fire)] > 0.0f; }

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
    drawTransmuterPrompts();

    if (mode_ != Mode::Playing || !level_->player().alive()) return;
    const int small = ui::smallFontSize();
    const Pickup* focused = level_->focusedPickup();
    // Price tags under the shop's wares, gold when affordable.
    for (const Pickup& p : level_->pickups()) {
        if (p.price <= 0 || &p == focused || (p.body.pos - level_->player().body.pos).length() > 160.0f) continue;
        const Vector2 tp = scene_.worldToScreen({p.body.pos.x, p.body.pos.y + 14.0f});
        ui::text(TextFormat("%d", p.price), tp.x, tp.y, small, run_.gold >= p.price ? ui::color::kGold : ui::color::kDanger,
                 ui::Align::Center);
    }
    // Perk altars name their perk from a little way off: one altar of a group is all the alchemist
    // may claim, so the choice should be in view at once. Neighbours alternate rows, as names are
    // wider than the altars are apart.
    for (const Pickup& p : level_->pickups()) {
        if (p.dead || p.type != PickupType::PerkAltar || &p == focused || (p.body.pos - level_->player().body.pos).length() > 160.0f)
            continue;
        int rank = 0;
        for (const Pickup& o : level_->pickups())
            rank += !o.dead && o.type == PickupType::PerkAltar && o.altarGroup == p.altarGroup && o.body.pos.x < p.body.pos.x ? 1 : 0;
        const PerkDef& perk = perkDef(p.perk);
        Vector2 tp = scene_.worldToScreen({p.body.pos.x, p.body.pos.y + 14.0f});
        if (rank % 2 != 0) tp.y += static_cast<float>(small) * 1.3f;
        drawPlate(tp.x, tp.y, static_cast<float>(ui::measure(perk.name, small)), static_cast<float>(small));
        ui::text(perk.name, tp.x, tp.y, small, hexColor(perk.color), ui::Align::Center);
    }

    // Burning with a dousing drink at hand: say so over the alchemist, unless a prompt is open.
    const Player& pl = level_->player();
    if (!focused && burningForReal(pl) && !run_.flask.empty() && dousesWhenDrunk(run_.flask.mat)) {
        const char* douse = usingGamepad_ ? "[X] drink to douse" : "[F] drink to douse";
        const Vec2 p = pl.body.interpolated(alpha);
        const Vector2 sp = scene_.worldToScreen({p.x, p.y - pl.body.halfH - 12.0f});
        drawPlate(sp.x, sp.y, static_cast<float>(ui::measure(douse, small)), static_cast<float>(small));
        ui::text(douse, sp.x, sp.y, small, Color{255, 170, 80, 255}, ui::Align::Center);
    }

    // Interaction prompt: the label, and for a spell what it is, what it does and where it goes.
    if (focused) drawPickupPrompt(*focused);
}

void GameScene::drawTransmuterPrompts() const {
    const int s = ui::scale();
    const float fs = static_cast<float>(s);
    const int small = ui::smallFontSize();
    const int size = ui::fontSize(s);
    // Transmutation circles explain themselves when the alchemist comes close.
    for (const Transmuter& t : level_->transmuters()) {
        if ((t.pos - level_->player().body.pos).length() > 110.0f) continue;
        Vector2 sp = scene_.worldToScreen(t.pos - Vec2{0.0f, 36.0f});
        if (t.complete) {
            ui::text("The circle is spent", sp.x, sp.y, small, ui::color::kTextDim, ui::Align::Center);
            continue;
        }
        // Name the control, and whether the flask already holds what the circle craves.
        const char* want = matDef(t.wants).name;
        const bool ready = !run_.flask.empty() && run_.flask.mat == t.wants;
        const char* fmt = ready ? (usingGamepad_ ? "Hold LT to pour your %s in for a perk"
                                                 : "Hold right-click to pour your %s in for a perk")
                                : (usingGamepad_ ? "Fill your flask with %s (hold Y), then pour it in"
                                                 : "Fill your flask with %s (hold Q), then pour it in");
        const std::string pour = TextFormat(fmt, want);
        drawPlate(sp.x, sp.y, static_cast<float>(std::max(ui::measure("Transmutation Circle", size), ui::measure(pour, small))),
                  static_cast<float>(size + small) + 6.0f + 3.0f * fs);
        ui::text("Transmutation Circle", sp.x, sp.y, size, Color{200, 170, 255, 255}, ui::Align::Center);
        ui::text(pour, sp.x, sp.y + static_cast<float>(size) + 2.0f, small, ui::color::kText, ui::Align::Center);
        const float w = 50.0f * s, h = 3.0f * s;
        Rgba m = materialColor(t.wants);
        ui::bar({sp.x - w * 0.5f, sp.y + static_cast<float>(size + small) + 6.0f, w, h},
                static_cast<float>(t.progress) / static_cast<float>(t.required), Color{m.r, m.g, m.b, 255});
    }
}

void GameScene::drawPickupPrompt(const Pickup& p) const {
    const int s = ui::scale();
    const float fs = static_cast<float>(s);
    const int small = ui::smallFontSize();
    const Vector2 sp = scene_.worldToScreen({p.body.pos.x, p.body.pos.y - 16.0f});
    std::string label = (usingGamepad_ ? "[B] " : "[E] ") + p.label();
    if (p.rare) label += " - a rare find";
    const Color labelColor = p.price > 0 && run_.gold < p.price ? ui::color::kDanger : (p.rare ? ui::color::kGold : ui::color::kText);
    struct Line {
        std::string text;
        Color color;
    };
    std::vector<Line> lines;
    if (p.type == PickupType::Spell) {
        const SpellDef& d = spellDef(p.spell);
        static constexpr const char* kTypeLines[] = {"Projectile", "Modifier - boosts the spell after it",
                                                     "Multicast - casts the next spells together"};
        lines.push_back({kTypeLines[static_cast<int>(d.type)], ui::spellTypeColor(p.spell)});
        lines.push_back({spellStats(p.spell), ui::color::kTextDim});
        lines.push_back({d.description, ui::color::kText});
        const Wand* wand = run_.currentWand();
        const bool satchelRoom = std::find(run_.inventory.begin(), run_.inventory.end(), SpellId::None) != run_.inventory.end();
        if (run_.canAutoSlot(p.spell) && wand) lines.push_back({"Goes into " + wand->name, ui::color::kAccent});
        else if (satchelRoom)
            lines.push_back({usingGamepad_ ? "Goes to your satchel - Back to place it" : "Goes to your satchel - Tab to place it",
                             ui::color::kTextDim});
        else lines.push_back({usingGamepad_ ? "Satchel full - Back, then Y on a spell to set it down"
                                            : "Satchel full - Tab, then right-click a spell to set it down",
                              ui::color::kDanger});
    } else if (p.type == PickupType::PerkAltar) {
        lines.push_back({perkDef(p.perk).description, ui::color::kText});
    } else if (p.type == PickupType::Potion) {
        // What the liquid does, and that a different one trades places with the flask's. The same one
        // pours in only what fits: a full flask takes none, and what is left over stays in the bottle.
        lines.push_back({matDescription(p.mat), ui::color::kText});
        const Flask& f = run_.flask;
        if (f.empty() || f.mat == p.mat) {
            const int held = f.empty() ? 0 : f.amount;
            if (held >= Flask::kCapacity) lines.push_back({"Your flask is full", ui::color::kDanger});
            else if (held + p.amount > Flask::kCapacity)
                lines.push_back({"Tops up your flask - the rest stays here", ui::color::kAccent});
            else lines.push_back({"Fills your flask", ui::color::kAccent});
        } else {
            lines.push_back({std::string("Swaps with your flask of ") + matDef(f.mat).name + ", left here in its place",
                             ui::color::kTextDim});
        }
    }
    const int size = ui::fontSize(s);
    const float lineH = static_cast<float>(small) + 2.0f * fs;
    int widest = ui::measure(label, size);
    for (const Line& l : lines) widest = std::max(widest, ui::measure(l.text, small));
    // The card grows upwards from the label's place, clear of the wares beside it, and stays on
    // screen beside a wall where the camera stops.
    const float swF = static_cast<float>(app_.screenWidth());
    const float halfW = static_cast<float>(widest) * 0.5f + 6.0f * fs;
    const float cx = halfW * 2.0f < swF ? clamp(sp.x, halfW, swF - halfW) : swF * 0.5f;
    const float details = lines.empty() ? 0.0f : 3.0f * fs + lineH * static_cast<float>(lines.size());
    const float top = sp.y - details;
    drawPlate(cx, top, static_cast<float>(widest), static_cast<float>(size) + details);
    ui::text(label, cx, top, size, labelColor, ui::Align::Center);
    float y = top + static_cast<float>(size) + 3.0f * fs;
    for (const Line& l : lines) {
        ui::text(l.text, cx, y, small, l.color, ui::Align::Center);
        y += lineH;
    }
    if (p.type == PickupType::Wand && p.wand) drawWandCard(*p.wand, cx, sp.y - 3.0f * fs); // above the prompt, clear of the wand itself
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
        {"  recharge ", ui::color::kTextDim},
        {TextFormat("%.2fs", w.rechargeTime), judge(w.rechargeTime, held ? held->rechargeTime : 0.0f, true)}};
    std::vector<Segment> mana = {
        {"mana ", ui::color::kTextDim},
        {TextFormat("%d", static_cast<int>(w.manaMax)), judge(w.manaMax, held ? held->manaMax : 0.0f, false)},
        {" (+", ui::color::kTextDim},
        {TextFormat("%d", static_cast<int>(w.manaCharge)), judge(w.manaCharge, held ? held->manaCharge : 0.0f, false)},
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
    DrawRectangleRec(card, kPlateColor);
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
    drawVitals();
    drawRunCorner();
    drawHotbar();
    drawFlask();
    drawBossBar();
}

void GameScene::drawVitals() const {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    const Player& pl = level_->player();

    // Health, mana, levitation, breath.
    float x = 10.0f * s, y = 10.0f * s;
    const float barW = 110.0f * s;
    ui::bar({x, y, barW, 7.0f * s}, pl.hp / pl.maxHp, ui::color::kHealth, ui::color::kBarBack, hpTrail_.shown);
    ui::text(TextFormat("%d / %d", static_cast<int>(std::ceil(pl.hp)), static_cast<int>(std::ceil(pl.maxHp))), x + 3.0f * s,
             y + 3.5f * s - static_cast<float>(small) * 0.5f, small, WHITE);
    y += 9.0f * s;
    if (const Wand* wand = run_.currentWand()) {
        const Wand& w = *wand;
        ui::bar({x, y, barW, 4.0f * s}, w.mana / w.manaMax, ui::color::kMana);
        y += 5.0f * s;
        const float reload = w.cooldownTotal > 0.0f ? 1.0f - w.cooldown / w.cooldownTotal : 1.0f;
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
}

void GameScene::drawRunCorner() const {
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale());
    const int small = ui::smallFontSize();
    const float sw = static_cast<float>(app_.screenWidth());

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
}

void GameScene::drawHotbar() const {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    const float sh = static_cast<float>(app_.screenHeight());

    // Wands and the active deck.
    const float icon = 13.0f * s;
    float wx = 10.0f * s, wy = sh - icon - 10.0f * s;
    for (size_t i = 0; i < run_.wands.size(); ++i) {
        const Wand& w = run_.wands[i];
        const bool active = static_cast<int>(i) == run_.activeWand;
        const Rectangle r{wx, wy, icon, icon};
        ui::panel(r, active ? Color{60, 44, 90, 235} : Color{20, 16, 28, 220}, active ? ui::color::kAccent : ui::color::kPanelBorder);
        // Tiny wand glyph.
        const Vector2 a{r.x + 3.0f * s, r.y + r.height - 3.0f * s}, b{r.x + r.width - 3.0f * s, r.y + 3.0f * s};
        DrawLineEx(a, b, 1.5f * s, hexColor(w.handleColor));
        DrawCircleV(b, 1.5f * s, hexColor(w.gemColor));
        ui::text(std::to_string(i + 1), r.x + 1.0f * s, r.y, small, ui::color::kTextDim);
        wx += icon + 3.0f * s;
    }
    if (const Wand* wand = run_.currentWand()) {
        const Wand& w = *wand;
        const float dx = wx + 8.0f * s;
        ui::text(w.name, dx, wy - static_cast<float>(small) - 2.0f * s, small, ui::color::kText);
        const float slot = 11.0f * s;
        const int nextSlot = w.deckPos;
        for (int i = 0; i < w.capacity; ++i) {
            const Rectangle r{dx + static_cast<float>(i) * (slot + 2.0f * s), wy + (icon - slot) * 0.5f, slot, slot};
            // Slot order, not cast order: a shuffle wand gets no read-head highlight.
            const SpellId spell = w.slots[static_cast<size_t>(i)];
            ui::spellIcon(spell, r, i == nextSlot && !w.shuffle);
        }
        // Spells waiting in the satchel: the newest one in a pulsing gold frame, and the way in.
        const float bx = dx + static_cast<float>(w.capacity) * (slot + 2.0f * s) + 10.0f * s;
        const float by = wy + (icon - slot) * 0.5f;
        const bool full = std::none_of(run_.inventory.begin(), run_.inventory.end(), [](SpellId sp) { return sp == SpellId::None; });
        if (satchelNew_ > 0 && satchelLatest_ != SpellId::None) {
            const Rectangle r{bx, by, slot, slot};
            ui::spellIcon(satchelLatest_, r);
            const auto pulse = static_cast<unsigned char>(150.0f + 100.0f * std::sin(time_ * 5.0f));
            DrawRectangleLinesEx({r.x - s, r.y - s, r.width + 2.0f * s, r.height + 2.0f * s}, s, withAlpha(ui::color::kGold, pulse));
            ui::text(TextFormat("+%d  %s", satchelNew_, usingGamepad_ ? "Back" : "Tab"), r.x + slot + 4.0f * s,
                     r.y + (slot - static_cast<float>(small)) * 0.5f, small, ui::color::kGold);
        } else if (full) {
            ui::text("Satchel full", bx, by + (slot - static_cast<float>(small)) * 0.5f, small, ui::color::kDanger);
        }
    }
}

void GameScene::drawFlask() const {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());

    const Flask& f = run_.flask;
    const float fw = 12.0f * s, fh = 18.0f * s;
    const Rectangle r{sw - fw - 12.0f * s, sh - fh - 10.0f * s, fw, fh};
    DrawRectangleRec(r, Color{20, 24, 34, 220});
    if (!f.empty()) {
        const Rgba c = cellColor(makeCell(f.mat, 128), 0, 0, 0);
        const float frac = static_cast<float>(f.amount) / Flask::kCapacity;
        DrawRectangleRec({r.x, r.y + r.height * (1.0f - frac), r.width, r.height * frac}, Color{c.r, c.g, c.b, 255});
    }
    DrawRectangleLinesEx(r, s, Color{190, 220, 240, 200});
    DrawRectangleRec({r.x + r.width * 0.3f, r.y - 3.0f * s, r.width * 0.4f, 3.0f * s}, Color{120, 80, 50, 255});
    const std::string label =
        f.empty() ? "Empty flask" : std::string(matDef(f.mat).name) + TextFormat(" %d%%", f.amount * 100 / Flask::kCapacity);
    // The flask's name and its controls, on a dark plate so they read over ice and sulphur. When a
    // drink would save the alchemist (putting out flames, or healing a bad wound) the legend says
    // so and the flask's frame pulses.
    const char* legend = usingGamepad_ ? "LT pour  Y absorb  X drink" : "RMB pour  Q absorb  F drink";
    Color legendColor = ui::color::kTextDim;
    const Player& pl = level_->player();
    const auto pulse = static_cast<unsigned char>(255.0f * (0.65f + 0.35f * std::sin(time_ * 8.0f)));
    bool urgent = false;
    if (pl.alive() && !f.empty() && burningForReal(pl) && dousesWhenDrunk(f.mat)) {
        legend = usingGamepad_ ? "X drink - puts out the flames" : "F drink - puts out the flames";
        legendColor = Color{255, 170, 80, pulse};
        urgent = true;
    } else if (pl.alive() && f.mat == mat::Vitae && !f.empty() && pl.hp < pl.maxHp * 0.4f) {
        legend = usingGamepad_ ? "X drink - heals" : "F drink - heals";
        legendColor = Color{255, 122, 184, pulse};
        urgent = true;
    }
    if (urgent) DrawRectangleLinesEx({r.x - s, r.y - s, r.width + 2.0f * s, r.height + 2.0f * s}, s, legendColor);
    const float plateW = static_cast<float>(std::max(ui::measure(label, small), ui::measure(legend, small))) + 6.0f * s;
    const float plateY = r.y + r.height - static_cast<float>(small) * 2.2f - 2.0f * s;
    DrawRectangleRec({r.x - 4.0f * s - plateW, plateY, plateW + 2.0f * s, r.y + r.height - plateY + 2.0f * s}, Color{10, 8, 16, 150});
    ui::text(label, r.x - 4.0f * s, r.y + r.height - static_cast<float>(small), small, ui::color::kText, ui::Align::Right);
    ui::text(legend, r.x - 4.0f * s, r.y + r.height - static_cast<float>(small) * 2.2f, small, legendColor, ui::Align::Right);
}

void GameScene::drawBossBar() const {
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale());
    const float sw = static_cast<float>(app_.screenWidth());

    // Boss (or guardian) health.
    if (const Enemy* boss = level_->bossBarTarget()) {
        const float bw = sw * 0.5f;
        const Rectangle r{(sw - bw) * 0.5f, 8.0f * s + static_cast<float>(size), bw, 6.0f * s};
        ui::text(boss->def().name, sw * 0.5f, r.y - static_cast<float>(size) - 3.0f * s, size, ui::color::kGold, ui::Align::Center);
        ui::panel({r.x - 2.0f * s, r.y - 2.0f * s, r.width + 4.0f * s, r.height + 4.0f * s}, Color{12, 10, 18, 220});
        ui::bar(r, boss->hp / boss->maxHp, Color{220, 160, 40, 255}, ui::color::kBarBack,
                boss->id == bossTrailId_ ? bossTrail_.shown : -1.0f);
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
    DrawRectangleLinesEx(r, s, withAlpha(ui::color::kGold, al(0.8f)));
    float y = r.y + 4.0f * s;
    ui::text("FEAT EARNED", sw * 0.5f, y, small, Color{200, 170, 100, al(1.0f)}, ui::Align::Center);
    y += static_cast<float>(small) + 2.0f * s;
    ui::text(def.name, sw * 0.5f, y, size, Color{255, 222, 140, al(1.0f)}, ui::Align::Center);
    y += static_cast<float>(size) + 2.0f * s;
    ui::text(def.description, sw * 0.5f, y, small, Color{205, 195, 215, al(1.0f)}, ui::Align::Center);
}

float GameScene::titleCardHeight() const {
    const int s = ui::scale();
    float h = static_cast<float>(ui::fontSize(s * 3) + ui::fontSize(s + 1)) + 6.0f * static_cast<float>(s);
    if (!titleOmen_.empty()) h += static_cast<float>(ui::fontSize(s + 1)) * 0.5f + static_cast<float>(ui::fontSize(s));
    return h;
}

float GameScene::pickTitleTop() const {
    const float sh = static_cast<float>(app_.screenHeight()), h = titleCardHeight();
    float ys[2] = {scene_.worldToScreen(level_->player().body.pos).y, -1.0e6f};
    if (const Enemy* g = level_->bossBarTarget()) ys[1] = scene_.worldToScreen(g->body.pos).y;
    // The smallest gap between the card's band and either figure.
    auto clearance = [&](float top) {
        const float y0 = top * sh, y1 = y0 + h;
        float gap = 1.0e6f;
        for (float y : ys) gap = std::min(gap, y < y0 ? y0 - y : (y > y1 ? y - y1 : 0.0f));
        return gap;
    };
    return clearance(kTitleTopHigh) >= clearance(kTitleTopLow) ? kTitleTopHigh : kTitleTopLow;
}

void GameScene::drawTitleCard() const {
    if (titleTimer_ <= 0.0f) return;
    float a = std::min(1.0f, titleTimer_ / 1.0f) * std::min(1.0f, (kTitleCardSeconds - titleTimer_) * 2.0f);
    a = clamp(a, 0.0f, 1.0f);
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const int s = ui::scale();
    const float fs = static_cast<float>(s);
    const unsigned char alpha = static_cast<unsigned char>(255.0f * a);
    const int mainSize = ui::fontSize(s * 3), subSize = ui::fontSize(s + 1), omenSize = ui::fontSize(s);
    const float top = (titleTop_ >= 0.0f ? titleTop_ : kTitleTopHigh) * sh;
    const float subY = top + static_cast<float>(mainSize) + 6.0f * fs;

    // A soft dark band behind the block, fading out at both ends.
    float widest = static_cast<float>(std::max(ui::measure(titleMain_, mainSize), ui::measure(titleSub_, subSize)));
    if (!titleOmen_.empty()) widest = std::max(widest, static_cast<float>(ui::measure(titleOmen_, omenSize)));
    const float half = widest * 0.5f + 40.0f * fs;
    const Rectangle band{sw * 0.5f - half, top - 10.0f * fs, half * 2.0f, titleCardHeight() + 20.0f * fs};
    const Color clear{0, 0, 0, 0}, dark{0, 0, 0, static_cast<unsigned char>(140.0f * a)};
    DrawRectangleGradientH(static_cast<int>(band.x), static_cast<int>(band.y), static_cast<int>(half), static_cast<int>(band.height), clear, dark);
    DrawRectangleGradientH(static_cast<int>(sw * 0.5f), static_cast<int>(band.y), static_cast<int>(half), static_cast<int>(band.height), dark, clear);

    ui::text(titleMain_, sw * 0.5f, top, mainSize, Color{240, 226, 200, alpha}, ui::Align::Center);
    ui::text(titleSub_, sw * 0.5f, subY, subSize, Color{190, 170, 220, alpha}, ui::Align::Center);
    // Gold rules either side of the subtitle.
    const float subHalf = static_cast<float>(ui::measure(titleSub_, subSize)) * 0.5f, ruleY = subY + static_cast<float>(subSize) * 0.5f;
    const Color rule = withAlpha(ui::color::kGold, static_cast<uint8_t>(alpha / 2));
    for (float side : {-1.0f, 1.0f}) {
        const float x0 = sw * 0.5f + side * (subHalf + 10.0f * fs), x1 = sw * 0.5f + side * (subHalf + 46.0f * fs);
        DrawRectangleRec({std::min(x0, x1), ruleY, std::abs(x1 - x0), fs}, rule);
    }
    if (!titleOmen_.empty())
        ui::text(titleOmen_, sw * 0.5f, subY + static_cast<float>(subSize) * 1.5f, omenSize, Color{235, 190, 110, alpha}, ui::Align::Center);
}

void GameScene::drawEventNotice() const {
    constexpr float kShow = 3.5f;
    if (eventNotice_.text.empty() || eventNoticeTime_ > kShow) return;
    const float a = clamp(std::min(eventNoticeTime_ * 3.0f, (kShow - eventNoticeTime_) / 0.8f), 0.0f, 1.0f);
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const float s = static_cast<float>(ui::scale());
    Color c = hexColor(eventNotice_.color);
    c.a = static_cast<unsigned char>(255.0f * a);
    // In the larger hand where it fits; a long line in a narrow window takes the body font, and
    // wraps (centred) if even that is too wide.
    const float room = sw - 24.0f * s;
    int size = ui::fontSize(ui::scale() + 1);
    if (static_cast<float>(ui::measure(eventNotice_.text, size)) > room) size = ui::fontSize(ui::scale());
    ui::textWrapped(eventNotice_.text, sw * 0.5f - room * 0.5f, sh * 0.3f, room, size, c, ui::Align::Center);
}

void GameScene::drawPortalBloom() const {
    // Additive, so it reads as light pouring out of the portal rather than a grey wash.
    constexpr uint32_t kHalo = 0x965fff, kCore = 0xebc8ff;
    const Vector2 c = scene_.worldToScreen(level_->exitPos() - Vec2{0.0f, 9.0f});
    const int cx = static_cast<int>(c.x), cy = static_cast<int>(c.y);
    const float k = std::sin(fade_ * PI), sw = static_cast<float>(app_.screenWidth());
    BeginBlendMode(BLEND_ADDITIVE);
    DrawCircleGradient(cx, cy, (0.1f + 1.3f * fade_) * sw, hexColor(kHalo, static_cast<uint8_t>(150.0f * k)), hexColor(kHalo, 0));
    DrawCircleGradient(cx, cy, (0.04f + 0.35f * fade_) * sw, hexColor(kCore, static_cast<uint8_t>(190.0f * k)), hexColor(kCore, 0));
    EndBlendMode();
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
