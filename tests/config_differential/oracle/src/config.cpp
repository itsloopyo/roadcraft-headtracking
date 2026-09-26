// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <windows.h>

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "config_sanitize.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/protocol/port_utils.h"

namespace rc_ht {

namespace {

constexpr char kIniName[] = "HeadTracking.ini";

constexpr char kSectionNetwork[]   = "Network";
constexpr char kSectionGeneral[]   = "General";
constexpr char kSectionHotkeys[]   = "Hotkeys";
constexpr char kSectionSmoothing[] = "Smoothing";
constexpr char kSectionPosition[]  = "Position";

// Values here must stay in step with the Config member initialisers; the
// config_defaults test generates this file and loads it back over a poisoned
// Config to hold the two together.
constexpr char kDefaultIniText[] =
    "; RoadCraft Head Tracking - configuration\r\n"
    "; Edit values, restart the game to apply.\r\n"
    ";\r\n"
    "; Controls (remappable, see [Hotkeys]):\r\n"
    ";   End  / Ctrl+Shift+Y   toggle tracking\r\n"
    ";   PgUp / Ctrl+Shift+G   cycle tracking mode (rotation and position\r\n"
    ";                         / rotation only / position only)\r\n"
    ";\r\n"
    "; Centre, sensitivity, curves and axis inversion belong in your tracker\r\n"
    "; (OpenTrack, the phone app), so one profile works the same in every game.\r\n\r\n"
    "[Network]\r\n"
    "UdpPort=4242\r\n\r\n"
    "[General]\r\n"
    "EnableOnStartup=1\r\n\r\n"
    "[Hotkeys]\r\n"
    "; Windows virtual key codes, in hex. Each action has a nav-cluster key and a\r\n"
    "; Ctrl+Shift+<key> chord, and both fire it.\r\n"
    "; Common codes: End 0x23, Insert 0x2D, Delete 0x2E, PgUp 0x21,\r\n"
    "; PgDn 0x22, F1-F12 0x70-0x7B, A-Z 0x41-0x5A.\r\n"
    "ToggleKey=0x23\r\n"
    "CycleModeKey=0x21\r\n"
    "ChordToggleKey=0x59\r\n"
    "ChordCycleModeKey=0x47\r\n\r\n"
    "[Smoothing]\r\n"
    "; 0.0 none .. 1.0 heavy. Covers rotation and position. LocalSmoothing applies\r\n"
    "; to a tracker sending to 127.0.0.1; RemoteSmoothing to anything arriving over\r\n"
    "; the network, including this PC's own LAN address.\r\n"
    "LocalSmoothing=0.0\r\n"
    "RemoteSmoothing=0.15\r\n\r\n"
    "[Position]\r\n"
    "Enabled=1\r\n"
    "; How far the head may move the camera, in metres, 0 to 0.5. LimitZ is leaning\r\n"
    "; forward and LimitZBack is leaning away; LimitY is up and LimitYDown is down.\r\n"
    "LimitX=0.30\r\n"
    "LimitY=0.20\r\n"
    "LimitYDown=0.20\r\n"
    "LimitZ=0.40\r\n"
    "LimitZBack=0.10\r\n";

std::string IniPath(const std::string& exe_dir) {
    return exe_dir + "\\" + kIniName;
}

float UseSanitized(const char* name, float raw, float clean) {
    if (raw != clean) {
        Log::Line("[config] %s=%.4f is out of range or not finite; using %.4f", name, raw, clean);
    }
    return clean;
}

// Two probes with opposite fallbacks tell a parsed value from an echoed fallback,
// which IniReader::ReadBool cannot report by itself.
bool ParsedBool(const cameraunlock::IniReader& ini, const char* section,
                const char* key, bool& out) {
    const bool as_true = ini.ReadBool(section, key, true);
    if (as_true != ini.ReadBool(section, key, false)) return false;
    out = as_true;
    return true;
}

// Windows' INI reader hands back everything after the '=', inline comment and
// all, so every parser here has to decide what may follow the number. Trailing
// space and a ; or # comment may; anything else means the whole value was not
// the number, which is what refuses `LimitX=0,25` from a comma-decimal machine
// rather than reading it as 0.
bool NothingFollowsButAComment(const char* end) {
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    return *end == '\0' || *end == ';' || *end == '#';
}

// Parsed in the C locale, so the decimal point is a full stop whatever the
// machine is set to.
bool ParseFloatText(const std::string& text, float& out) {
    const char* start = text.c_str();
    char* end = nullptr;
    static const _locale_t c_numeric = _create_locale(LC_NUMERIC, "C");
    if (c_numeric == nullptr) return false;
    const double value = _strtod_l(start, &end, c_numeric);
    if (end == start || !NothingFollowsButAComment(end)) return false;
    out = static_cast<float>(value);
    return true;
}

bool ParseDecimalInt(const std::string& text, long& out) {
    const char* start = text.c_str();
    char* end = nullptr;
    const long value = std::strtol(start, &end, 10);
    if (end == start || !NothingFollowsButAComment(end)) return false;
    out = value;
    return true;
}

bool ReadFlag(const cameraunlock::IniReader& ini, const char* section,
              const char* key, bool current) {
    const std::string text = ini.ReadString(section, key, "");
    if (text.empty()) return current;

    bool parsed = false;
    if (ParsedBool(ini, section, key, parsed)) return parsed;

    const bool has_comment = text.find(';') != std::string::npos
                          || text.find('#') != std::string::npos;
    Log::Line("[config] %s=%s is not 0 or 1 (or true/false, yes/no, on/off); keeping %d.%s",
              key, text.c_str(), current ? 1 : 0,
              has_comment ? " A trailing ; comment is part of the value here - put comments on "
                            "their own line above the key." : "");
    return current;
}

template <typename Sanitize>
float ReadFloatValue(const cameraunlock::IniReader& ini, const char* section,
                     const char* key, float current, Sanitize sanitize) {
    const std::string text = ini.ReadString(section, key, "");
    if (text.empty()) return current;

    float raw = 0.0f;
    if (!ParseFloatText(text, raw)) {
        Log::Line("[config] %s=%s is not a number; keeping %.4f.%s", key, text.c_str(), current,
                  text.find(',') != std::string::npos
                      ? " Use a full stop for the decimal point, not a comma." : "");
        return current;
    }
    return UseSanitized(key, raw, sanitize(raw));
}

float ReadSmoothing(const cameraunlock::IniReader& ini, const char* key, float current,
                    float shipped_default) {
    return ReadFloatValue(ini, kSectionSmoothing, key, current, [shipped_default](float raw) {
        return SanitizeSmoothing(raw, shipped_default);
    });
}

float ReadLimit(const cameraunlock::IniReader& ini, const char* key, float current) {
    return ReadFloatValue(ini, kSectionPosition, key, current, [current](float raw) {
        return SanitizePositionLimit(raw, current);
    });
}

// Hex, as the shipped file writes them: a bare 24 is 0x24. The whole value has
// to be the code, because key names like "End" parse as hex digits.
bool ParseVirtualKey(const std::string& text, int& out) {
    const char* start = text.c_str();
    if (text.size() > 2 && start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) start += 2;
    char* end = nullptr;
    const long value = std::strtol(start, &end, 16);
    if (end == start || !NothingFollowsButAComment(end)) return false;
    out = static_cast<int>(value);
    return true;
}

int ReadKey(const cameraunlock::IniReader& ini, const char* key, int fallback) {
    const std::string text = ini.ReadString(kSectionHotkeys, key, "");
    if (text.empty()) return fallback;

    int raw = 0;
    if (!ParseVirtualKey(text, raw)) {
        Log::Line("[config] %s=%s is not a virtual key code (0x23, or 23 read as hex); "
                  "keeping 0x%X", key, text.c_str(), fallback);
        return fallback;
    }
    if (!IsBindableVirtualKey(raw)) {
        Log::Line("[config] %s=%s is not a key that can be bound; keeping 0x%X",
                  key, text.c_str(), fallback);
        return fallback;
    }
    return raw;
}

void ReadUdpPort(const cameraunlock::IniReader& ini, Config& out) {
    const std::string text = ini.ReadString(kSectionNetwork, "UdpPort", "");
    if (text.empty()) return;

    long parsed = 0;
    if (!ParseDecimalInt(text, parsed)) {
        Log::Line("[config] UdpPort=%s is not a number; using %u", text.c_str(),
                  static_cast<unsigned>(out.udp_port));
        return;
    }
    bool valid = false;
    const std::uint16_t port = cameraunlock::NormalizeUdpPort(
        static_cast<int>(parsed), out.udp_port, valid);
    if (valid) {
        out.udp_port = port;
        return;
    }
    Log::Line("[config] UdpPort=%s is outside 1024-65535; using %u", text.c_str(),
              static_cast<unsigned>(out.udp_port));
}

// HotkeyPoller keeps a list, so two actions on one code both run on a press.
// Both bindings go back to what they came in as. Nav and chord keys are separate
// guard classes and may share a code.
void RefuseCollidingHotkeys(const Config& previous, Config& out) {
    struct Pair { const char* a; const char* b; int Config::* ma; int Config::* mb; };
    static const Pair kPairs[] = {
        { "ToggleKey", "CycleModeKey", &Config::toggle_key, &Config::cycle_mode_key },
        { "ChordToggleKey", "ChordCycleModeKey", &Config::chord_toggle_key,
          &Config::chord_cycle_mode_key },
    };
    for (const Pair& p : kPairs) {
        if (out.*p.ma != out.*p.mb) continue;
        Log::Line("[config] %s and %s are both 0x%X - one key cannot run two actions; "
                  "keeping 0x%X and 0x%X", p.a, p.b, out.*p.ma, previous.*p.ma, previous.*p.mb);
        out.*p.ma = previous.*p.ma;
        out.*p.mb = previous.*p.mb;
    }
}

// GetPrivateProfileString does not skip a UTF-8 BOM, so a file that opens with a
// section header loses every key in that section.
void WarnUtf8Bom(const std::string& path) {
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "rb") != 0 || file == nullptr) return;
    unsigned char head[3] = { 0, 0, 0 };
    const std::size_t got = std::fread(head, 1, sizeof(head), file);
    std::fclose(file);
    if (got != sizeof(head) || head[0] != 0xEF || head[1] != 0xBB || head[2] != 0xBF) return;
    Log::Line("[config] %s starts with a UTF-8 byte order mark, which Windows' INI reader does "
              "not skip: if a section header is on the first line, every key in that section "
              "is ignored. Re-save the file without a BOM.", path.c_str());
}

