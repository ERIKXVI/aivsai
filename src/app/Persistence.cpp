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
    // A write interrupted between removing the old file and renaming the new one leaves only
    // the temporary file; it is complete, so read that.
    fs::path source = path;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        fs::path tmp = path;
        tmp += ".tmp";
        if (fs::exists(tmp, ec)) source = tmp;
    }
    std::ifstream in(source);
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
    std::error_code ec;
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return;
        for (const auto& [k, v] : kv) out << k << '=' << v << '\n';
        out.flush();
        out.close();
        if (out.fail()) { // disk full or write error: keep the previous file untouched
            fs::remove(tmp, ec);
            return;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        // Some runtimes cannot rename over an existing file; readKeyValues falls back to the
        // temporary file if the second rename fails too.
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

std::string historyKey(size_t i) { return "history." + std::to_string(i); }

bool parseNumber(const std::string& s, long long& out, long long lo, long long hi) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtoll(s.c_str(), &end, 10);
    return end && *end == '\0' && out >= lo && out <= hi;
}

} // namespace

std::string encodeHistoryEntry(const RunHistoryEntry& e) {
    std::ostringstream ss;
    ss << e.seed << '|' << e.loadout << '|' << e.difficulty << '|' << e.depth << '|' << (e.sanctum ? 1 : 0) << '|'
       << (e.victory ? 1 : 0) << '|' << (e.daily ? 1 : 0) << '|' << static_cast<long long>(e.time) << '|' << e.kills << '|'
       << e.fate;
    return ss.str();
}

bool decodeHistoryEntry(const std::string& text, RunHistoryEntry& out) {
    // Nine numeric fields, then the fate (which may itself contain anything but a newline).
    constexpr int kNumbers = 9;
    long long v[kNumbers];
    constexpr long long kLimits[kNumbers][2] = {{0, 0xFFFFFFFFll}, {0, 63}, {0, 2}, {0, 4}, {0, 1},
                                                {0, 1},           {0, 1},  {0, 1000000}, {0, 1000000}};
    size_t pos = 0;
    for (int i = 0; i < kNumbers; ++i) {
        const size_t bar = text.find('|', pos);
        if (bar == std::string::npos || !parseNumber(text.substr(pos, bar - pos), v[i], kLimits[i][0], kLimits[i][1]))
            return false;
        pos = bar + 1;
    }
    out.seed = static_cast<uint32_t>(v[0]);
    out.loadout = static_cast<int>(v[1]);
    out.difficulty = static_cast<int>(v[2]);
    out.depth = static_cast<int>(v[3]);
    out.sanctum = v[4] != 0;
    out.victory = v[5] != 0;
    out.daily = v[6] != 0;
    out.time = static_cast<float>(v[7]);
    out.kills = static_cast<int>(v[8]);
    out.fate = text.substr(pos);
    return true;
}

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
    read(kv, "hints", s.hints);
    read(kv, "high_quality", s.highQuality);
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
                                        {"loadout", str(s.loadout)},
                                        {"hints", str(s.hints)},
                                        {"high_quality", str(s.highQuality)}});
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
    read(kv, "best_kills", p.bestKills);
    read(kv, "best_gold", p.bestGold);
    read(kv, "spells_seen", p.spellsSeen);
    read(kv, "perks_seen", p.perksSeen);
    if (auto it = kv.find("creature_kills"); it != kv.end()) {
        std::istringstream ss(it->second);
        std::string item;
        for (size_t i = 0; i < p.creatureKills.size() && std::getline(ss, item, ','); ++i) p.creatureKills[i] = std::atoi(item.c_str());
    }
    for (size_t i = 0; i < Profile::kHistorySize; ++i) {
        auto it = kv.find(historyKey(i));
        RunHistoryEntry e;
        if (it == kv.end() || !decodeHistoryEntry(it->second, e)) break;
        p.history.push_back(std::move(e));
    }
    return p;
}

void saveProfile(const Profile& p) {
    KeyValues kv{{"runs", str(p.runs)},
                 {"victories", str(p.victories)},
                 {"deepest", str(p.deepest)},
                 {"best_time", str(p.bestTime)},
                 {"total_kills", str(p.totalKills)},
                 {"total_gold", str(p.totalGold)},
                 {"last_daily_seed", str(p.lastDailySeed)},
                 {"best_daily_depth", str(p.bestDailyDepth)},
                 {"chests_opened", str(p.chestsOpened)},
                 {"best_kills", str(p.bestKills)},
                 {"best_gold", str(p.bestGold)},
                 {"spells_seen", str(p.spellsSeen)},
                 {"perks_seen", str(p.perksSeen)},
                 {"creature_kills", joinInts(p.creatureKills)}};
    for (size_t i = 0; i < p.history.size() && i < Profile::kHistorySize; ++i) kv[historyKey(i)] = encodeHistoryEntry(p.history[i]);
    writeKeyValues(pathFor("profile.dat"), kv);
}

} // namespace pm
