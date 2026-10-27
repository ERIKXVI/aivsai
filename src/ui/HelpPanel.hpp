#pragma once
// Shared "How to Play" screen, opened from the title screen and from the pause menu.

namespace pm {

class App;

/// Draws the controls (for the gamepad when `gamepad`, else keyboard and mouse) and the rules
/// of play. Returns true when the player chooses to go back.
bool drawHelpPanel(App& app, bool gamepad);

} // namespace pm
