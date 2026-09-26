// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cmath>

namespace rc_ht {

// Boundary validation for values read from the user-editable HeadTracking.ini.
// IniReader parses floats with strtod, which accepts "nan" and "inf", so every
// float that reaches the camera path goes through one of these first.

inline float SanitizeFinite(float v, float fallback) {
    return std::isfinite(v) ? v : fallback;
}

inline float ClampRange(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Validation, never a floor: any value inside [0,1] reaches the processor
// untouched, 0.0 included. `fallback` is the default of the key being read, so a
// malformed RemoteSmoothing lands on 0.15 rather than on the local 0.0.
inline float SanitizeSmoothing(float v, float fallback) {
    return ClampRange(SanitizeFinite(v, fallback), 0.0f, 1.0f);
}

// A GetAsyncKeyState-watchable key that is not a mouse button or a modifier.
// Ctrl and Shift are what the chord guard tests, so a binding on one of them
// either never fires or fires with every chord.
inline bool IsBindableVirtualKey(int v) {
    if (v < 0x01 || v > 0xFE) return false;
    if (v <= 0x06) return false;
    if (v >= 0x10 && v <= 0x12) return false;
    if (v >= 0xA0 && v <= 0xA5) return false;
    return true;
}

// Travel limits in metres. A negative limit inverts the processor's clamp and a
// non-finite one puts NaN into the eye position.
constexpr float kMaxPositionLimit = 0.5f;

inline float SanitizePositionLimit(float v, float fallback) {
    return ClampRange(SanitizeFinite(v, fallback), 0.0f, kMaxPositionLimit);
}

}  // namespace rc_ht