// GetPrivateProfileSectionA reports nSize - 2 when the section did not fit and
// offers no way to ask how much it needed, so the buffer grows until it does.
// Giving up quietly on the first size is what this used to do, and it turned the
// one diagnostic whose whole job is naming a key the mod does not read into
// silence for exactly the file that most needs it - the one with enough in the
// section to overflow.
constexpr std::size_t kFirstSectionBytes = 4096;
constexpr std::size_t kMaxSectionBytes = 64 * 1024;

bool ReadWholeSection(const std::string& path, const char* section, std::vector<char>& buffer) {
    for (std::size_t size = kFirstSectionBytes; size <= kMaxSectionBytes; size *= 2) {
        buffer.assign(size, '\0');
        const DWORD used = GetPrivateProfileSectionA(section, buffer.data(),
                                                     static_cast<DWORD>(size), path.c_str());
        if (used < size - 2) return true;
    }
    Log::Line("[config] [%s] holds more than %zu bytes, so it could not be checked for keys this "
              "mod does not read. Anything misspelled in that section is silently doing nothing.",
              section, kMaxSectionBytes);
    return false;
}

void WarnUnknownKeysInSection(const std::string& path, const char* section,
                              const char* const* known, std::size_t known_count) {
    std::vector<char> buffer;
    if (!ReadWholeSection(path, section, buffer)) return;

    for (const char* entry = buffer.data(); *entry != '\0'; entry += std::strlen(entry) + 1) {
        const char* text = entry;
        while (*text == ' ' || *text == '\t') ++text;
        if (*text == '#' || *text == ';') continue;
        const char* equals = std::strchr(text, '=');
        if (equals == nullptr) continue;
        std::string name(text, equals);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
        if (name.empty()) continue;

        bool recognised = false;
        for (std::size_t i = 0; i < known_count && !recognised; ++i) {
            recognised = _stricmp(name.c_str(), known[i]) == 0;
        }
        if (recognised) continue;
        Log::Line("[config] [%s] %s is not a key this mod reads, so it does nothing. Check the "
                  "spelling and the section it is under.", section, name.c_str());
    }
}

