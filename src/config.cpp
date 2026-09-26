// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <windows.h>

#include "legacy_config/legacy_config.h"
#include "logging.h"

namespace rc_ht {

namespace {

constexpr char kIniName[] = "HeadTracking.ini";

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

}  // namespace

void LoadConfig(const std::string& exe_dir, Config& out) {
    legacy::Config read;
    read.udp_port = out.udp_port;
    read.enable_on_startup = out.enable_on_startup;
    read.toggle_key = out.toggle_key;
    read.cycle_mode_key = out.cycle_mode_key;
    read.chord_toggle_key = out.chord_toggle_key;
    read.chord_cycle_mode_key = out.chord_cycle_mode_key;
    read.local_smoothing = out.local_smoothing;
    read.remote_smoothing = out.remote_smoothing;
    read.position_enabled = out.position_enabled;
    read.limit_x = out.limit_x;
    read.limit_y = out.limit_y;
    read.limit_y_down = out.limit_y_down;
    read.limit_z = out.limit_z;
    read.limit_z_back = out.limit_z_back;

    legacy::LoadConfig(IniPath(exe_dir), read);

    out.udp_port = read.udp_port;
    out.enable_on_startup = read.enable_on_startup;
    out.toggle_key = read.toggle_key;
    out.cycle_mode_key = read.cycle_mode_key;
    out.chord_toggle_key = read.chord_toggle_key;
    out.chord_cycle_mode_key = read.chord_cycle_mode_key;
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.position_enabled = read.position_enabled;
    out.limit_x = read.limit_x;
    out.limit_y = read.limit_y;
    out.limit_y_down = read.limit_y_down;
    out.limit_z = read.limit_z;
    out.limit_z_back = read.limit_z_back;
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
