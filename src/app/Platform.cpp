#include "app/Platform.hpp"

#include <cstdio>

#if defined(_WIN32)
// Declared directly rather than through <windows.h>, whose macros clash with raylib.
extern "C" __declspec(dllimport) int __stdcall MessageBoxA(void* owner, const char* text, const char* caption,
                                                           unsigned int type);
#endif

namespace pm::platform {

void showFatalError(const char* title, const char* message) {
#if defined(_WIN32)
    constexpr unsigned int kIconError = 0x10;
    MessageBoxA(nullptr, message, title, kIconError);
#endif
    std::fprintf(stderr, "%s: %s\n", title, message);
}

} // namespace pm::platform
