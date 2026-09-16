// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "cameraunlock/memory/pe_fingerprint.h"

namespace rc_ht::builds {

// Everything this mod pins to a specific `Roadcraft - Retail.exe` build.
struct OffsetTable {
    // Camera::SetTransform(camera, position, up row, right row, forward row).
    // Writes the camera's world matrix and rebuilds its view, frustum planes and
    // corners from it, so composing the head pose into its arguments turns
    // rendering, visibility and the HUD's world markers together.
    unsigned int camera_set_transform_rva;

    // The two returns from SetTransform inside the camera component system, the
    // only caller that drives the camera the player looks through. The other
    // callers set up cameras of their own and are left alone.
    //
    // Both are arms of the one per-frame call site, not two live ones: a trace of
    // 16 calls returned to _b every time and to _a never, and an in-game log over
    // 13 minutes recorded three camera changes rather than the per-frame pair a
    // second live site would produce. _a stays because a build where the other
    // arm is taken would otherwise go silently uncomposed.
    unsigned int camera_system_return_a_rva;
    unsigned int camera_system_return_b_rva;

    // Byte offsets of the live field of view, in whole degrees, in the camera
    // object SetTransform is handed. Camera::SetHorizontalFov stores its
    // argument at the first and derives the second as
    // 2*atan(tan(arg/2) * aspect_ratio), which is what fixes the first as the
    // horizontal angle and the second as the vertical one.
    unsigned int camera_horizontal_fov;
    unsigned int camera_vertical_fov;

    // The UI loading screen singleton: non-null from the moment a level load
    // starts until the player is handed the vehicle, which covers the stretch
    // after the menu sound state has already flipped to menu_off.
    unsigned int loading_screen_global_rva;

    // The Hydra matchmaking client singleton, its game session manager member,
    // and the manager's session pointer and state. Both vtables are checked
    // before either object is trusted.
    unsigned int matchmaking_client_global_rva;
    unsigned int matchmaking_client_vtable_rva;
    unsigned int client_session_manager;
    unsigned int session_manager_vtable_rva;
    unsigned int session_manager_session;
    unsigned int session_manager_state;
};

struct BuildProfile {
    const char* Name;
    cameraunlock::memory::PeFingerprint Fingerprint;
    OffsetTable Offsets;
};

// A profile whose fingerprint routes but whose addresses are not derived yet
// leaves the mod dormant.
inline bool IsProfileComplete(const BuildProfile& p) {
    const OffsetTable& o = p.Offsets;
    return o.camera_set_transform_rva != 0
        && o.camera_system_return_a_rva != 0
        && o.camera_system_return_b_rva != 0
        && o.camera_horizontal_fov != 0
        && o.camera_vertical_fov != 0
        && o.loading_screen_global_rva != 0
        && o.matchmaking_client_global_rva != 0
        && o.matchmaking_client_vtable_rva != 0
        && o.client_session_manager != 0
        && o.session_manager_vtable_rva != 0
        && o.session_manager_session != 0
        && o.session_manager_state != 0;
}

}  // namespace rc_ht::builds
