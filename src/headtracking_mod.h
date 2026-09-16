// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <mutex>

#include "camera_transform.h"

namespace rc_ht {

// Pins this module and hands the bootstrap to a new thread. There is no
// teardown: the pin keeps the detours, receiver and hotkey thread alive until
// the process exits, which is what reclaims them.
void Initialize();

// Serialises the composed camera path against itself and against the hotkey
// thread. The engine dispatches the camera update onto its job system rather
// than onto one render thread: a trace of the hooked function caught 16 calls
// spread across 12 distinct thread ids, all for the same camera. Nothing in the
// pipeline the path advances - the frame clock, the interpolators, the
// processors, the gate weight, the field-of-view bases - is written to be read
// from more than one thread, so the path takes this for its whole body instead
// of assuming an affinity the engine does not offer. Uncontended, once a frame.
std::mutex& CameraPathMutex();

// The pose to compose into this frame's camera, already scaled for the field of
// view the camera is rendering and re-clamped to the configured travel limits.
// Advances the pipeline, so the camera hook calls it exactly once per frame,
// holding CameraPathMutex(). False when there is nothing to compose: no tracker
// data yet, or the gate has been shut long enough for the pose to have eased
// back to identity.
bool PoseForThisFrame(HeadPose& pose, float zoom_factor);

}  // namespace rc_ht
