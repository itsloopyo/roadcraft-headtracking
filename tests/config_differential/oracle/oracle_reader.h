// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "cameraunlock/data/position_settings.h"

// The oracle: what v0.1.0, the newest published build, ran on after reading
// HeadTracking.ini. oracle_reader.cpp compiles v0.1.0's reader from byte copies
// and transcribes the startup code that consumed it.
namespace rc_oracle {

enum Action { kToggle = 0, kCycleMode = 1 };

// One HotkeyPoller registration: the action, the code, and 3 where the callback
// is ChordGuarded (fires only while Ctrl and Shift are both held), 0 where it is
// NavGuarded (fires unless Ctrl and Shift are both held).
using Registration = std::tuple<int, int, unsigned>;

// TrackingMode's numbers.
enum Mode { kRotationAndPosition = 0, kRotationOnly = 1, kPositionOnly = 2 };

// The travel limits the camera path clamps the zoom-scaled lean to
// (ConfiguredLeanLimits).
struct LeanLimits {
    float x = 0;
    float y_up = 0;
    float y_down = 0;
    float z_forward = 0;
    float z_back = 0;
};

struct Published {
    unsigned udp_port = 0;
    bool tracking_enabled = false;
    // What ApplyConfigToPipeline handed the session: its position settings and
    // its smoothing pair.
    cameraunlock::PositionSettings position;
    float local_smoothing = 0;
    float remote_smoothing = 0;
    LeanLimits lean;
    int mode = -1;
    std::vector<Registration> hotkeys;
};

// `exe_dir` is the game folder, as the bootstrap resolved it, with no trailing
// backslash.
Published Read(const std::string& exe_dir);

// What v0.1.0's WriteDefaultConfigIfMissing writes into `exe_dir` on a first
// start with no HeadTracking.ini there.
void WriteFirstRunFile(const std::string& exe_dir);

}  // namespace rc_oracle
