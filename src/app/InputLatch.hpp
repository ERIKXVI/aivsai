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
/// The window is away (minimised) and frames are skipped: the next beginFrame counts as a refocus,
/// so the press that brings the window back is not taken as input.
void noteAway();
/// True if `button` went down since the previous frame, however briefly.
bool mousePressed(int button);
/// True while `button` is held, except for the click that refocused the window.
bool mouseDown(int button);
/// Like IsMouseButtonReleased(button), but false for the release of the click that refocused the
/// window: its press was swallowed, so its release must not drop or cancel a spell held in hand.
bool mouseReleased(int button);
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
/// Gamepad 0, read only while this window may: a pad is shared by every app, so while another
/// window has the focus (and for the refocus grace after it returns) the pad reads as idle.
/// True if pad 0 is connected and not blocked.
bool padAvailable();
/// Like IsGamepadButtonPressed(0, button); false while blocked.
bool padPressed(int button);
/// Like IsGamepadButtonDown(0, button); false while blocked.
bool padDown(int button);
/// Like GetGamepadAxisMovement(0, axis); 0 while blocked (where a released trigger reads -1).
float padAxis(int axis);
/// Which device the player is using, for button prompts: a key, a click or a mouse movement
/// means keyboard and mouse; a gamepad button, a deflected stick or a trigger pull means the
/// gamepad. Remembered across scenes, and unchanged when nothing was touched this frame.
bool usingGamepad();
/// The device usingGamepad() last settled on, without looking at this frame's input: a new
/// scene starts its button prompts in the device the player was using.
bool lastUsedGamepad();

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

    WheelStepper() = default;
    /// A stepper with its own pace, e.g. no cooldown for a long range such as a brush size.
    WheelStepper(float cooldown, float maxQueued) : cooldownTime_(cooldown), maxQueued_(maxQueued) {}

private:
    float cooldownTime_ = kCooldown, maxQueued_ = kMaxQueued;
    float accum_ = 0.0f, idle_ = 0.0f, cooldown_ = 0.0f;
};

} // namespace pm::input
