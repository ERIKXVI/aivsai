#pragma once
// Presses that survive fast taps. raylib reports a key or button as "pressed" only if it
// is down when a frame samples it, so a press and release arriving between two frames (a
// quick touchpad tap, or any short tap at a low frame rate) is lost. The latch remembers
// every press until the next frame: mouse buttons through a hook on the window's button
// callback, keys through raylib's key queue.

namespace pm::input {

/// Chains onto raylib's mouse-button callback. Call once after the window exists.
void installLatch();
/// Starts a frame: presses latched since the previous frame become visible. The click that
/// brings the window back into focus is swallowed, and the mouse buttons stay quiet until it
/// is let go, so it cannot press whatever button lies under the cursor.
void beginFrame();
/// True if `button` went down since the previous frame, however briefly.
bool mousePressed(int button);
/// True while `button` is held, except for the click that refocused the window.
bool mouseDown(int button);
/// True if `key` went down since the previous frame, however briefly.
bool keyPressed(int key);
/// True if any key other than a lone modifier (Shift, Ctrl, Alt, Super) went down since the
/// previous frame.
bool anyKeyPressed();
/// Hides `key`'s press from the rest of this frame, once a global shortcut has used it.
void consumeKey(int key);
/// During play the cursor is hidden (the reticle replaces it) and confined to the window, so
/// aiming at a screen edge cannot click on another monitor. Anywhere else it is free.
void setPlayCursor(bool captured);
/// Which device the player is using, for button prompts: a key, a click or a mouse movement
/// means keyboard and mouse; a gamepad button or a deflected stick means the gamepad.
/// Returns `wasGamepad` when nothing was touched this frame.
bool usingGamepad(bool wasGamepad);

/// Turns vertical wheel motion into whole steps: one per notch of an ordinary wheel, and one
/// per notch's worth of the many small deltas a touchpad swipe or a smooth-scrolling wheel
/// sends over several frames. Steps are at least kCooldown apart, so fast notches queue (up to
/// kMaxQueued); a pause forgets a partial notch, and turning back drops what was queued.
class WheelStepper {
public:
    static constexpr float kCooldown = 0.12f; ///< seconds between two steps
    static constexpr float kForget = 0.2f;    ///< seconds without motion that drop a partial notch
    static constexpr float kMaxQueued = 4.0f; ///< notches waiting out the cooldown
    /// `delta` is this frame's wheel motion (positive is up). Returns +1 for a notch up, -1 for
    /// a notch down, 0 otherwise.
    int step(float delta, float dt);

private:
    float accum_ = 0.0f, idle_ = 0.0f, cooldown_ = 0.0f;
};

} // namespace pm::input
