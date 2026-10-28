#include "app/Persistence.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "game/Ordeals.hpp"
#include "game/Run.hpp"
#include "gen/Biome.hpp"

namespace pm {

namespace fs = std::filesystem;

namespace {

/// Where writeKeyValues stages a file before renaming it into place.
fs::path tempPathFor(const fs::path& path) {
    fs::path tmp = path;
    tmp += ".tmp";
    return tmp;
}

/// Where writeKeyValues keeps the previous file while the new one may not have reached the disk.
fs::path backupPathFor(const fs::path& path) {
    fs::path bak = path;
    bak += ".bak";
    return bak;
}

/// The pairs in one file, skipping lines without '='; a missing file reads as empty.
KeyValues parseKeyValues(const fs::path& path) {
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

} // namespace

KeyValues readKeyValues(const fs::path& path) {
    // A write interrupted between moving the old file aside and renaming the new one leaves only
    // the temporary file; it is complete, so read that.
    fs::path source = path;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        if (const fs::path tmp = tempPathFor(path); fs::exists(tmp, ec)) source = tmp;
    }
    KeyValues kv = parseKeyValues(source);
    // A power cut soon after a write can leave the new file empty or zero-filled: fall back to the
    // previous one, whose data reached the disk long ago. A file deleted on purpose stays deleted.
    if (kv.empty() && fs::exists(source, ec)) {
        if (const fs::path bak = backupPathFor(path); fs::exists(bak, ec)) kv = parseKeyValues(bak);
    }
    return kv;
}

void writeKeyValues(const fs::path& path, const KeyValues& kv) {
    // Write to a temporary file first so a crash never leaves a truncated profile.
    const fs::path tmp = tempPathFor(path);
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
    // The new data may still be only in the OS cache, so a power cut soon after the rename can
    // leave the file empty or zero-filled: keep the previous one, long since on disk, as a backup.
    if (fs::exists(path, ec)) {
        const fs::path bak = backupPathFor(path);
        fs::rename(path, bak, ec);
        if (ec) { // a runtime that cannot rename over an existing file
            fs::remove(bak, ec);
            fs::rename(path, bak, ec);
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

void removeKeyValues(const fs::path& path) {
    std::error_code ec;
    fs::remove(path, ec);
    // A leftover temporary file or the backup must not bring the data back.
    fs::remove(tempPathFor(path), ec);
    fs::remove(backupPathFor(path), ec);
}

bool parseInteger(const std::string& s, long long& out, long long lo, long long hi) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtoll(s.c_str(), &end, 10);
    return end && *end == '\0' && out >= lo && out <= hi;
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

} // namespace

std::string encodeHistoryEntry(const RunHistoryEntry& e) {
    std::ostringstream ss;
    ss << e.seed << '|' << e.loadout << '|' << e.difficulty << '|' << e.depth << '|' << (e.sanctum ? 1 : 0) << '|'
       << (e.victory ? 1 : 0) << '|' << (e.daily ? 1 : 0) << '|' << static_cast<long long>(e.time) << '|' << e.kills << '|';
    if (e.ordeal > 0) ss << 'o' << e.ordeal << '|'; // older builds read it as part of the fate
    ss << e.fate;
    return ss.str();
}

bool decodeHistoryEntry(const std::string& text, RunHistoryEntry& out) {
    // Nine numeric fields, then the fate (which may itself contain anything but a newline).
    constexpr int kNumbers = 9;
    long long v[kNumbers];
    // seed, loadout, difficulty, depth, sanctum, victory, daily, time, kills
    constexpr long long kLimits[kNumbers][2] = {{0, 0xFFFFFFFFll}, {0, 63}, {0, kDifficultyCount - 1}, {0, kFinalStage},
                                                {0, 1}, {0, 1}, {0, 1}, {0, 1000000}, {0, 1000000}};
    size_t pos = 0;
    for (int i = 0; i < kNumbers; ++i) {
        const size_t bar = text.find('|', pos);
        if (bar == std::string::npos || !parseInteger(text.substr(pos, bar - pos), v[i], kLimits[i][0], kLimits[i][1]))
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
    // An ordeal tag ("o3|") may precede the fate; entries from before ordeals have none.
    out.ordeal = 0;
    if (pos < text.size() && text[pos] == 'o') {
        const size_t bar = text.find('|', pos);
        long long ordeal = 0;
        if (bar != std::string::npos && parseInteger(text.substr(pos + 1, bar - pos - 1), ordeal, 1, kMaxOrdeal)) {
            out.ordeal = static_cast<int>(ordeal);
            pos = bar + 1;
        }
    }
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

Settings settingsFromKeyValues(const KeyValues& kv) {
    Settings s;
    read(kv, "master_volume", s.masterVolume);
    read(kv, "music_volume", s.musicVolume);
    read(kv, "sfx_volume", s.sfxVolume);
    read(kv, "screen_shake", s.screenShake);
    read(kv, "fullscreen", s.fullscreen);
    read(kv, "show_fps", s.showFps);
    read(kv, "difficulty", s.difficulty);
    read(kv, "loadout", s.loadout);
    read(kv, "hints", s.hints);
    read(kv, "matter_lens", s.matterLens);
    read(kv, "high_quality", s.highQuality);
    read(kv, "ordeal", s.ordeal);
    s.ordeal = std::clamp(s.ordeal, 0, kMaxOrdeal);
    return s;
}

KeyValues settingsToKeyValues(const Settings& s) {
    return {{"master_volume", str(s.masterVolume)},
            {"music_volume", str(s.musicVolume)},
            {"sfx_volume", str(s.sfxVolume)},
            {"screen_shake", str(s.screenShake)},
            {"fullscreen", str(s.fullscreen)},
            {"show_fps", str(s.showFps)},
            {"difficulty", str(s.difficulty)},
            {"loadout", str(s.loadout)},
            {"hints", str(s.hints)},
            {"matter_lens", str(s.matterLens)},
            {"high_quality", str(s.highQuality)},
            {"ordeal", str(s.ordeal)}};
}

Profile profileFromKeyValues(const KeyValues& kv) {
    Profile p;
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
    read(kv, "feats", p.featsEarned);
    // Profiles from before ordeals: a completed Work has already earned the first.
    p.ordealUnlocked = p.victories > 0 ? 1 : 0;
    read(kv, "ordeal_unlocked", p.ordealUnlocked);
    p.ordealUnlocked = std::clamp(p.ordealUnlocked, 0, kMaxOrdeal);
    read(kv, "spells_seen", p.spellsSeen);
    read(kv, "perks_seen", p.perksSeen);
    read(kv, "codex_seen", p.codexSeen); // missing from profiles older than the codex: nothing witnessed yet
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
    // Profiles from before personal bests were kept: recover what the chronicle remembers,
    // and mark the gold best unknown so the next run does not claim a false record.
    if (p.runs > 0 && !kv.count("best_kills"))
        for (const RunHistoryEntry& e : p.history) p.bestKills = std::max(p.bestKills, e.kills);
    if (p.runs > 0 && !kv.count("best_gold")) p.bestGold = -1;
    return p;
}

KeyValues profileToKeyValues(const Profile& p) {
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
                 {"feats", str(p.featsEarned)},
                 {"ordeal_unlocked", str(p.ordealUnlocked)},
                 {"spells_seen", str(p.spellsSeen)},
                 {"perks_seen", str(p.perksSeen)},
                 {"codex_seen", str(p.codexSeen)},
                 {"creature_kills", joinInts(p.creatureKills)}};
    for (size_t i = 0; i < p.history.size() && i < Profile::kHistorySize; ++i) kv[historyKey(i)] = encodeHistoryEntry(p.history[i]);
    return kv;
}

Settings loadSettings() { return settingsFromKeyValues(readKeyValues(pathFor("settings.cfg"))); }

void saveSettings(const Settings& s) { writeKeyValues(pathFor("settings.cfg"), settingsToKeyValues(s)); }

Profile loadProfile() { return profileFromKeyValues(readKeyValues(pathFor("profile.dat"))); }

void saveProfile(const Profile& p) { writeKeyValues(pathFor("profile.dat"), profileToKeyValues(p)); }

} // namespace pm
