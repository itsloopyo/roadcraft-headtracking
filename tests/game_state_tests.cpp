// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The gate's pure half: the classification, and the Wwise ID hash the menu gate
// matches on. The IDs below are the ones the running game passed to SetState.

#include "game_state.h"

#include "logging.h"
#include "test_support.h"

#include <cstdio>

using namespace rc_ht;
using rc_test::Check;

int main() {
    std::printf("RoadCraft head tracking - gameplay gate tests\n");
    std::printf("=============================================\n");

    std::printf("WwiseShortId\n");
    Check(WwiseShortId("ui_menu_toggle") == 0x6F1815B6u, "ui_menu_toggle is 6F1815B6");
    Check(WwiseShortId("menu_off") == 0xB5031356u, "menu_off is B5031356");
    Check(WwiseShortId("mus_ui_viewport_visibility") == 0xDE31714Bu,
          "mus_ui_viewport_visibility is DE31714B");
    Check(WwiseShortId("vis_off") == 0x39D298F7u, "vis_off is 39D298F7");
    Check(WwiseShortId("UI_Menu_Toggle") == WwiseShortId("ui_menu_toggle"),
          "names are hashed lower-cased");

    std::printf("ShouldFollowHead\n");
    Check(ShouldFollowHead(true, false, false, false), "driving with tracking on follows");
    Check(!ShouldFollowHead(false, false, false, false), "the toggle turns it off");
    Check(!ShouldFollowHead(true, true, false, false), "a menu holds it off");
    Check(!ShouldFollowHead(true, false, true, false), "a loading screen holds it off");
    Check(!ShouldFollowHead(true, false, false, true), "a co-op session holds it off");

    return rc_test::Summary("gameplay gate");
}
