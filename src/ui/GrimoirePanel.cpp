#include "ui/GrimoirePanel.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

#include "app/App.hpp"
#include "app/InputLatch.hpp"
#include "app/RunRecords.hpp"
#include "game/Enemy.hpp"
#include "game/Loadouts.hpp"
#include "game/Perks.hpp"
#include "game/Spells.hpp"
#include "render/ColorUtil.hpp"
#include "render/Sprites.hpp"
#include "ui/Ui.hpp"

namespace pm {

namespace {

constexpr float kPanelW = 440.0f, kPanelH = 300.0f;

/// Creatures listed in the Grimoire (the homunculus companion is a perk, not a foe).
bool listedCreature(EnemyType t) { return t != EnemyType::Companion; }

int countBits(uint64_t v) {
    int n = 0;
    for (; v; v &= v - 1) ++n;
    return n;
}

bool spellKnown(const Profile& p, int i) { return (p.spellsSeen >> i) & 1ull; }

std::string clockTime(float seconds) {
    const int t = static_cast<int>(seconds);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d", t / 60, t % 60);
    return buf;
}

} // namespace

bool GrimoirePanel::draw(App& app) {
    const Profile& p = app.profile();
    const float sw = static_cast<float>(app.screenWidth()), sh = static_cast<float>(app.screenHeight());
    const float s = static_cast<float>(ui::scale());
    const int small = ui::fontSize(std::max(1, ui::scale() - 1));
    DrawRectangle(0, 0, app.screenWidth(), app.screenHeight(), Color{0, 0, 0, 150});
    Rectangle r{sw * 0.5f - kPanelW * 0.5f * s, sh * 0.5f - kPanelH * 0.5f * s, kPanelW * s, kPanelH * s};
    ui::panel(r);
    ui::text("GRIMOIRE", sw * 0.5f, r.y + 8.0f * s, ui::fontSize(ui::scale() + 1), ui::color::kGold, ui::Align::Center);

    int spells = 0;
    for (int i = 1; i < kSpellCount; ++i) spells += spellKnown(p, i) ? 1 : 0;
    int creatures = 0, creatureTotal = 0;
    for (int i = 0; i < static_cast<int>(EnemyType::Count); ++i) {
        if (!listedCreature(static_cast<EnemyType>(i))) continue;
        ++creatureTotal;
        creatures += p.creatureKills[static_cast<size_t>(i)] > 0 ? 1 : 0;
    }
    const int perks = countBits(p.perksSeen);

    // Tabs double as the completion summary.
    constexpr int kTabs = static_cast<int>(Tab::Count);
    const std::string labels[kTabs] = {TextFormat("Spells %d/%d", spells, kSpellCount - 1),
                                       TextFormat("Creatures %d/%d", creatures, creatureTotal),
                                       TextFormat("Perks %d/%d", perks, kPerkCount), "Chronicle"};
    const float tabW = 98.0f * s, tabH = 16.0f * s, tabGap = 6.0f * s;
    float tx = sw * 0.5f - (kTabs * tabW + (kTabs - 1) * tabGap) * 0.5f;
    for (int i = 0; i < kTabs; ++i) {
        Rectangle tr{tx + static_cast<float>(i) * (tabW + tabGap), r.y + 28.0f * s, tabW, tabH};
        if (ui::button(tr, labels[i], true, static_cast<int>(tab_) == i)) tab_ = static_cast<Tab>(i);
    }

    // Left/right (A/D, D-pad) flip between the tabs.
    const bool left = input::keyPressed(KEY_LEFT) || input::keyPressed(KEY_A) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT);
    const bool right = input::keyPressed(KEY_RIGHT) || input::keyPressed(KEY_D) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT);
    if (left || right) tab_ = static_cast<Tab>((static_cast<int>(tab_) + (right ? 1 : kTabs - 1)) % kTabs);

    const float cx = r.x + 20.0f * s, cy = r.y + 54.0f * s;
    switch (tab_) {
    case Tab::Spells: drawSpells(app, cx, cy); break;
    case Tab::Creatures: drawCreatures(app, cx, cy); break;
    case Tab::Perks: drawPerks(app, cx, cy); break;
    case Tab::Chronicle: drawChronicle(app, cx, cy); break;
    case Tab::Count: break;
    }

    std::string footer = TextFormat("Chests opened %d    Creatures slain %d    Gold gathered %d", p.chestsOpened, p.totalKills,
                                    p.totalGold);
    ui::text(footer, sw * 0.5f, r.y + r.height - 44.0f * s, small, ui::color::kTextDim, ui::Align::Center);
    return ui::button({sw * 0.5f - 50.0f * s, r.y + r.height - 24.0f * s, 100.0f * s, 16.0f * s}, "Back") ||
           ui::backPressed();
}