// Deduced from the array rather than passed alongside it: a hand-written
// count that outlives an edit to the list reports the key it no longer
// covers as unknown, in the one diagnostic whose whole job is spotting a
// key the mod does not read.
template <std::size_t N>
void WarnUnknownKeysInSection(const std::string& path, const char* section,
                              const char* const (&known)[N]) {
    WarnUnknownKeysInSection(path, section, known, N);
}

void WarnUnknownKeys(const std::string& path) {
    static const char* const kNetwork[]   = { "UdpPort" };
    static const char* const kGeneral[]   = { "EnableOnStartup" };
    static const char* const kHotkeys[]   = { "ToggleKey", "CycleModeKey", "ChordToggleKey",
                                              "ChordCycleModeKey" };
    static const char* const kSmoothing[] = { "LocalSmoothing", "RemoteSmoothing" };
    static const char* const kPosition[]  = { "Enabled", "LimitX", "LimitY", "LimitYDown",
                                              "LimitZ", "LimitZBack" };
    WarnUnknownKeysInSection(path, kSectionNetwork, kNetwork);
    WarnUnknownKeysInSection(path, kSectionGeneral, kGeneral);
    WarnUnknownKeysInSection(path, kSectionHotkeys, kHotkeys);
    WarnUnknownKeysInSection(path, kSectionSmoothing, kSmoothing);
    WarnUnknownKeysInSection(path, kSectionPosition, kPosition);
}

}  // namespace

