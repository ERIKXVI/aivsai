// GameScene's wand editor (the satchel of spells): dragging cards between wands and the
// satchel with the mouse or a gamepad cursor, the spell card and the first-cast preview.

#include "scenes/GameScene.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "app/App.hpp"
#include "app/InputLatch.hpp"
#include "render/ColorUtil.hpp"
#include "ui/Ui.hpp"

namespace pm {

namespace {

constexpr float kStickFlick = 0.6f; ///< left-stick deflection that counts as one step of the slot cursor
int stickStep(float axis) { return axis < -kStickFlick ? -1 : (axis > kStickFlick ? 1 : 0); }

constexpr int kSatchelCols = RunState::kInventorySize / 2; ///< the satchel grid: two rows
constexpr int kSatchelRows = RunState::kInventorySize / kSatchelCols;

const char* spellTypeName(SpellType type) {
    switch (type) {
    case SpellType::Projectile: return "Projectile";
    case SpellType::Modifier: return "Modifier";
    case SpellType::Multicast: return "Multicast";
    }
    return "";
}

/// The rules of wands, each beside a card of the kind it describes.
struct Rule {
    SpellId sample;
    const char* text;
};
constexpr Rule kRules[] = {
    {SpellId::SparkBolt, "Spells cast from left to right; when the deck runs out the wand recharges."},
    {SpellId::DamageUp, "Green modifiers boost the projectile that follows them."},
    {SpellId::DoubleCast, "Gold multicasts fire the next spells together."},
    {SpellId::SparkTrigger, "Triggers (the pink dot) cast the next spell where they land."},
};

/// Height the rules card needs at `cardWidth`, so the editor panel can grow to hold it.
float rulesHeight(float cardWidth) {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    const float icon = 12.0f * s, textW = cardWidth - 16.0f * s - icon - 4.0f * s;
    float h = 8.0f * s + static_cast<float>(small) + 6.0f * s;
    for (const Rule& rule : kRules) h += std::max(icon, ui::wrappedHeight(rule.text, textW, small)) + 5.0f * s;
    return h + 8.0f * s;
}

/// The wand editor's info card: details of `shown`, or the rules of wands when nothing is
/// under the cursor.
void drawSpellCard(Rectangle card, SpellId shown) {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    ui::panel(card, Color{12, 9, 18, 230});
    const float cx = card.x + 8.0f * s, cw = card.width - 16.0f * s;
    float cy = card.y + 8.0f * s;
    if (shown != SpellId::None) {
        const SpellDef& d = spellDef(shown);
        ui::spellIcon(shown, {cx, cy, 24.0f * s, 24.0f * s});
        ui::text(d.name, cx + 30.0f * s, cy + 2.0f * s, small, ui::spellTypeColor(shown));
        ui::text(spellTypeName(d.type), cx + 30.0f * s, cy + 4.0f * s + static_cast<float>(small), small,
                 ui::color::kTextDim);
        cy += 32.0f * s;
        ui::textWrapped(spellDetails(shown), cx, cy, cw, small, ui::color::kText);
        return;
    }
    ui::text("The art of wands", cx, cy, small, ui::color::kGold);
    cy += static_cast<float>(small) + 6.0f * s;
    const float icon = 12.0f * s;
    for (const Rule& rule : kRules) {
        ui::spellIcon(rule.sample, {cx, cy, icon, icon});
        const float h = ui::textWrapped(rule.text, cx + icon + 4.0f * s, cy, cw - icon - 4.0f * s, small, ui::color::kTextDim);
        cy += std::max(icon, h) + 5.0f * s;
    }
}

constexpr float kCardW = 130.0f;                ///< the info card's width, in layout units
constexpr Color kStatColor{140, 130, 150, 255}; ///< a wand's figures and its first-cast line
/// What the perk column says while no perk is held.
constexpr const char* kNoPerks = "None yet - sanctum altars, transmutation circles and guardians grant them";

/// Height the perk column needs: a line per perk held, or the note that none is, wrapped to `noPerksW`.
float perksHeight(const RunState& run, float noPerksW) {
    const int small = ui::smallFontSize();
    int perksHeld = 0;
    for (int p = 0; p < kPerkCount; ++p) perksHeld += run.perkCount(static_cast<PerkId>(p)) > 0 ? 1 : 0;
    return perksHeld > 0 ? static_cast<float>(perksHeld) * static_cast<float>(small) * 1.2f
                         : ui::wrappedHeight(kNoPerks, noPerksW, small);
}

/// The line of a wand's figures with its cast delay and recharge time.
const char* timingLine(const Wand& w) { return TextFormat("delay %.2fs  recharge %.2fs", w.castDelay, w.rechargeTime); }

/// The editor's panel, with its title and the line naming the controls.
void drawEditorPanel(Rectangle panelR, bool pad) {
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale());
    const int small = ui::smallFontSize();
    ui::panel(panelR);
    ui::text("SATCHEL OF SPELLS", panelR.x + panelR.width * 0.5f, panelR.y + 6.0f * s, size, ui::color::kGold, ui::Align::Center);
    ui::text(pad ? "D-pad moves. A picks up and places a spell, Y returns it to the satchel or sets it down, B closes."
                 : "Drag spells to move them. Right-click returns a spell to the satchel or sets it down. Tab closes.",
             panelR.x + panelR.width * 0.5f, panelR.y + 8.0f * s + static_cast<float>(size), small, ui::color::kTextDim,
             ui::Align::Center);
}

} // namespace