void GrimoirePanel::drawSpells(App& app, float x, float y) {
    const Profile& p = app.profile();
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale());
    constexpr int kColumns = 10;
    const float icon = 28.0f * s, step = 37.0f * s;
    x += (kPanelW - 40.0f - (kColumns - 1) * 37.0f - 28.0f) * 0.5f * s;
    int hovered = -1;
    for (int i = 1; i < kSpellCount; ++i) {
        const int col = (i - 1) % kColumns, row = (i - 1) / kColumns;
        Rectangle ir{x + static_cast<float>(col) * step, y + static_cast<float>(row) * step, icon, icon};
        if (spellKnown(p, i)) {
            ui::spellIcon(static_cast<SpellId>(i), ir, ui::hovered(ir));
        } else {
            ui::spellIcon(SpellId::None, ir);
            ui::text("?", ir.x + icon * 0.5f, ir.y + (icon - static_cast<float>(size)) * 0.5f, size, Color{90, 82, 100, 255},
                     ui::Align::Center, false);
        }
        if (ui::hovered(ir)) hovered = i;
    }
    if (hovered > 0) {
        auto id = static_cast<SpellId>(hovered);
        if (spellKnown(p, hovered)) ui::tooltip(spellDef(id).name, spellDetails(id), ui::spellTypeColor(id));
        else ui::tooltip("Undiscovered", "Find this spell in a chest, on a wand or in a sanctum shop.", ui::color::kTextDim);
    }
}

void GrimoirePanel::drawCreatures(App& app, float x, float y) {
    const Profile& p = app.profile();
    const SpriteBank& sprites = app.sprites();
    const float s = static_cast<float>(ui::scale());
    const int small = ui::fontSize(std::max(1, ui::scale() - 1));
    constexpr int kColumns = 5;
    const float cellW = 80.0f * s, cellH = 62.0f * s, artH = 34.0f * s;
    int slot = 0;
    for (int i = 0; i < static_cast<int>(EnemyType::Count); ++i) {
        const auto type = static_cast<EnemyType>(i);
        if (!listedCreature(type)) continue;
        const EnemyDef& def = enemyDef(type);
        const int kills = p.creatureKills[static_cast<size_t>(i)];
        const float px = x + static_cast<float>(slot % kColumns) * cellW, py = y + static_cast<float>(slot / kColumns) * cellH;
        ++slot;

        const SpriteInfo& info = sprites.info(def.sprite);
        const int scale = std::max(1, static_cast<int>(std::min((cellW - 16.0f * s) / static_cast<float>(std::max(1, info.w)),
                                                                artH / static_cast<float>(std::max(1, info.h)))));
        const float w = static_cast<float>(info.w * scale), h = static_cast<float>(info.h * scale);
        Vector2 pos{px + (cellW - w) * 0.5f, py + artH - h};
        if (kills > 0) sprites.drawScaled(def.sprite, 0, pos, scale, false, enemyTint(type));
        else sprites.drawScaled(def.sprite, 0, pos, scale, false, Color{34, 28, 44, 255}, false, true);

        const float ny = py + artH + 3.0f * s;
        ui::text(kills > 0 ? def.name : "???", px + cellW * 0.5f, ny, small, kills > 0 ? ui::color::kText : ui::color::kTextDim,
                 ui::Align::Center);
        if (kills > 0)
            ui::text(TextFormat("slain %d", kills), px + cellW * 0.5f, ny + static_cast<float>(small) + 1.0f * s, small,
                     ui::color::kTextDim, ui::Align::Center);
    }
}

void GrimoirePanel::drawPerks(App& app, float x, float y) {
    const Profile& p = app.profile();
    const float s = static_cast<float>(ui::scale());
    const int small = ui::fontSize(std::max(1, ui::scale() - 1));
    constexpr int kRows = 11;
    const float colW = 200.0f * s, rowH = 16.0f * s;
    int hovered = -1;
    for (int i = 0; i < kPerkCount; ++i) {
        const PerkDef& def = perkDef(static_cast<PerkId>(i));
        const bool known = (p.perksSeen >> i) & 1u;
        const float px = x + static_cast<float>(i / kRows) * colW, py = y + static_cast<float>(i % kRows) * rowH;
        Rectangle gem{px, py + (static_cast<float>(small) - 7.0f * s) * 0.5f, 7.0f * s, 7.0f * s};
        DrawRectangleRec(gem, known ? hexColor(def.color) : Color{50, 44, 58, 255});
        DrawRectangleLinesEx(gem, 1.0f, Color{0, 0, 0, 160});
        ui::text(known ? def.name : "? ? ?", px + 12.0f * s, py, small, known ? ui::color::kText : ui::color::kTextDim);
        if (ui::hovered({px, py, colW - 10.0f * s, rowH})) hovered = i;
    }
    if (hovered >= 0) {
        const PerkDef& def = perkDef(static_cast<PerkId>(hovered));
        if ((p.perksSeen >> hovered) & 1u) ui::tooltip(def.name, def.description, hexColor(def.color));
        else ui::tooltip("Unknown perk", "Take it at a sanctum altar or a transmutation circle to record it.", ui::color::kTextDim);
    }
}

