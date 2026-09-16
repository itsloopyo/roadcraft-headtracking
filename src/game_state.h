// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

namespace rc_ht {

// Hooks the sound engine's state setter and resolves the matchmaking session
// objects for the active build profile. False leaves the mod refusing to track
// rather than tracking through a menu or a co-op session it cannot see.
bool InitGameState();

// Whether a full-screen menu is up. RoadCraft's pause menu keeps the world
// running and the same camera, so the camera path cannot tell it apart from
// driving; the UI does tell the sound engine, through the Wwise state group
// ui_menu_toggle (menu_on for the pause menu and photo mode, menu_eagle_mod_on
// for the map, menu_off in gameplay). Anything but menu_off counts as a menu,
// and so does the time before the first state arrives.
bool IsMenuOpen();

// Whether the level loading screen is up. The menu state has already gone to
// menu_off by then, and the camera being driven is the loading scene's.
bool IsLoadingScreenUp();

// Whether a Hydra co-op session exists: hosted or joined, with or without other
// players in it yet. An object that fails its vtable check reads as live.
bool IsMultiplayerSessionLive();

// Pure classification of the inputs the gate combines.
bool ShouldFollowHead(bool tracking_enabled, bool menu_open, bool loading,
                      bool multiplayer_session_live);

// A Wwise short ID: 32-bit FNV-1 of the lower-cased name, which is how the sound
// engine turns a state group or state name into the ID it passes around.
std::uint32_t WwiseShortId(const char* name);

}  // namespace rc_ht
