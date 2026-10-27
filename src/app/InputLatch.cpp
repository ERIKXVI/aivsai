#include "app/InputLatch.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "GLFW/glfw3.h"
#include "raylib.h"

namespace pm::input {

namespace {

constexpr int kButtons = 8;
constexpr int kKeys = 512;

GLFWmousebuttonfun g_previous = nullptr;
std::array<bool, kButtons> g_latchedButtons{};
std::array<bool, kButtons> g_buttons{};
std::array<bool, kKeys> g_keys{};
std::array<bool, kKeys> g_consumed{};
bool g_anyKey = false;
bool g_wasFocused = true;
bool g_mouseBlocked = false; ///< swallowing the click that refocused the window
double g_focusAt = 0.0;
bool g_padBlocked = false; ///< another window has the focus, or it only just came back
constexpr float kTriggerPull = 0.3f; ///< a trigger past this is pulled (as when casting)
bool g_leftTrigger = false, g_rightTrigger = false; ///< each trigger was past kTriggerPull last frame
bool g_triggerPulled = false; ///< a trigger crossed kTriggerPull this frame
bool g_usingGamepad = false; ///< the last device used was the gamepad
bool g_captured = false;
GLFWcursor* g_blankCursor = nullptr;

void onMouseButton(GLFWwindow* window, int button, int action, int mods) {
    if (action == GLFW_PRESS && button >= 0 && button < kButtons) g_latchedButtons[static_cast<size_t>(button)] = true;
    if (g_previous) g_previous(window, button, action, mods);
}

} // namespace

void installLatch() {
    // GetWindowHandle() is the native handle on Windows (an HWND), not the GLFW window;
    // raylib's window is the one whose context is current on this thread.
    GLFWwindow* window = glfwGetCurrentContext();
    if (window != nullptr) g_previous = glfwSetMouseButtonCallback(window, onMouseButton);
}

void noteAway() { g_wasFocused = false; }

void beginFrame() {
    g_buttons = g_latchedButtons;
    // A press reported a poll after the focus event still counts as the refocusing click, hence
    // the moment's grace before the block can lift.
    constexpr double kRefocusGrace = 0.15;
    const bool focused = IsWindowFocused();
    if (focused && !g_wasFocused) {
        g_mouseBlocked = true;
        g_focusAt = GetTime();
    }
    g_wasFocused = focused;
    // A gamepad is shared by every app: what it does while another window has the focus, or in
    // the moment that window hands it back, is not meant for this one.
    g_padBlocked = !focused || GetTime() - g_focusAt <= kRefocusGrace;
    // A trigger counts once, as it crosses the threshold, so a held one does not keep taking the
    // prompts back from a moving mouse.
    const bool left = padAxis(GAMEPAD_AXIS_LEFT_TRIGGER) > kTriggerPull;
    const bool right = padAxis(GAMEPAD_AXIS_RIGHT_TRIGGER) > kTriggerPull;
    g_triggerPulled = (left && !g_leftTrigger) || (right && !g_rightTrigger);
    g_leftTrigger = left;
    g_rightTrigger = right;
    if (g_mouseBlocked && GetTime() - g_focusAt > kRefocusGrace && !IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
        !IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
        g_mouseBlocked = false;
    if (g_mouseBlocked) g_buttons.fill(false);
    g_latchedButtons.fill(false);
    g_keys.fill(false);
    g_consumed.fill(false);
    g_anyKey = false;
    for (int key = GetKeyPressed(); key != 0; key = GetKeyPressed()) {
        if (key > 0 && key < kKeys) g_keys[static_cast<size_t>(key)] = true;
        // A modifier alone is not "a key": Alt goes down a frame before the Enter of
        // Alt+Enter or the Tab of Alt+Tab.
        if (key < KEY_LEFT_SHIFT || key > KEY_RIGHT_SUPER) g_anyKey = true;
    }
}

bool mousePressed(int button) {
    if (g_mouseBlocked) return false;
    const bool latched = button >= 0 && button < kButtons && g_buttons[static_cast<size_t>(button)];
    return latched || IsMouseButtonPressed(button);
}

bool mouseDown(int button) { return !g_mouseBlocked && IsMouseButtonDown(button); }

bool keyPressed(int key) {
    const bool inRange = key > 0 && key < kKeys;
    if (inRange && g_consumed[static_cast<size_t>(key)]) return false;
    const bool latched = inRange && g_keys[static_cast<size_t>(key)];
    return latched || IsKeyPressed(key);
}

bool anyKeyPressed() { return g_anyKey; }

void consumeKey(int key) {
    if (key > 0 && key < kKeys) g_consumed[static_cast<size_t>(key)] = true;
    g_anyKey = false;
}

void setPlayCursor(bool captured) {
    if (captured == g_captured) return;
    GLFWwindow* window = glfwGetCurrentContext();
    if (window == nullptr) return;
    if (captured) {
        // A transparent cursor, set before capturing: GLFW's captured mode shows the cursor.
        if (g_blankCursor == nullptr) {
            static unsigned char pixels[16 * 16 * 4] = {};
            const GLFWimage image{16, 16, pixels};
            g_blankCursor = glfwCreateCursor(&image, 0, 0);
        }
        glfwSetCursor(window, g_blankCursor);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_CAPTURED);
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        glfwSetCursor(window, nullptr);
    }
    g_captured = captured;
}

bool padAvailable() { return !g_padBlocked && IsGamepadAvailable(0); }

bool padPressed(int button) { return !g_padBlocked && IsGamepadButtonPressed(0, button); }

bool padDown(int button) { return !g_padBlocked && IsGamepadButtonDown(0, button); }

float padAxis(int axis) { return g_padBlocked ? 0.0f : GetGamepadAxisMovement(0, axis); }

bool usingGamepad() {
    const Vector2 mouseDelta = GetMouseDelta();
    if (anyKeyPressed() || mousePressed(MOUSE_BUTTON_LEFT) || mouseDelta.x * mouseDelta.x + mouseDelta.y * mouseDelta.y > 4.0f) {
        g_usingGamepad = false;
        return false;
    }
    if (!padAvailable()) return g_usingGamepad;
    const float lx = padAxis(GAMEPAD_AXIS_LEFT_X), ly = padAxis(GAMEPAD_AXIS_LEFT_Y), rx = padAxis(GAMEPAD_AXIS_RIGHT_X),
                ry = padAxis(GAMEPAD_AXIS_RIGHT_Y);
    // raylib reports the triggers as axes only (never as the last button pressed), hence the pull.
    if (std::fabs(lx) > 0.4f || std::fabs(ly) > 0.4f || rx * rx + ry * ry > 0.2f || g_triggerPulled ||
        GetGamepadButtonPressed() != GAMEPAD_BUTTON_UNKNOWN)
        g_usingGamepad = true;
    return g_usingGamepad;
}

bool lastUsedGamepad() { return g_usingGamepad; }

int WheelStepper::step(float delta, float dt) {
    cooldown_ = std::max(0.0f, cooldown_ - dt);
    if (delta != 0.0f) {
        if (delta * accum_ < 0.0f) accum_ = 0.0f; // turning back drops what was queued
        accum_ = std::clamp(accum_ + delta, -maxQueued_, maxQueued_);
        idle_ = 0.0f;
    } else if ((idle_ += dt) > kForget) {
        accum_ = std::trunc(accum_); // whole notches still come out, a partial one is forgotten
    }
    if (std::fabs(accum_) < 0.99f || cooldown_ > 0.0f) return 0;
    const int dir = accum_ > 0.0f ? 1 : -1;
    accum_ -= static_cast<float>(dir);
    cooldown_ = cooldownTime_;
    return dir;
}

} // namespace pm::input
