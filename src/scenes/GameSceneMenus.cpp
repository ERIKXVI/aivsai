// GameScene's menus: the pause menu and the end screens (death and victory) with the run's
// record, its rewards and the ways on.

#include "scenes/GameScene.hpp"

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "app/App.hpp"
#include "app/InputLatch.hpp"
#include "app/RunRecords.hpp"
#include "render/Sprites.hpp"
#include "scenes/MenuScene.hpp"
#include "ui/Ui.hpp"

namespace pm {

void GameScene::drawPauseMenu() {
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const float s = static_cast<float>(ui::scale());
    ui::dimScreen(150);
    Rectangle panelR{sw * 0.5f - 85.0f * s, sh * 0.5f - 95.0f * s, 170.0f * s, 190.0f * s};
    ui::panel(panelR);
    ui::text("PAUSED", sw * 0.5f, panelR.y + 8.0f * s, ui::fontSize(ui::scale() + 1), ui::color::kGold, ui::Align::Center);
    // The run's seed (and ordeal, hover for its rules) at the foot of the panel.
    const int small = ui::smallFontSize();
    const std::string footer = run_.ordeal > 0 ? TextFormat("Seed %u - %s", run_.seed, ordealLabel(run_.ordeal).c_str())
                                               : TextFormat("Seed %u", run_.seed);
    const float footY = panelR.y + panelR.height - static_cast<float>(small) - 6.0f * s;
    ui::text(footer, sw * 0.5f, footY, small, ui::color::kTextDim, ui::Align::Center);
    if (run_.ordeal > 0 && ui::hovered({panelR.x, footY, panelR.width, static_cast<float>(small)}))
        ui::tooltip(ordealLabel(run_.ordeal), ordealRules(run_.ordeal), Color{255, 140, 110, 255});
    float bw = 140.0f * s, bh = 18.0f * s, x = sw * 0.5f - bw * 0.5f, y = panelR.y + 30.0f * s;
    // The destructive entries sit at the bottom and the focus does not wrap onto them.
    pauseNav_.update(6, false);
    int index = 0;
    Rectangle last{};
    auto item = [&](const char* label) {
        last = {x, y, bw, bh};
        const bool pressed = ui::button(last, label, true, pauseNav_.focused(index)) || pauseNav_.activated(index);
        ++index;
        y += bh + 5.0f * s;
        return pressed;
    };
    // Whether the entry just drawn is under the mouse or the keyboard / pad focus.
    auto hot = [&] { return ui::hovered(last) || pauseNav_.focused(index - 1); };

    if (item("Resume")) mode_ = Mode::Playing;
    if (item("How to Play")) mode_ = Mode::Help;
    if (item("Settings")) mode_ = Mode::Settings;
    // The run was saved when this stage began, so the title screen offers to continue it.
    if (item("Return to Title")) {
        app_.setScene(std::make_unique<MenuScene>(app_));
        return;
    }
    if (ui::hovered(last))
        ui::tooltipBeside(last, "Return to Title", "The run is kept: Continue the Work resumes it at the start of this stage.", ui::color::kGold);
    // Ending the run or closing the game takes a second press.
    if (item(confirmAbandon_ ? "Abandon the run?" : "Abandon Run")) {
        if (!confirmAbandon_) {
            confirmAbandon_ = true;
        } else {
            recordProfile(false);
            app_.setScene(std::make_unique<MenuScene>(app_));
            return;
        }
    }
    if (!hot()) confirmAbandon_ = false;
    if (confirmAbandon_ && ui::hovered(last))
        ui::tooltipBeside(last, "Abandon Run", "Press again to end this run; it is recorded as lost.", ui::color::kDanger);
    if (item(confirmQuit_ ? "Quit the game?" : "Quit Game")) {
        if (!confirmQuit_) confirmQuit_ = true;
        else app_.requestQuit(); // the run stays suspended at the start of this stage
    }
    if (!hot()) confirmQuit_ = false;
    if (ui::hovered(last))
        ui::tooltipBeside(last, "Quit Game", "Closes the game. The run is kept for Continue the Work.", ui::color::kGold);
}

float GameScene::drawDescent(float y, unsigned char alpha) const {
    const float sw = static_cast<float>(app_.screenWidth()), s = static_cast<float>(ui::scale());
    const int small = ui::smallFontSize();
    // One section per stage; the fill runs through the stages cleared and part way through the
    // one the alchemist fell in (a sanctum counts its stage as cleared).
    const float stageFrac = run_.inSanctum ? 1.0f : (guide_.length() > 0.0f ? clamp(guide_.progress() / guide_.length(), 0.0f, 1.0f) : 0.0f);
    const float fill = (static_cast<float>(run_.levelIndex) + stageFrac) / static_cast<float>(kStageCount);
    const Rectangle bar{sw * 0.5f - 150.0f * s, y, 300.0f * s, 3.0f * s};
    const float section = bar.width / static_cast<float>(kStageCount);
    DrawRectangleRec(bar, Color{40, 34, 52, alpha});
    DrawRectangleRec({bar.x, bar.y, bar.width * fill, bar.height}, Color{255, 210, 110, alpha});
    for (int i = 0; i < kStageCount; ++i) {
        const float x = bar.x + section * static_cast<float>(i);
        if (i > 0) DrawRectangleRec({x - 0.5f * s, bar.y - 2.0f * s, 1.0f * s, bar.height + 4.0f * s}, Color{120, 100, 140, alpha});
        // Every stage is named, the ones still unseen in a dim hand: the way down is the lure.
        const bool reached = i <= run_.levelIndex;
        const Color c = i == run_.levelIndex ? Color{255, 210, 110, alpha} : reached ? Color{200, 190, 170, alpha} : Color{110, 100, 125, alpha};
        ui::text(TextFormat("%s %s", stageNumeral(i), biomeDef(biomeForDepth(i)).name), x + section * 0.5f, bar.y + bar.height + 3.0f * s,
                 small, c, ui::Align::Center);
    }
    y = bar.y + bar.height + 5.0f * s + static_cast<float>(small);
    const std::string here = biomeDef(biomeForDepth(run_.levelIndex)).name;
    std::string caption = run_.inSanctum      ? "Fell in the sanctum after " + here
                          : stageFrac < 0.05f ? "Fell at the threshold of " + here
                                              : TextFormat("%d%% of the way through %s", static_cast<int>(stageFrac * 100.0f), here.c_str());
    if (run_.levelIndex < kFinalStage) {
        const BiomeDef& next = biomeDef(biomeForDepth(run_.levelIndex + 1));
        caption += std::string(" - next: ") + next.name + " - " + next.subtitle;
    } else {
        caption += " - the Stone lies at its heart";
    }
    ui::text(caption, sw * 0.5f, y, small, Color{170, 160, 185, alpha}, ui::Align::Center);
    return y + static_cast<float>(small) + 6.0f * s;
}

void GameScene::drawEndScreen(bool victory) {
    const float sw = static_cast<float>(app_.screenWidth()), sh = static_cast<float>(app_.screenHeight());
    const float s = static_cast<float>(ui::scale());
    float a = clamp((endTimer_ - (victory ? 0.5f : kDeathScreenDelay)) * 1.5f, 0.0f, 1.0f);
    ui::dimScreen(static_cast<unsigned char>(170.0f * a));
    if (a <= 0.0f) return;
    unsigned char al = static_cast<unsigned char>(255.0f * a);
    const int font = ui::fontSize(ui::scale());
    const float fontH = static_cast<float>(font);
    // Gathering all four guardians' essences on the way crowns the Work with the Quintessence.
    const bool perfected = victory && run_.quintessence();
    const char* title = perfected ? "THE QUINTESSENCE" : victory ? "MAGNUM OPUS" : "THE WORK IS UNDONE";
    const char* sub = perfected ? "Four essences and the Stone made one: the fifth element is yours."
                      : victory ? "The Prima Materia is perfected. You have become the Stone."
                                : "Your matter returns to the prima materia...";
    const Color titleColor = perfected ? Color{235, 245, 255, al} : victory ? Color{255, 220, 120, al} : Color{230, 80, 80, al};
    ui::text(title, sw * 0.5f, sh * 0.2f, ui::fontSize(ui::scale() * 3), titleColor, ui::Align::Center);
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
    const std::string reached = biomeDef(biomeForDepth(run_.levelIndex)).name;
    const std::string seedNote = run_.daily ? " (Daily Rite)" : run_.ordeal > 0 ? " (" + ordealLabel(run_.ordeal) + ")" : "";
    const std::string left[] = {
        std::string("Reached: ") + (run_.inSanctum ? "Sanctum after " : "") + reached,
        "Time: " + ui::formatClock(st.time),
        "Creatures slain: " + std::to_string(st.kills),
        "Gold gathered: " + std::to_string(st.goldCollected),
    };
    const std::string right[] = {
        "Spells cast: " + std::to_string(st.spellsCast),
        "Chests opened: " + std::to_string(st.chestsOpened),
        "Alchemical kills: " + std::to_string(st.alchemicalKills),
        TextFormat("Seed: %u", run_.seed) + seedNote,
    };
    const Color statColor{220, 210, 200, al};
    float y = std::max(sh * 0.38f, subY + lineH * 3.7f);
    const float gutter = 14.0f * s;
    for (size_t i = 0; i < std::size(left); ++i)
        ui::text(left[i], sw * 0.5f - gutter, y + lineH * static_cast<float>(i), font, statColor, ui::Align::Right);
    for (size_t i = 0; i < std::size(right); ++i)
        ui::text(right[i], sw * 0.5f + gutter, y + lineH * static_cast<float>(i), font, statColor);
    y += lineH * static_cast<float>(std::size(left)) + lineH * 0.3f;
    if (!victory) y = drawDescent(y, al);

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
    for (const LoadoutId path : record_.unlockedPaths) {
        ui::text(std::string("Path unlocked: the ") + loadoutDef(path).name, sw * 0.5f, y, font, Color{255, 210, 110, al}, ui::Align::Center);
        y += lineH;
    }
    if (record_.unlockedOrdeal > 0) {
        ui::text(ordealLabel(record_.unlockedOrdeal) + " unlocked: " + ordealDef(record_.unlockedOrdeal).name, sw * 0.5f, y, font,
                 Color{255, 140, 110, al}, ui::Align::Center);
        y += lineH;
    }
    if (record_.nearestPath) {
        const PathProgress& np = *record_.nearestPath;
        // A yes/no goal (target 1) reads as the goal alone; counted ones show how far along they are.
        std::string label = np.target > 1 ? TextFormat("Next path, the %s: %s %d/%d", np.name.c_str(), np.requirement.c_str(), np.now, np.target)
                                          : TextFormat("Next path, the %s: %s", np.name.c_str(), np.requirement.c_str());
        if (np.target > 1 && np.now > np.was) label += TextFormat("  (+%d this run)", np.now - np.was);
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
    // The pad's face buttons double as jump, interact, absorb and drink: a player still mashing
    // them from the fight must not skip this screen unread. Its pad and Enter shortcuts wake a
    // moment after the fade, once those buttons have been let go.
    const bool settled = endTimer_ > (victory ? 0.5f : kDeathScreenDelay) + 1.1f;
    const bool held = IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) ||
                      IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_UP) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT) ||
                      IsKeyDown(KEY_ENTER);
    if (settled && !held) endInputArmed_ = true;
    const bool armed = endInputArmed_;

    const float bw = 100.0f * s, bh = 18.0f * s, gap = 6.0f * s;
    float bx = sw * 0.5f - bw * 1.5f - gap;
    y += 6.0f * s;
    auto startRun = [&](uint32_t seed) {
        RunState fresh = newRun(seed, run_.difficulty, false, run_.loadout, run_.daily ? 0 : run_.ordeal);
        app_.setScene(std::make_unique<GameScene>(app_, std::move(fresh)));
    };
    const char* again = victory ? (usingGamepad_ ? "New Work (A)" : "New Work (R)") : (usingGamepad_ ? "Try Again (A)" : "Try Again (R)");
    if (ui::button({bx, y, bw, bh}, again) || (armed && ui::confirmPressed()) || input::keyPressed(KEY_R)) {
        startRun(randomRunSeed());
        return;
    }
    bx += bw + gap;
    const bool retryKey = input::keyPressed(KEY_T) || (armed && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP));
    if (ui::button({bx, y, bw, bh}, usingGamepad_ ? "Retry Seed (Y)" : "Retry Seed (T)") || retryKey) {
        startRun(run_.seed); // the same stages and treasures, outside the Daily Rite
        return;
    }
    if (ui::hovered({bx, y, bw, bh}))
        ui::tooltip("Retry Seed", "Walk the same stages again: the same layout, wands and treasures.", ui::color::kAccent);
    bx += bw + gap;
    if (ui::button({bx, y, bw, bh}, "Main Menu") || (armed && ui::backPressed())) {
        app_.setScene(std::make_unique<MenuScene>(app_));
        return;
    }
    // A path this run unlocked is offered at once, so the reward is one press away. It takes the
    // row (and the pad's X) the gentler road would use; that offer returns on the next fall.
    if (!record_.unlockedPaths.empty()) {
        const LoadoutId fresh = record_.unlockedPaths.back();
        const LoadoutDef& def = loadoutDef(fresh);
        const Rectangle offer{sw * 0.5f - bw, y + bh + gap, bw * 2.0f, bh};
        const bool offerKey = input::keyPressed(KEY_N) || (armed && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT));
        if (ui::button(offer, TextFormat(usingGamepad_ ? "Begin as the %s (X)" : "Begin as the %s (N)", def.name), true, true) || offerKey) {
            app_.settings().loadout = static_cast<int>(fresh); // Choose Your Path remembers it too
            if (!app_.options().automated()) saveSettings(app_.settings());
            app_.setScene(std::make_unique<GameScene>(app_, newRun(randomRunSeed(), run_.difficulty, false, fresh, run_.daily ? 0 : run_.ordeal)));
            return;
        }
        if (ui::hovered(offer)) ui::tooltip(def.name, def.description, ui::color::kGold);
        return;
    }
    // Two quick falls in a row on stage I, never having won: offer the gentler road.
    const Profile& prof = app_.profile();
    const bool struggling = !victory && prof.victories == 0 && run_.difficulty != Difficulty::Apprentice && run_.ordeal == 0 &&
                            !run_.daily && prof.history.size() >= 2 && prof.history[0].depth == 0 && prof.history[1].depth == 0 &&
                            !prof.history[0].victory && !prof.history[1].victory;
    if (struggling) {
        const Rectangle easier{sw * 0.5f - bw, y + bh + gap, bw * 2.0f, bh};
        const bool easierKey = input::keyPressed(KEY_P) || (armed && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT));
        if (ui::button(easier, usingGamepad_ ? "Try as Apprentice (X)" : "Try as Apprentice (P)") || easierKey) {
            app_.settings().difficulty = static_cast<int>(Difficulty::Apprentice);
            if (!app_.options().automated()) saveSettings(app_.settings());
            app_.setScene(std::make_unique<GameScene>(app_, newRun(randomRunSeed(), Difficulty::Apprentice, false, run_.loadout, 0)));
            return;
        }
        if (ui::hovered(easier))
            ui::tooltip("Apprentice", std::string(difficultyDetail(Difficulty::Apprentice)) + " Change it any time under Choose Your Path.",
                        ui::color::kGold);
    }
}

} // namespace pm
