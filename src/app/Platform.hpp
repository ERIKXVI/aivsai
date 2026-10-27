#pragma once
// Small platform services that raylib does not cover.

namespace pm::platform {

/// Reports an unrecoverable start-up problem to the player: a message box on Windows
/// (where there is no console), stderr elsewhere.
void showFatalError(const char* title, const char* message);
/// While on, Windows keeps the display and the system awake: in borderless windowed mode nothing
/// else does, and gamepad input does not reset the idle timer. Does nothing elsewhere.
void keepDisplayAwake(bool on);

} // namespace pm::platform
