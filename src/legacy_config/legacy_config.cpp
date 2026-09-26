// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Frozen. See legacy_config.h.

#include "legacy_config.h"

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

namespace rc_ht::legacy {

namespace {

// The shipped smoothing defaults, mirroring the Config member initialisers:
// what a malformed value in that key falls back to.
constexpr float kDefaultLocalSmoothing = 0.0f;
constexpr float kDefaultRemoteSmoothing = 0.15f;

constexpr char kSectionNetwork[]   = "Network";
constexpr char kSectionGeneral[]   = "General";
constexpr char kSectionHotkeys[]   = "Hotkeys";
constexpr char kSectionSmoothing[] = "Smoothing";
constexpr char kSectionPosition[]  = "Position";

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

bool LoadConfig(const std::string& path, Config& out) {
    cameraunlock::IniReader ini;
    if (!ini.Open(path)) {
        Log::Line("[config] could not open %s - using built-in defaults", path.c_str());
        return false;
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
    return true;
}

std::vector<Key> ReadKeys() {
    return {
        {"Network", "UdpPort"},
        {"General", "EnableOnStartup"},
        {"Hotkeys", "ToggleKey"},
        {"Hotkeys", "CycleModeKey"},
        {"Hotkeys", "ChordToggleKey"},
        {"Hotkeys", "ChordCycleModeKey"},
        {"Smoothing", "LocalSmoothing"},
        {"Smoothing", "RemoteSmoothing"},
        {"Position", "Enabled"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitYDown"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
    };
}

}  // namespace rc_ht::legacy
