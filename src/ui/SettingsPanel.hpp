#pragma once
// Shared settings screen used by the main menu and the pause menu.

namespace pm {

class App;

/// Draws the settings panel and applies changes immediately. Returns true when the player
/// chooses to go back.
bool drawSettingsPanel(App& app);

} // namespace pm
