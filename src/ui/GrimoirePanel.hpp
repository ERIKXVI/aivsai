#pragma once
// The Grimoire: a codex of every spell discovered, creature slain and perk taken across
// all runs, plus a chronicle of the most recent runs, shown from the title screen.

namespace pm {

class App;

class GrimoirePanel {
public:
    /// Draws the panel; returns true when the player chooses to go back.
    bool draw(App& app);

private:
    enum class Tab { Spells, Creatures, Perks, Chronicle, Count };

    void drawSpells(App& app, float x, float y);
    void drawCreatures(App& app, float x, float y);
    void drawPerks(App& app, float x, float y);
    void drawChronicle(App& app, float x, float y);

    Tab tab_ = Tab::Spells;
};

} // namespace pm
