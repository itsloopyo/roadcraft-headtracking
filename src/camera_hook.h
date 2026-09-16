// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace rc_ht {

// Detours Camera::SetTransform for the active build profile. Only the camera
// component system's calls are composed; every other camera passes through.
bool InstallCameraHook();

}  // namespace rc_ht
