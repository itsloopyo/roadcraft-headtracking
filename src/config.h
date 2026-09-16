// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>
#include <string>

#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"

namespace rc_ht {

// The shipped smoothing defaults, named once: they seed the Config below AND
// are what a malformed value in that key falls back to, and those two have to
// be the same number or a bad RemoteSmoothing lands on the local default.
constexpr float kDefaultLocalSmoothing =
    static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
constexpr float kDefaultRemoteSmoothing =
    static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

struct Config {
    // Held as the socket's own type so an out-of-range INI value cannot reach
    // UdpReceiver::Start by truncating to a different 16-bit port.
    std::uint16_t udp_port = 4242;
    bool enable_on_startup = true;

    // Virtual key codes. Every action has a nav-cluster key and a Ctrl+Shift
    // chord letter, and both fire it.
    int toggle_key = 0x23;
    int cycle_mode_key = 0x21;
    int chord_toggle_key = 0x59;
    int chord_cycle_mode_key = 0x47;

    float local_smoothing = kDefaultLocalSmoothing;
    float remote_smoothing = kDefaultRemoteSmoothing;

    bool position_enabled = true;
    float limit_x = cameraunlock::PositionSettings{}.limit_x;
    float limit_y = cameraunlock::PositionSettings{}.limit_y;
    float limit_y_down = cameraunlock::PositionSettings{}.limit_y_down;
    float limit_z = cameraunlock::PositionSettings{}.limit_z;
    float limit_z_back = cameraunlock::PositionSettings{}.limit_z_back;
};

// Reads HeadTracking.ini from `exe_dir` over `out`. Absent or refused keys leave
// the member at whatever it already held, so a default-constructed Config yields
// the shipped defaults.
void LoadConfig(const std::string& exe_dir, Config& out);

// Writes the documented default HeadTracking.ini into `exe_dir` unless one is
// already there. Never overwrites a user's file.
void WriteDefaultConfigIfMissing(const std::string& exe_dir);

}  // namespace rc_ht
