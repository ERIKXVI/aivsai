#pragma once
// Presses that survive fast taps. raylib reports a key or button as "pressed" only if it
// is down when a frame samples it, so a press and release arriving between two frames (a
// quick touchpad tap, or any short tap at a low frame rate) is lost. The latch remembers
// every press until the next frame: mouse buttons through a hook on the window's button
// callback, keys through raylib's key queue.

namespace pm::input {

/// Chains onto raylib's mouse-button callback. Call once after the window exists.
void installLatch();
/// Starts a frame: presses latched since the previous frame become visible.
void beginFrame();
/// True if `button` went down since the previous frame, however briefly.
bool mousePressed(int button);
/// True if `key` went down since the previous frame, however briefly.
bool keyPressed(int key);
/// True if any key other than a lone modifier (Shift, Ctrl, Alt, Super) went down since the
/// previous frame.
bool anyKeyPressed();
/// Hides `key`'s press from the rest of this frame, once a global shortcut has used it.
void consumeKey(int key);
/// Which device the player is using, for button prompts: a key, a click or a mouse movement
/// means keyboard and mouse; a gamepad button or a deflected stick means the gamepad.
/// Returns `wasGamepad` when nothing was touched this frame.
bool usingGamepad(bool wasGamepad);

} // namespace pm::input
