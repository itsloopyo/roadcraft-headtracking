// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// v0.1.0's reader and startup code, the newest published build (tag 1368652).
//
// The reader is compiled from byte copies: src/config.cpp, src/config.h,
// src/config_sanitize.h and src/logging.h beside this file are v0.1.0's files
// (git show v0.1.0:src/<file>), which CMakeLists.txt pins by hash. They are
// included inside namespace rc_oracle, after every header they include, so the
// published rc_ht::Config and rc_ht::LoadConfig become rc_oracle::rc_ht's and
// cannot collide with the mod's own. Every cameraunlock-core source they
// include holds the same bytes at v0.1.0's pin (b8d47eb) and at this repo's
// (CMakeLists.txt pins those too).
//
// The startup code is transcribed from v0.1.0:src/headtracking_mod.cpp, which
// hooks the game and cannot be compiled into a test:
//
//   lines 70-84     ApplyConfigToPipeline, recording what it handed on
//   lines 146-149   ConfiguredLeanLimits, the clamp after the zoom scaling
//   lines 206-231   Bindings and RegisterHotkeys, recording each AddHotkey
//   lines 262-273   LoadAndApplyConfig: the pipeline and the enabled flag

#include "oracle_reader.h"

#include <windows.h>

#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/protocol/port_utils.h"

namespace rc_oracle {
#include "src/config.cpp"
}  // namespace rc_oracle

namespace rc_oracle {

Published Read(const std::string& exe_dir) {
    rc_ht::Config config;
    rc_ht::LoadConfig(exe_dir, config);

    Published p;
    p.udp_port = config.udp_port;

    // ApplyConfigToPipeline. SetPositionSettings replaces the smoothing fields
    // of the settings it is handed with the session's pair, which the two calls
    // before it had just set to these same values.
    p.local_smoothing = config.local_smoothing;
    p.remote_smoothing = config.remote_smoothing;
    p.position.limit_x = config.limit_x;
    p.position.limit_y = config.limit_y;
    p.position.limit_y_down = config.limit_y_down;
    p.position.limit_z = config.limit_z;
    p.position.limit_z_back = config.limit_z_back;
    p.position.local_smoothing = p.local_smoothing;
    p.position.remote_smoothing = p.remote_smoothing;
    p.mode = config.position_enabled ? kRotationAndPosition : kRotationOnly;

    p.lean = LeanLimits{config.limit_x, config.limit_y, config.limit_y_down, config.limit_z,
                        config.limit_z_back};

    p.tracking_enabled = config.enable_on_startup;

    // RegisterHotkeys: each action's nav key NavGuarded, its chord key
    // ChordGuarded.
    const struct { int nav_key; int chord_key; Action action; } bindings[] = {
        { config.toggle_key,     config.chord_toggle_key,     kToggle },
        { config.cycle_mode_key, config.chord_cycle_mode_key, kCycleMode },
    };
    for (const auto& binding : bindings) {
        p.hotkeys.emplace_back(binding.action, binding.nav_key, 0u);
        p.hotkeys.emplace_back(binding.action, binding.chord_key, 3u);
    }
    return p;
}

void WriteFirstRunFile(const std::string& exe_dir) {
    rc_ht::WriteDefaultConfigIfMissing(exe_dir);
}

}  // namespace rc_oracle
