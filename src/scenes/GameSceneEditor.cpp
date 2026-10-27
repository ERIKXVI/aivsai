// GameScene's wand editor (the satchel of spells): dragging cards between wands and the
// satchel with the mouse or a gamepad cursor, the spell card and the first-cast preview.

#include "scenes/GameScene.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "app/App.hpp"
#include "app/InputLatch.hpp"
#include "render/ColorUtil.hpp"
#include "ui/Ui.hpp"

namespace pm {

namespace {

constexpr float kStickFlick = 0.6f; ///< left-stick deflection that counts as one step of the slot cursor
int stickStep(float axis) { return axis < -kStickFlick ? -1 : (axis > kStickFlick ? 1 : 0); }

const char* spellTypeName(SpellType type) {
    switch (type) {
    case SpellType::Projectile: return "Projectile";
    case SpellType::Modifier: return "Modifier";
    case SpellType::Multicast: return "Multicast";
    }
    return "";
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

void GameScene::syncStickFlick() {
    stickX_ = stickStep(GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X));
    stickY_ = stickStep(GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y));
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
    const int sx = stickStep(GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X));
    const int sy = stickStep(GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y));
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
    const int small = ui::smallFontSize();
    const bool pad = usingGamepad_;
    ui::dimScreen(160);

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

    // Guided first edit: until a satchel spell has been moved onto a wand once, a satchel spell
    // worth using and its destination glow, with a ghost icon drifting between them. An attack
    // spell goes into an empty slot; a modifier or multicast goes onto the wand's first attack
    // spell, which it then sits in front of. The hardest-hitting wand comes first, never the
    // digging wand by accident.
    int guideFrom = -1, guideWand = -1, guideSlot = -1;
    bool guideBooster = false;
    if (!hints_.seen(Hint::WandGuide)) {
        const auto from = std::find_if(run_.inventory.begin(), run_.inventory.end(), guideCandidate);
        if (from != run_.inventory.end()) {
            guideFrom = static_cast<int>(from - run_.inventory.begin());
            guideBooster = spellDef(*from).type != SpellType::Projectile;
        }
        std::vector<std::pair<float, int>> byDamage; // (first-cast damage, wand), hardest first
        for (size_t wi = 0; wi < run_.wands.size(); ++wi) {
            int pos = 0;
            const Wand& w = run_.wands[wi];
            float damage = 0.0f;
            for (const ProjectileLaunch& l : buildShot(w.slots, w.order, pos, w.spellsPerCast).projectiles) damage += l.def.damage;
            byDamage.emplace_back(-damage, static_cast<int>(wi));
        }
        std::stable_sort(byDamage.begin(), byDamage.end());
        for (const auto& entry : byDamage) {
            const int wi = entry.second;
            const auto& slots = run_.wands[static_cast<size_t>(wi)].slots;
            const auto empty = std::find(slots.begin(), slots.end(), SpellId::None);
            if (empty == slots.end()) continue;
            auto to = empty;
            if (guideBooster) {
                // Onto the first attack spell that has room behind it to move along; a shuffle
                // wand has no "after it" to boost.
                if (run_.wands[static_cast<size_t>(wi)].shuffle) continue;
                to = std::find_if(slots.begin(), slots.end(), [](SpellId s) { return s != SpellId::None && spellDef(s).type == SpellType::Projectile; });
                if (to == slots.end() || std::find(to + 1, slots.end(), SpellId::None) == slots.end()) continue;
            }
            guideWand = wi;
            guideSlot = static_cast<int>(to - slots.begin());
            break;
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
        // A booster at the end of an ordered deck is wasted; say so in place of the preview.
        const SpellId dangling = w.shuffle ? SpellId::None : trailingBooster(w.slots);
        if (dangling != SpellId::None)
            ui::text(TextFormat("%s has nothing after it to work on", spellDef(dangling).name), sx, y + slot + 3.0f * s, small,
                     ui::color::kDanger);
        else if (count > 0)
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
    ui::text(TextFormat("Kills %d   Gold %d   Time %s", run_.stats.kills, run_.gold,
                        ui::formatClock(run_.stats.time).c_str()),
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
        const char* caption = HintTracker::text(Hint::WandGuide, pad);
        if (guideBooster)
            caption = pad ? "Pick up the glowing booster with A and place it on the glowing spell - it boosts the spell after it"
                          : "Drag the glowing booster onto the glowing spell - it goes in front and boosts the spell after it";
        ui::text(caption, panelR.x + 10.0f * s, satchelBottom + 3.0f * s, small, ui::color::kGold);
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

} // namespace pm
