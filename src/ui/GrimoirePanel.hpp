#pragma once
// The Grimoire: a codex of every spell discovered, creature slain, perk taken, feat earned and
// transmutation witnessed across all runs, plus a chronicle of the most recent runs, shown from the
// title screen.

#include <cstddef>
#include <optional>

#include "app/InputLatch.hpp"
#include "ui/Ui.hpp"

namespace pm {

class App;

class GrimoirePanel {
public:
    /// Draws the panel; returns true when the player chooses to go back.
    bool draw(App& app);
    /// Replaying a chronicled seed abandons a suspended run: ask for a second click first.
    void setReplayAbandonsRun(bool abandons) { replayAbandonsRun_ = abandons; }
    /// The chronicle entry the player chose to replay since the last call, if any.
    std::optional<size_t> takeReplayRequest() {
        auto r = replay_;
        replay_.reset();
        return r;
    }

private:
    enum class Tab { Spells, Creatures, Perks, Feats, Codex, Chronicle, Count };

    /// The tab row at `top` across the panel (left edge `panelX`, width `panelW`), each tab as wide as its
    /// label; a click picks a tab.
    void drawTabs(App& app, float panelX, float panelW, float top);
    void drawSpells(App& app, float x, float y);
    void drawCreatures(App& app, float x, float y);
    void drawPerks(App& app, float x, float y);
    void drawFeats(App& app, float x, float y);
    /// The Alchemical Codex: a scrolling list of every transmutation, its formula once witnessed, else its riddle.
    void drawCodex(App& app, float x, float y);
    /// Scrolls the codex list (`visible` rows in view) by the wheel, by its scroll bar's area `grip` (pressed or
    /// dragged), and onto the focus of the keys or pad when that moves; returns the focused row (-1: none).
    int scrollCodex(Rectangle grip, int visible);
    void drawChronicle(App& app, float x, float y);

    Tab tab_ = Tab::Spells;
    ui::NavList chronicleNav_;
    ui::NavList codexNav_;
    input::WheelStepper codexWheel_{0.0f, 8.0f}; ///< mouse wheel to codex rows: one per notch, unthrottled
    int codexScroll_ = 0;        ///< the first codex row in view
    int codexFocus_ = -1;        ///< the row the keys or pad focused last frame (-1: none)
    bool codexDragging_ = false; ///< the codex scroll bar is held
    std::optional<size_t> replay_;
    bool replayAbandonsRun_ = false;
    int armedRow_ = -1; ///< row clicked once while replaying would abandon a suspended run
};

} // namespace pm