void LoadConfig(const std::string& exe_dir, Config& out) {
    const std::string path = IniPath(exe_dir);
    cameraunlock::IniReader ini;
    if (!ini.Open(path)) {
        Log::Line("[config] could not open %s - using built-in defaults", path.c_str());
        return;
    }

    WarnUtf8Bom(path);
    WarnUnknownKeys(path);

    const Config previous = out;

    ReadUdpPort(ini, out);
    out.enable_on_startup = ReadFlag(ini, kSectionGeneral, "EnableOnStartup", out.enable_on_startup);

    out.toggle_key           = ReadKey(ini, "ToggleKey",         out.toggle_key);
    out.cycle_mode_key       = ReadKey(ini, "CycleModeKey",      out.cycle_mode_key);
    out.chord_toggle_key     = ReadKey(ini, "ChordToggleKey",    out.chord_toggle_key);
    out.chord_cycle_mode_key = ReadKey(ini, "ChordCycleModeKey", out.chord_cycle_mode_key);
    RefuseCollidingHotkeys(previous, out);

    out.local_smoothing  = ReadSmoothing(ini, "LocalSmoothing",  out.local_smoothing,
                                         kDefaultLocalSmoothing);
    out.remote_smoothing = ReadSmoothing(ini, "RemoteSmoothing", out.remote_smoothing,
                                         kDefaultRemoteSmoothing);

    out.position_enabled = ReadFlag(ini, kSectionPosition, "Enabled", out.position_enabled);
    out.limit_x      = ReadLimit(ini, "LimitX",     out.limit_x);
    out.limit_y      = ReadLimit(ini, "LimitY",     out.limit_y);
    out.limit_y_down = ReadLimit(ini, "LimitYDown", out.limit_y_down);
    out.limit_z      = ReadLimit(ini, "LimitZ",     out.limit_z);
    out.limit_z_back = ReadLimit(ini, "LimitZBack", out.limit_z_back);
}

void WriteDefaultConfigIfMissing(const std::string& exe_dir) {
    const std::string path = IniPath(exe_dir);

    // CREATE_NEW, so a file written between a check and a truncating open is
    // never overwritten.
    const HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_EXISTS) return;
        Log::Line("[config] could not create %s (%lu) - the game directory is not writable. "
                  "Built-in defaults are in use and edits there will not be read.",
                  path.c_str(), error);
        return;
    }

    constexpr DWORD kTextBytes = static_cast<DWORD>(sizeof(kDefaultIniText) - 1);
    DWORD written = 0;
    const BOOL ok = WriteFile(file, kDefaultIniText, kTextBytes, &written, nullptr);
    const DWORD write_error = ok ? 0 : GetLastError();
    CloseHandle(file);
    if (!ok || written != kTextBytes) {
        Log::Line("[config] %s was created but only %lu of %lu bytes could be written (%lu); "
                  "delete it and restart the game for a complete default config.",
                  path.c_str(), written, kTextBytes, write_error);
    }
}

}  // namespace rc_ht
