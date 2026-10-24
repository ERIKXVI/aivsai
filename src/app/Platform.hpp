#pragma once
// Small platform services that raylib does not cover.

namespace pm::platform {

/// Reports an unrecoverable start-up problem to the player: a message box on Windows
/// (where there is no console), stderr elsewhere.
void showFatalError(const char* title, const char* message);

} // namespace pm::platform
