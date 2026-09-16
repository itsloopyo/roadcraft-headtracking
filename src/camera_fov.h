// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace rc_ht {

// Byte offsets of the two field-of-view fields inside the render camera.
struct FovOffsets {
    unsigned horizontal;
    unsigned vertical;
};

// What the camera is projecting through on this frame, against the field of
// view the player configured for it.
struct FovReading {
    float horizontal_deg = 0.0f;
    float vertical_deg = 0.0f;
    float base_horizontal_deg = 0.0f;
    // tan(live/2) / tan(base/2), both horizontal. 1.0 whenever the camera is
    // at the field of view the player set, which is where it sits in ordinary
    // play.
    float zoom_factor = 1.0f;
    // tan(horizontal/2) / tan(vertical/2), which is the display aspect the
    // engine's own projection divides x by. Logged so that a build where the
    // two fields are not the horizontal and vertical angles this mod reads them
    // as shows up as an aspect that is not the display's.
    float aspect = 1.0f;
};

void InitFovTracking(const FovOffsets& offsets);

// True on the one reading that moved a camera's base up, which is the player
// having changed a field-of-view slider. Worth a line in the log: without it the
// only evidence of a base change is the factor going back to 1.0000, which is
// what ordinary play looks like, and the release gate is that ordinary play
// reads 1.0000. Valid only for the reading ReadFov just returned.
bool BaseWidenedOnLastRead();

// Forgets every camera's base. Called when a level load starts, because the load
// replaces the cameras and a base learned in the last level says nothing about
// the next one's. A field-of-view slider moved mid-level needs no reset: BaseFor
// recognises the move by how far above the base the reading sits.
void ResetFovTracking();

// Reads the camera's live field of view and pairs it with the base. False when
// the camera reports a field of view outside the range the engine's own
// Camera::SetHorizontalFov accepts, in which case the caller applies no
// compensation rather than a guessed one.
bool ReadFov(const void* camera, FovReading& out);

}  // namespace rc_ht
