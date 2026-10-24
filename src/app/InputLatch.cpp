#include "app/InputLatch.hpp"

#include <array>

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
bool g_anyKey = false;

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

void beginFrame() {
    g_buttons = g_latchedButtons;
    g_latchedButtons.fill(false);
    g_keys.fill(false);
    g_anyKey = false;
    for (int key = GetKeyPressed(); key != 0; key = GetKeyPressed()) {
        if (key > 0 && key < kKeys) g_keys[static_cast<size_t>(key)] = true;
        g_anyKey = true;
    }
}

bool mousePressed(int button) {
    const bool latched = button >= 0 && button < kButtons && g_buttons[static_cast<size_t>(button)];
    return latched || IsMouseButtonPressed(button);
}

bool keyPressed(int key) {
    const bool latched = key > 0 && key < kKeys && g_keys[static_cast<size_t>(key)];
    return latched || IsKeyPressed(key);
}

bool anyKeyPressed() { return g_anyKey; }

} // namespace pm::input
