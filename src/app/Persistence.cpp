#include "app/Persistence.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace pm {

namespace fs = std::filesystem;

KeyValues readKeyValues(const fs::path& path) {
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

void writeKeyValues(const fs::path& path, const KeyValues& kv) {
    // Write to a temporary file first so a crash never leaves a truncated profile.
    fs::path tmp = path;
    tmp += ".tmp";
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

namespace {

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

template <size_t N>
std::string joinInts(const std::array<int, N>& values) {
    std::string out;
    for (size_t i = 0; i < N; ++i) {
        if (i) out += ',';
        out += std::to_string(values[i]);
    }
    return out;
}

fs::path pathFor(const char* name) { return dataDirectory() / name; }

} // namespace

const fs::path& dataDirectory() {
    static const fs::path dir = [] {
        fs::path base;
#if defined(_WIN32)
        // Wide API so user names outside the ANSI code page still resolve.
        if (const wchar_t* appData = _wgetenv(L"APPDATA")) base = fs::path(appData) / L"PrimaMateria";
#else
        if (const char* xdg = std::getenv("XDG_DATA_HOME")) base = fs::path(xdg) / "PrimaMateria";
        else if (const char* home = std::getenv("HOME")) base = fs::path(home) / ".local" / "share" / "PrimaMateria";
#endif
        std::error_code ec;
        if (!base.empty() && (fs::create_directories(base, ec) || fs::exists(base, ec))) return base;
        return fs::path(".");
    }();
    return dir;
}

Settings loadSettings() {
    Settings s;
    KeyValues kv = readKeyValues(pathFor("settings.cfg"));
    read(kv, "master_volume", s.masterVolume);
    read(kv, "music_volume", s.musicVolume);
    read(kv, "sfx_volume", s.sfxVolume);
    read(kv, "screen_shake", s.screenShake);
    read(kv, "fullscreen", s.fullscreen);
    read(kv, "show_fps", s.showFps);
    read(kv, "difficulty", s.difficulty);
    read(kv, "loadout", s.loadout);
    return s;
}

void saveSettings(const Settings& s) {
    writeKeyValues(pathFor("settings.cfg"), {{"master_volume", str(s.masterVolume)},
                                        {"music_volume", str(s.musicVolume)},
                                        {"sfx_volume", str(s.sfxVolume)},
                                        {"screen_shake", str(s.screenShake)},
                                        {"fullscreen", str(s.fullscreen)},
                                        {"show_fps", str(s.showFps)},
                                        {"difficulty", str(s.difficulty)},
                                        {"loadout", str(s.loadout)}});
}

Profile loadProfile() {
    Profile p;
    KeyValues kv = readKeyValues(pathFor("profile.dat"));
    read(kv, "runs", p.runs);
    read(kv, "victories", p.victories);
    read(kv, "deepest", p.deepest);
    read(kv, "best_time", p.bestTime);
    read(kv, "total_kills", p.totalKills);
    read(kv, "total_gold", p.totalGold);
    read(kv, "last_daily_seed", p.lastDailySeed);
    read(kv, "best_daily_depth", p.bestDailyDepth);
    read(kv, "chests_opened", p.chestsOpened);
    read(kv, "spells_seen", p.spellsSeen);
    read(kv, "perks_seen", p.perksSeen);
    read(kv, "hints_seen", p.hintsSeen);
    if (auto it = kv.find("creature_kills"); it != kv.end()) {
        std::istringstream ss(it->second);
        std::string item;
        for (size_t i = 0; i < p.creatureKills.size() && std::getline(ss, item, ','); ++i) p.creatureKills[i] = std::atoi(item.c_str());
    }
    return p;
}

void saveProfile(const Profile& p) {
    writeKeyValues(pathFor("profile.dat"), {{"runs", str(p.runs)},
                                       {"victories", str(p.victories)},
                                       {"deepest", str(p.deepest)},
                                       {"best_time", str(p.bestTime)},
                                       {"total_kills", str(p.totalKills)},
                                       {"total_gold", str(p.totalGold)},
                                       {"last_daily_seed", str(p.lastDailySeed)},
                                       {"best_daily_depth", str(p.bestDailyDepth)},
                                       {"chests_opened", str(p.chestsOpened)},
                                       {"spells_seen", str(p.spellsSeen)},
                                       {"perks_seen", str(p.perksSeen)},
                                       {"hints_seen", str(p.hintsSeen)},
                                       {"creature_kills", joinInts(p.creatureKills)}});
}

} // namespace pm
