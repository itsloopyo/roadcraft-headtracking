// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "builds/build_profile.h"

// Every Steam build profile lives here, append-only. A patch gets a new profile
// at the top of kKnownProfiles; an existing profile's numbers are never edited,
// so a player still on the older build keeps matching it.

namespace rc_ht::builds {

// RoadCraft, Steam app 2104890, root\bin\pc\Roadcraft - Retail.exe linked
// 2026-09-02 14:28:36 UTC (Steam build id 25119957). The TimeDateStamp below is
// what routes; this line is only the human handle on it, so it states the UTC
// that field decodes to rather than the local time a tool printed it in.
extern const BuildProfile kSteamProfile_20260902 = {
    "steam-win64-20260902",
    { 0x6A983294, 0x05E71000, 0x056A9703 },
    {
        /* camera_set_transform_rva       */ 0x023811E0,
        /* camera_system_return_a_rva     */ 0x012D9635,
        /* camera_system_return_b_rva     */ 0x012D99E7,
        /* camera_horizontal_fov          */ 0x2D0,
        /* camera_vertical_fov            */ 0x2D4,
        /* loading_screen_global_rva      */ 0x053CE8F8,
        /* matchmaking_client_global_rva  */ 0x05306200,
        /* matchmaking_client_vtable_rva  */ 0x038A8370,
        /* client_session_manager         */ 0x168,
        /* session_manager_vtable_rva     */ 0x038A8120,
        /* session_manager_session        */ 0x30,
        /* session_manager_state          */ 0xC8,
    },
};

// Same exe, linked 2026-09-11 16:47:54 UTC. The patch left the image the same
// size and moved parts of .text by 0x10 rather than rebuilding it: every address
// below was re-resolved against a dump of the running build, and each one is the
// unanimous answer of every instruction in .text that references it. The camera
// system's two call sites did not move and now call 0x023811D0; the loading
// screen global lost 0x10 with the code around it; the matchmaking globals and
// both vtables did not move at all.
extern const BuildProfile kSteamProfile_20260911 = {
    "steam-win64-20260911",
    { 0x6AA430BA, 0x05E71000, 0x056A73B0 },
    {
        /* camera_set_transform_rva       */ 0x023811D0,
        /* camera_system_return_a_rva     */ 0x012D9635,
        /* camera_system_return_b_rva     */ 0x012D99E7,
        /* camera_horizontal_fov          */ 0x2D0,
        /* camera_vertical_fov            */ 0x2D4,
        /* loading_screen_global_rva      */ 0x053CE8E8,
        /* matchmaking_client_global_rva  */ 0x05306200,
        /* matchmaking_client_vtable_rva  */ 0x038A8370,
        /* client_session_manager         */ 0x168,
        /* session_manager_vtable_rva     */ 0x038A8120,
        /* session_manager_session        */ 0x30,
        /* session_manager_state          */ 0xC8,
    },
};

}  // namespace rc_ht::builds
