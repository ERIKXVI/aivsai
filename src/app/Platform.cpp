#include "app/Platform.hpp"

#include <cstdio>

#if defined(_WIN32)
// Declared directly rather than through <windows.h>, whose macros clash with raylib.
extern "C" __declspec(dllimport) int __stdcall MessageBoxA(void* owner, const char* text, const char* caption,
                                                           unsigned int type);
extern "C" __declspec(dllimport) unsigned long __stdcall SetThreadExecutionState(unsigned long flags);
#endif

namespace pm::platform {

void showFatalError(const char* title, const char* message) {
#if defined(_WIN32)
    constexpr unsigned int kIconError = 0x10;
    MessageBoxA(nullptr, message, title, kIconError);
#endif
    std::fprintf(stderr, "%s: %s\n", title, message);
}

void keepDisplayAwake(bool on) {
#if defined(_WIN32)
    constexpr unsigned long kContinuous = 0x80000000ul, kSystemRequired = 0x1ul, kDisplayRequired = 0x2ul;
    SetThreadExecutionState(on ? kContinuous | kSystemRequired | kDisplayRequired : kContinuous);
#else
    (void)on;
#endif
}

} // namespace pm::platform
