#include "app/Persistence.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace pm {

namespace {

namespace fs = std::filesystem;

using KeyValues = std::map<std::string, std::string>;

KeyValues readFile(const std::string& path) {
    KeyValues kv;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv;
}

void writeFile(const std::string& path, const KeyValues& kv) {
    // Write to a temporary file first so a crash never leaves a truncated profile.
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return;
        for (const auto& [k, v] : kv) out << k << '=' << v << '\n';
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
    }
}

template <typename T>
void read(const KeyValues& kv, const char* key, T& out) {
    auto it = kv.find(key);
    if (it == kv.end()) return;
    std::istringstream ss(it->second);
    T v{};
    if (ss >> v) out = v;
}

template <typename T>
std::string str(T v) {
    std::ostringstream ss;
    ss << v;
    return ss.str();
}

std::string pathFor(const char* name) { return (fs::path(dataDirectory()) / name).string(); }

} // namespace

std::string dataDirectory() {
    static const std::string dir = [] {
        fs::path base;
#if defined(_WIN32)
        if (const char* appData = std::getenv("APPDATA")) base = fs::path(appData) / "PrimaMateria";
#else
        if (const char* xdg = std::getenv("XDG_DATA_HOME")) base = fs::path(xdg) / "PrimaMateria";
        else if (const char* home = std::getenv("HOME")) base = fs::path(home) / ".local" / "share" / "PrimaMateria";
#endif
        std::error_code ec;
        if (!base.empty() && (fs::create_directories(base, ec) || fs::exists(base, ec))) return base.string();
        return std::string(".");
    }();
    return dir;
}

Settings loadSettings() {
    Settings s;
    KeyValues kv = readFile(pathFor("settings.cfg"));
    read(kv, "master_volume", s.masterVolume);
    read(kv, "music_volume", s.musicVolume);
    read(kv, "sfx_volume", s.sfxVolume);
    read(kv, "screen_shake", s.screenShake);
    read(kv, "fullscreen", s.fullscreen);
    read(kv, "show_fps", s.showFps);
    read(kv, "difficulty", s.difficulty);
    return s;
}

void saveSettings(const Settings& s) {
    writeFile(pathFor("settings.cfg"), {{"master_volume", str(s.masterVolume)},
                                        {"music_volume", str(s.musicVolume)},
                                        {"sfx_volume", str(s.sfxVolume)},
                                        {"screen_shake", str(s.screenShake)},
                                        {"fullscreen", str(s.fullscreen)},
                                        {"show_fps", str(s.showFps)},
                                        {"difficulty", str(s.difficulty)}});
}

Profile loadProfile() {
    Profile p;
    KeyValues kv = readFile(pathFor("profile.dat"));
    read(kv, "runs", p.runs);
    read(kv, "victories", p.victories);
    read(kv, "deepest", p.deepest);
    read(kv, "best_time", p.bestTime);
    read(kv, "total_kills", p.totalKills);
    read(kv, "total_gold", p.totalGold);
    read(kv, "last_daily_seed", p.lastDailySeed);
    read(kv, "best_daily_depth", p.bestDailyDepth);
    return p;
}

void saveProfile(const Profile& p) {
    writeFile(pathFor("profile.dat"), {{"runs", str(p.runs)},
                                       {"victories", str(p.victories)},
                                       {"deepest", str(p.deepest)},
                                       {"best_time", str(p.bestTime)},
                                       {"total_kills", str(p.totalKills)},
                                       {"total_gold", str(p.totalGold)},
                                       {"last_daily_seed", str(p.lastDailySeed)},
                                       {"best_daily_depth", str(p.bestDailyDepth)}});
}

} // namespace pm
