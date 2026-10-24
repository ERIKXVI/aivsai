// Prima Materia — a falling-sand alchemy roguelite.
// Entry point: parses command-line options and hands control to the App.

#include <cstdlib>
#include <cstring>
#include <string>

#include "app/App.hpp"

namespace {

pm::LaunchOptions parseArgs(int argc, char** argv) {
    pm::LaunchOptions o;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (std::strcmp(a, "--sandbox") == 0) o.sandbox = true;
        else if (std::strcmp(a, "--play") == 0) o.startRun = true;
        else if (std::strcmp(a, "--windowed") == 0) o.windowed = true;
        else if (std::strcmp(a, "--autoplay") == 0) o.autoplay = true;
        else if (std::strcmp(a, "--turbo") == 0) o.turbo = true;
        else if (std::strcmp(a, "--god") == 0) o.god = true;
        else if (std::strcmp(a, "--seed") == 0) o.seed = static_cast<uint32_t>(std::strtoul(next(), nullptr, 10));
        else if (std::strcmp(a, "--level") == 0) o.startLevel = std::atoi(next());
        else if (std::strcmp(a, "--loadout") == 0) o.loadout = next();
        else if (std::strcmp(a, "--sanctum") == 0) o.startInSanctum = true;
        else if (std::strcmp(a, "--ui") == 0) o.openUi = next();
        else if (std::strcmp(a, "--capture") == 0) {
            o.captureFrame = std::atoi(next());
            o.capturePath = next();
        } else if (std::strcmp(a, "--capture-every") == 0) {
            o.captureEvery = std::atoi(next());
            o.capturePath = next();
        } else if (std::strcmp(a, "--frames") == 0) {
            o.quitAfter = std::atoi(next());
        } else if (std::strcmp(a, "--size") == 0) {
            o.width = std::atoi(next());
            o.height = std::atoi(next());
        }
    }
    return o;
}

} // namespace

int main(int argc, char** argv) {
    pm::App app(parseArgs(argc, argv));
    return app.run();
}