void GrimoirePanel::drawChronicle(App& app, float x, float y) {
    const Profile& p = app.profile();
    const float s = static_cast<float>(ui::scale());
    const int small = ui::fontSize(std::max(1, ui::scale() - 1));
    if (p.history.empty()) {
        ui::text("No runs recorded yet. Your first descent will be written here.", x + (kPanelW - 40.0f) * 0.5f * s, y + 40.0f * s,
                 small, ui::color::kTextDim, ui::Align::Center);
        return;
    }
    // Columns: path, how deep, time, fate. Choosing a row replays its seed.
    const int size = ui::fontSize(ui::scale());
    const float cols[] = {0.0f, 100.0f * s, 205.0f * s, 250.0f * s};
    const float rowH = 17.0f * s, rowW = (kPanelW - 32.0f) * s;
    const char* headers[] = {"Path", "Reached", "Time", "Fate"};
    for (int c = 0; c < 4; ++c) ui::text(headers[c], x + cols[c], y, size, ui::color::kGold);
    ui::text("choose a run to replay its seed", x + rowW - 6.0f * s, y + static_cast<float>(size - small), small,
             ui::color::kTextDim, ui::Align::Right);
    chronicleNav_.update(static_cast<int>(p.history.size()));
    const float top = y + rowH * 1.25f;
    auto rowRect = [&](size_t i) { return Rectangle{x - 6.0f * s, top + static_cast<float>(i) * rowH - 2.0f * s, rowW, rowH}; };
    // One highlighted row: the keyboard/gamepad focus while it is active, else the one under the mouse.
    int hot = -1;
    bool mouseHot = false;
    for (size_t i = 0; i < p.history.size(); ++i) {
        if (chronicleNav_.focused(static_cast<int>(i))) {
            hot = static_cast<int>(i);
            mouseHot = false;
        } else if (hot < 0 && ui::hovered(rowRect(i))) {
            hot = static_cast<int>(i);
            mouseHot = true;
        }
    }
    if (hot != armedRow_) armedRow_ = -1;
    if (hot >= 0) {
        DrawRectangleRec(rowRect(static_cast<size_t>(hot)), armedRow_ == hot ? Color{230, 80, 80, 60} : Color{170, 130, 255, 40});
        if ((mouseHot && input::mousePressed(MOUSE_BUTTON_LEFT)) || chronicleNav_.activated(hot)) {
            if (replayAbandonsRun_ && armedRow_ != hot) armedRow_ = hot; // first press: confirm
            else replay_ = static_cast<size_t>(hot);
        }
    }
    float py = top;
    for (const RunHistoryEntry& e : p.history) {
        const bool known = e.loadout >= 0 && e.loadout < kLoadoutCount;
        std::string path = known ? loadoutDef(static_cast<LoadoutId>(e.loadout)).name : "?";
        if (e.daily) path += " (daily)";
        const Color main = e.victory ? ui::color::kGold : ui::color::kText;
        ui::text(path, x + cols[0], py, size, main);
        ui::text(depthLabel(e), x + cols[1], py, size, main);
        ui::text(clockTime(e.time), x + cols[2], py, size, ui::color::kTextDim);
        ui::text(e.fate, x + cols[3], py, size, e.victory ? ui::color::kGold : Color{230, 150, 120, 255});
        py += rowH;
    }
    if (mouseHot || armedRow_ >= 0) {
        const char* title = TextFormat("Seed %u", p.history[static_cast<size_t>(hot)].seed);
        if (armedRow_ == hot)
            ui::tooltip(title, "Choose it again to begin: your suspended run will be abandoned.", ui::color::kDanger);
        else if (replayAbandonsRun_)
            ui::tooltip(title, "Walk this path again: the same stages, wands and treasures. Abandons your suspended run.",
                        ui::color::kDanger);
        else
            ui::tooltip(title, "Walk this path again: the same stages, wands and treasures.", ui::color::kAccent);
    }
}

} // namespace pm