SpellId& GameScene::editorSlot(int wand, int slot) {
    return wand < 0 ? run_.inventory[static_cast<size_t>(slot)] : run_.wands[static_cast<size_t>(wand)].slots[static_cast<size_t>(slot)];
}

void GameScene::dropDragOnto(int wand, int slot) {
    // A satchel spell dropped onto a wand's spell goes in front of it while the wand has room
    // further along, so a modifier lands before the spell it boosts; anything else swaps.
    const bool fromSatchel = drag_.wand < 0 && wand >= 0 && editorSlot(wand, slot) != SpellId::None;
    if (fromSatchel && insertSpell(run_.wands[static_cast<size_t>(wand)].slots, slot, drag_.spell))
        editorSlot(drag_.wand, drag_.slot) = SpellId::None;
    else std::swap(editorSlot(drag_.wand, drag_.slot), editorSlot(wand, slot));
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

void GameScene::takeSpellOut(int wand, int slot) {
    SpellId& spell = editorSlot(wand, slot);
    if (wand >= 0 && run_.addSpell(spell)) {
        spell = SpellId::None;
        app_.audio().play(Sfx::UiClick, 0.6f);
        return;
    }
    // From the satchel (or a wand, with the satchel full) the spell is set down at the alchemist's feet
    // with a little toss, to be picked up again with E. Within reach of anything else E could take (a
    // shop's ware over its pedestal, a spell on the floor, a chest) it lands a step aside, away from the
    // nearest: right under the alchemist it would take E from what it was set down to make room for (see
    // Level::focusedPickup). Spells set down earlier do not count; they still lie at the feet while the
    // editor is open.
    const Vec2 at = level_->player().body.pos;
    float aside = 0.0f, nearest = 16.0f * 16.0f; // the reach of E, squared
    for (const Pickup& p : level_->pickups()) {
        const float d = (p.body.pos - at).lengthSq();
        if (p.dead || !p.interactive() || (p.type == PickupType::Chest && p.opened) || p.setDown || d >= nearest) continue;
        nearest = d;
        aside = p.body.pos.x > at.x ? -30.0f : 30.0f;
    }
    Pickup dropped = makeSpellPickup(at, spell);
    dropped.body.vel = {aside, -80.0f};
    dropped.setDown = true;
    level_->spawnPickup(std::move(dropped));
    spell = SpellId::None;
    app_.audio().play(Sfx::UiClick, 0.6f, 0.8f);
}

void GameScene::syncStickFlick() {
    stickX_ = stickStep(input::padAxis(GAMEPAD_AXIS_LEFT_X));
    stickY_ = stickStep(input::padAxis(GAMEPAD_AXIS_LEFT_Y));
}

void GameScene::updateEditorPad() {
    if (!input::padAvailable()) return;
    // The cursor walks a grid: one row per wand, then the satchel's two rows of eight.
    const int wands = static_cast<int>(run_.wands.size());
    auto rowWidth = [&](int row) { return row < wands ? run_.wands[static_cast<size_t>(row)].capacity : kSatchelCols; };
    int row = padCursor_.wand >= 0 ? padCursor_.wand : wands + padCursor_.slot / kSatchelCols;
    int col = padCursor_.wand >= 0 ? padCursor_.slot : padCursor_.slot % kSatchelCols;
    row = std::clamp(row, 0, wands + kSatchelRows - 1);
    col = std::clamp(col, 0, rowWidth(row) - 1);

    int dx = 0, dy = 0;
    if (input::padPressed(GAMEPAD_BUTTON_LEFT_FACE_LEFT)) dx = -1;
    if (input::padPressed(GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) dx = 1;
    if (input::padPressed(GAMEPAD_BUTTON_LEFT_FACE_UP)) dy = -1;
    if (input::padPressed(GAMEPAD_BUTTON_LEFT_FACE_DOWN)) dy = 1;
    // A flick of the left stick moves one slot.
    const int prevX = stickX_, prevY = stickY_;
    syncStickFlick();
    if (stickX_ != prevX && stickX_ != 0) dx = stickX_;
    if (stickY_ != prevY && stickY_ != 0) dy = stickY_;
    if (dx != 0 || dy != 0) {
        row = std::clamp(row + dy, 0, wands + kSatchelRows - 1);
        col = std::clamp(col + dx, 0, rowWidth(row) - 1);
        app_.audio().play(Sfx::UiHover, 0.4f);
    }
    padCursor_ = row < wands ? SlotRef{row, col} : SlotRef{-1, (row - wands) * kSatchelCols + col};

    SpellId& at = editorSlot(padCursor_.wand, padCursor_.slot);
    if (input::padPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
        if (drag_.active) dropDragOnto(padCursor_.wand, padCursor_.slot);
        else if (at != SpellId::None) drag_ = {true, at, padCursor_.wand, padCursor_.slot};
    }
    if (input::padPressed(GAMEPAD_BUTTON_RIGHT_FACE_UP) && !drag_.active && at != SpellId::None)
        takeSpellOut(padCursor_.wand, padCursor_.slot);
}

/// Where the wand editor's parts go, worked out before any of it is drawn.
struct GameScene::InventoryLayout {
    float slot = 0.0f, gap = 0.0f;  ///< a spell slot's side and the space between two
    float wandRowH = 0.0f;          ///< one wand row: its slots and first-cast line, or its figures where those are taller
    Rectangle panelR{};             ///< the panel, centred and sized to hold all of it
    float perkX = 0.0f;             ///< the perk column, from the panel's left edge
    float noPerksW = 0.0f;          ///< how wide the note that no perk is held may run
    float captionW = 0.0f;          ///< how wide the guide's caption under the satchel may run
    float sx = 0.0f, slotsW = 0.0f; ///< where the wands' slots and first-cast lines start, past the figures, and their room
    WandGuide guide;                ///< the guided first edit, inactive once it has been done
    const char* caption = "";       ///< what the guide's caption says
};

/// What drawing the editor found: the slot under the mouse or the pad cursor, the guide's two
/// slots and a tooltip for the wand figures or perk under the mouse.
struct GameScene::InventoryHover {
    Vector2 mouse{};                    ///< read once, before the slots are drawn
    SpellId spell = SpellId::None;      ///< the spell in the slot under the cursor
    int wand = -2;                      ///< that slot's wand: -1 for the satchel, -2 while the cursor is on no slot
    int slot = -1;                      ///< that slot's place in its wand or the satchel
    Rectangle cursorR{};                ///< that slot's rectangle
    Rectangle guideFromR{}, guideToR{}; ///< the guide's satchel spell and the slot it goes to
    std::string wandTip, wandTipTitle;  ///< the tooltip for the wand figures or perk under the mouse
};

void GameScene::drawInventory() {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    const bool pad = usingGamepad_;
    ui::dimScreen(160);
    const InventoryLayout lay = layoutInventory();
    const Rectangle& panelR = lay.panelR;
    drawEditorPanel(panelR, pad);

    InventoryHover hover;
    hover.mouse = GetMousePosition();
    float y = drawWandRows(lay, hover);
    // Satchel grid, right under the wands.
    y += static_cast<float>(small) + 4.0f * s;
    const float satchelBottom = drawSatchelGrid(lay, y, hover);
    drawPerksAndRunFigures(lay, y, hover);

    // Info card: the spell under the cursor, or the rules of wands.
    drawSpellCard({panelR.x + panelR.width - (kCardW + 10.0f) * s, panelR.y + 32.0f * s, kCardW * s, panelR.height - 56.0f * s},
                  drag_.active ? drag_.spell : hover.spell);

    if (lay.guide.active() && !drag_.active) drawWandGuide(lay, hover, satchelBottom);
    drawPadCursor(hover);

    // Drag & drop: a press picks a spell up and the release drops it where it lands. Released over
    // its own slot it stays in hand, and the next press puts it down (or back, beside the slots):
    // the only way a touchpad tap can place, as its release falls in the same poll and raylib
    // never reports it.
    if (!pad) handleInventoryClicks(hover);
    if (drag_.active) {
        dragHeldSpell(lay, hover);
    } else if (hover.spell == SpellId::None && !hover.wandTip.empty()) {
        ui::tooltip(hover.wandTipTitle, hover.wandTip, ui::color::kGold);
    }
}

GameScene::InventoryLayout GameScene::layoutInventory() const {
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    const bool pad = usingGamepad_;
    const float slot = 16.0f * s, gap = 2.0f * s;
    // One wand row: its slots and first-cast line, or its four lines of figures if those are taller
    // (at the smallest UI scale).
    const float wandRowH = std::max(slot * 1.9f + 6.0f * s, 4.0f * (static_cast<float>(small) + s) + 4.0f * s);
    // The layout's 520 units across, or what a narrow (4:3) window has: all that runs across the
    // panel (the slots, the perk column, the info card) is placed from its edges.
    const float panelW = std::min(520.0f * s, sw - 8.0f * s);

    // Guided first edit: until a satchel spell has been moved onto a wand once, a satchel spell
    // worth using and its destination glow, with a ghost icon drifting between them. An attack
    // spell goes into an empty slot; a modifier or multicast goes onto the wand's first attack
    // spell (never a digging one), which it then sits in front of. The hardest-hitting wand comes
    // first, and never a wand whose only attack spells dig.
    const WandGuide guide = hints_.seen(Hint::WandGuide) ? WandGuide{} : pickWandGuide(run_);
    const char* caption = HintTracker::text(Hint::WandGuide, pad);
    if (guide.booster)
        caption = pad ? "Pick up the glowing booster with A and place it on the glowing spell - it boosts the spell after it"
                      : "Drag the glowing booster onto the glowing spell - it goes in front and boosts the spell after it";

    // Under the wands, the satchel's two rows with the perks in a column beside them (offsets from
    // the panel's left edge and top). The guide's caption goes under the satchel, across to the info
    // card, unless the perk column runs down past the satchel: then only as wide as the satchel.
    const float perkX = 10.0f * s + static_cast<float>(kSatchelCols) * (slot + gap) + 14.0f * s;
    const float satchelTop = 32.0f * s + wandRowH * static_cast<float>(run_.wands.size()) + static_cast<float>(small) + 4.0f * s;
    const float satchelH = static_cast<float>(kSatchelRows) * (slot + gap);
    const float noPerksW = panelW - 146.0f * s - perkX;
    const float perksH = perksHeight(run_, noPerksW);
    const float captionW = perksH > satchelH ? perkX - 18.0f * s : panelW - (kCardW + 30.0f) * s;
    const float captionH = guide.active() ? ui::wrappedHeight(caption, captionW, small) : 0.0f;
    // Tall enough for every wand row, the satchel with the caption under it (clear of the run's
    // figures at the foot) and the info card with the rules of wands.
    const float panelH = std::max({200.0f * s, 104.0f * s + wandRowH * static_cast<float>(run_.wands.size()),
                                   56.0f * s + rulesHeight(kCardW * s),
                                   satchelTop + satchelH + 3.0f * s + captionH + static_cast<float>(small) + 6.0f * s});
    const Rectangle panelR{sw * 0.5f - panelW * 0.5f, sh * 0.5f - panelH * 0.5f, panelW, panelH};

    // The figures column fits its widest line: at the smallest UI scale the small font is large
    // for the layout. The slots and the first-cast line start after it and end before the card.
    float figuresW = 104.0f * s;
    for (const Wand& w : run_.wands) figuresW = std::max(figuresW, static_cast<float>(ui::measure(timingLine(w), small)));
    const float sx = panelR.x + 14.0f * s + figuresW;
    const float slotsW = panelR.x + panelR.width - (kCardW + 16.0f) * s - sx;
    return {slot, gap, wandRowH, panelR, perkX, noPerksW, captionW, sx, slotsW, guide, caption};
}

float GameScene::drawWandRows(const InventoryLayout& lay, InventoryHover& hover) const {
    const float s = static_cast<float>(ui::scale());
    float y = lay.panelR.y + 32.0f * s;
    for (size_t wi = 0; wi < run_.wands.size(); ++wi) {
        const Wand& w = run_.wands[wi];
        drawWandFigures(lay, wi, y, hover);
        // A wand widened past the room left (Expansion perks) gets smaller slots.
        const float cell = std::min(lay.slot + lay.gap, lay.slotsW / static_cast<float>(std::max(1, w.capacity)));
        for (int i = 0; i < w.capacity; ++i) {
            const Rectangle r{lay.sx + static_cast<float>(i) * cell, y, cell - lay.gap, cell - lay.gap};
            drawEditorSlot(static_cast<int>(wi), i, r, w.slots[static_cast<size_t>(i)], hover);
            if (lay.guide.active() && lay.guide.wand == static_cast<int>(wi) && lay.guide.slot == i) hover.guideToR = r;
        }
        drawFirstCastLine(lay, wi, y);
        y += lay.wandRowH;
    }
    return y;
}

void GameScene::drawWandFigures(const InventoryLayout& lay, size_t wi, float y, InventoryHover& hover) const {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    const Wand& w = run_.wands[wi];
    float x = lay.panelR.x + 10.0f * s;
    const Rectangle nameR{x, y, 104.0f * s, lay.slot};
    ui::text(w.name, x, y, small, static_cast<int>(wi) == run_.activeWand ? ui::color::kText : ui::color::kTextDim);
    ui::text(timingLine(w), x, y + static_cast<float>(small) + 1.0f * s, small, kStatColor);
    const float statLine = static_cast<float>(small) + 1.0f * s;
    ui::text(TextFormat("mana %d (+%d/s)", static_cast<int>(w.manaMax), static_cast<int>(w.manaCharge)), x, y + statLine * 2.0f,
             small, kStatColor);
    // The deck's quirks on a line of their own, so they never run into the slots.
    float fx = x;
    if (w.shuffle) {
        ui::text("shuffle", fx, y + statLine * 3.0f, small, kStatColor);
        fx += static_cast<float>(ui::measure("shuffle  ", small));
    }
    if (w.spellsPerCast > 1) ui::text("multicast", fx, y + statLine * 3.0f, small, ui::color::kAccent);
    if (!usingGamepad_ && ui::hovered({nameR.x, nameR.y, nameR.width, statLine * 4.0f})) {
        hover.wandTipTitle = w.name;
        hover.wandTip = TextFormat("Spread %.1f deg. Spells per cast %d. Capacity %d.%s", w.spreadDeg, w.spellsPerCast,
                                   w.capacity, w.shuffle ? " Shuffle: casts spells in random order." : "");
    }
}

void GameScene::drawEditorSlot(int wand, int slotIndex, Rectangle r, SpellId sp, InventoryHover& hover) const {
    const bool isDragSrc = drag_.active && drag_.wand == wand && drag_.slot == slotIndex;
    // With a pad, the cursor slot is what the mouse would be pointing at.
    const bool pointed = usingGamepad_ ? padCursor_.wand == wand && padCursor_.slot == slotIndex
                                       : CheckCollisionPointRec(hover.mouse, r);
    ui::spellIcon(isDragSrc ? SpellId::None : sp, r, pointed);
    if (pointed) {
        hover.spell = sp;
        hover.wand = wand;
        hover.slot = slotIndex;
        hover.cursorR = r;
    }
}

void GameScene::drawFirstCastLine(const InventoryLayout& lay, size_t wi, float y) const {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    const Wand& w = run_.wands[wi];
    // What the first cast of this deck does, so the effect of modifiers is visible. It
    // flashes gold after an edit changes it.
    int pos = 0;
    const CastShot first = buildShot(w.slots, w.order, pos, w.spellsPerCast);
    float damage = 0.0f;
    for (const ProjectileLaunch& l : first.projectiles) damage += l.def.damage + l.def.explosionDamage;
    const int count = static_cast<int>(first.projectiles.size());
    Color firstColor = kStatColor;
    if (firstCastWand_ == static_cast<int>(wi) && firstCastFlash_ > 0.0f)
        firstColor = ColorLerp(kStatColor, ui::color::kGold, std::min(1.0f, firstCastFlash_ / 0.5f));
    // A booster at the end of an ordered deck is wasted; say so in place of the preview.
    const SpellId dangling = w.shuffle ? SpellId::None : trailingBooster(w.slots, w.spellsPerCast);
    if (dangling != SpellId::None)
        ui::textWrapped(TextFormat("%s has nothing after it to work on", spellDef(dangling).name), lay.sx,
                        y + lay.slot + 3.0f * s, lay.slotsW, small, ui::color::kDanger);
    else if (count > 0)
        ui::textWrapped(TextFormat("first cast: %d projectile%s, %d damage, %d mana", count, count == 1 ? "" : "s",
                                   static_cast<int>(damage), static_cast<int>(first.mana)),
                        lay.sx, y + lay.slot + 3.0f * s, lay.slotsW, small, firstColor);
}

float GameScene::drawSatchelGrid(const InventoryLayout& lay, float y, InventoryHover& hover) const {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    const float slot = lay.slot, gap = lay.gap;
    ui::text("Satchel", lay.panelR.x + 10.0f * s, y - static_cast<float>(small) - 2.0f * s, small, ui::color::kTextDim);
    for (int i = 0; i < RunState::kInventorySize; ++i) {
        const int col = i % kSatchelCols, row = i / kSatchelCols;
        const Rectangle r{lay.panelR.x + 10.0f * s + static_cast<float>(col) * (slot + gap),
                          y + static_cast<float>(row) * (slot + gap), slot, slot};
        drawEditorSlot(-1, i, r, run_.inventory[static_cast<size_t>(i)], hover);
        if (lay.guide.active() && lay.guide.from == i) hover.guideFromR = r;
    }
    return y + static_cast<float>(kSatchelRows) * (slot + gap);
}

void GameScene::drawPerksAndRunFigures(const InventoryLayout& lay, float y, InventoryHover& hover) const {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    const Rectangle& panelR = lay.panelR;
    // Perks beside the satchel.
    const float px = panelR.x + lay.perkX;
    ui::text("Perks", px, y - static_cast<float>(small) - 2.0f * s, small, ui::color::kTextDim);
    float py = y;
    bool anyPerk = false;
    for (int p = 0; p < kPerkCount; ++p) {
        const int n = run_.perkCount(static_cast<PerkId>(p));
        if (n == 0) continue;
        const PerkDef& d = perkDef(static_cast<PerkId>(p));
        const std::string label = n > 1 ? std::string(d.name) + " x" + std::to_string(n) : d.name;
        ui::text(label, px, py, small, hexColor(d.color));
        anyPerk = true;
        if (!usingGamepad_ && ui::hovered({px, py, 100.0f * s, static_cast<float>(small)})) {
            hover.wandTipTitle = d.name;
            hover.wandTip = d.description;
        }
        py += static_cast<float>(small) * 1.2f;
        if (py > panelR.y + panelR.height - 14.0f * s) break;
    }
    if (!anyPerk) ui::textWrapped(kNoPerks, px, py, lay.noPerksW, small, ui::color::kTextDim);
    ui::text(TextFormat("Kills %d   Gold %d   Time %s", run_.stats.kills, run_.gold,
                        ui::formatClock(run_.stats.time).c_str()),
             panelR.x + panelR.width - 10.0f * s, panelR.y + panelR.height - static_cast<float>(small) - 6.0f * s, small,
             ui::color::kTextDim, ui::Align::Right);
}

void GameScene::drawWandGuide(const InventoryLayout& lay, const InventoryHover& hover, float satchelBottom) const {
    const float s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    const float slot = lay.slot;
    const Rectangle& guideFromR = hover.guideFromR;
    const Rectangle& guideToR = hover.guideToR;
    const float t = time_;
    const unsigned char glow = static_cast<unsigned char>(140.0f + 100.0f * std::sin(t * 6.0f));
    const Color gold = withAlpha(ui::color::kGold, glow);
    const float m = 2.0f * s;
    DrawRectangleLinesEx({guideFromR.x - m, guideFromR.y - m, guideFromR.width + 2 * m, guideFromR.height + 2 * m}, s, gold);
    DrawRectangleLinesEx({guideToR.x - m, guideToR.y - m, guideToR.width + 2 * m, guideToR.height + 2 * m}, s, gold);
    // The ghost eases from the satchel to the slot, fading in and out at the ends.
    const float k = std::fmod(t * 0.7f, 1.0f), e = k * k * (3.0f - 2.0f * k);
    const Rectangle ghost{guideFromR.x + (guideToR.x - guideFromR.x) * e, guideFromR.y + (guideToR.y - guideFromR.y) * e,
                          slot, slot};
    ui::spellIcon(run_.inventory[static_cast<size_t>(lay.guide.from)], ghost, false);
    DrawRectangleRec(ghost, Color{14, 12, 20, static_cast<unsigned char>(255.0f * (1.0f - std::sin(k * PI)))});
    ui::textWrapped(lay.caption, lay.panelR.x + 10.0f * s, satchelBottom + 3.0f * s, lay.captionW, small, ui::color::kGold);
}

void GameScene::drawPadCursor(InventoryHover& hover) const {
    const float s = static_cast<float>(ui::scale());
    Rectangle& cursorR = hover.cursorR;
    if (usingGamepad_ && !hints_.seen(Hint::WandGuide) && cursorR.width <= 0.0f) cursorR = hover.guideFromR;
    if (usingGamepad_ && cursorR.width > 0.0f) {
        const float m = 1.0f * s;
        DrawRectangleLinesEx({cursorR.x - m, cursorR.y - m, cursorR.width + 2 * m, cursorR.height + 2 * m}, s, ui::color::kGold);
    }
}

void GameScene::handleInventoryClicks(const InventoryHover& hover) {
    const bool pressed = input::mousePressed(MOUSE_BUTTON_LEFT);
    if (pressed && drag_.active) {
        if (hover.wand != -2) dropDragOnto(hover.wand, hover.slot);
        else drag_ = Drag{};
    } else if (pressed && hover.wand != -2 && hover.spell != SpellId::None) {
        drag_ = {true, hover.spell, hover.wand, hover.slot};
    }
    // Not while a spell is in hand (as with the pad's Y): emptying its slot would leave it held from
    // nowhere. Nor in the frame a left press moved spells: `hover` predates the move.
    if (input::mousePressed(MOUSE_BUTTON_RIGHT) && !pressed && !drag_.active && hover.spell != SpellId::None)
        takeSpellOut(hover.wand, hover.slot);
}

void GameScene::dragHeldSpell(const InventoryLayout& lay, const InventoryHover& hover) {
    const float slot = lay.slot;
    // The held spell follows the mouse, or floats above the pad cursor.
    const Rectangle r = usingGamepad_ ? Rectangle{hover.cursorR.x + slot * 0.4f, hover.cursorR.y - slot * 0.6f, slot, slot}
                                      : Rectangle{hover.mouse.x - slot * 0.5f, hover.mouse.y - slot * 0.5f, slot, slot};
    ui::spellIcon(drag_.spell, r, true);
    const bool overSource = hover.wand == drag_.wand && hover.slot == drag_.slot;
    if (!usingGamepad_ && input::mouseReleased(MOUSE_BUTTON_LEFT) && !overSource) {
        if (hover.wand != -2) dropDragOnto(hover.wand, hover.slot);
        else drag_ = Drag{};
    }
}

} // namespace pm
