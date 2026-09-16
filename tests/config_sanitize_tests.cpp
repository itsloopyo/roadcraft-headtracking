// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Boundary tests for the values HeadTracking.ini feeds into the camera path.

#include "config_sanitize.h"

#include "test_support.h"

#include <cstdio>
#include <limits>

using namespace rc_ht;
using rc_test::Check;

namespace {

const float kNan = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();
const float kFloatMax = std::numeric_limits<float>::max();

void SmoothingTests() {
    std::printf("SanitizeSmoothing\n");
    Check(SanitizeSmoothing(0.0f, 0.0f) == 0.0f, "0 passes through");
    Check(SanitizeSmoothing(0.15f, 0.0f) == 0.15f, "in-range passes through");
    Check(SanitizeSmoothing(1.0f, 0.0f) == 1.0f, "1 passes through");
    Check(SanitizeSmoothing(0.0f, 0.15f) == 0.0f,
          "a configured 0 on the remote key is never raised to its 0.15 default");
    Check(SanitizeSmoothing(5.0f, 0.0f) == 1.0f, "above 1 clamps to 1");
    Check(SanitizeSmoothing(-2.0f, 0.15f) == 0.0f, "below 0 clamps to the bound, not the fallback");
    Check(SanitizeSmoothing(kNan, 0.15f) == 0.15f, "NaN takes the key's own default");
    Check(SanitizeSmoothing(kInf, 0.0f) == 0.0f, "Inf takes the key's own default");
    Check(SanitizeSmoothing(-kInf, 0.15f) == 0.15f, "-Inf takes the key's own default");
}

void PositionLimitTests() {
    std::printf("SanitizePositionLimit\n");
    Check(SanitizePositionLimit(0.30f, 0.30f) == 0.30f, "default passes through");
    Check(SanitizePositionLimit(0.50f, 0.30f) == 0.50f, "the documented maximum passes through");
    Check(SanitizePositionLimit(0.0f, 0.30f) == 0.0f, "zero pins the axis");
    Check(SanitizePositionLimit(-0.5f, 0.30f) == 0.0f, "a negative limit clamps to 0");
    Check(SanitizePositionLimit(kNan, 0.30f) == 0.30f, "NaN takes the fallback");
    Check(SanitizePositionLimit(kInf, 0.20f) == 0.20f, "Inf takes the fallback");
    Check(SanitizePositionLimit(40.0f, 0.40f) == 0.5f, "centimetres typed as metres clamp to 0.5");
    Check(SanitizePositionLimit(kFloatMax, 0.30f) == 0.5f, "float max clamps to 0.5");
}

void BindableKeyTests() {
    std::printf("IsBindableVirtualKey\n");
    Check(IsBindableVirtualKey(0x23), "End");
    Check(IsBindableVirtualKey(0x59), "Y");
    Check(IsBindableVirtualKey(0xFE), "the top of the range");
    Check(!IsBindableVirtualKey(0x00), "0");
    Check(!IsBindableVirtualKey(0x01), "left mouse button");
    Check(!IsBindableVirtualKey(0x02), "right mouse button");
    Check(!IsBindableVirtualKey(0x06), "mouse X2");
    Check(!IsBindableVirtualKey(0x10), "Shift");
    Check(!IsBindableVirtualKey(0x11), "Ctrl");
    Check(!IsBindableVirtualKey(0x12), "Alt");
    Check(!IsBindableVirtualKey(0xA2), "left Ctrl");
    Check(!IsBindableVirtualKey(0xA5), "right Alt");
    Check(!IsBindableVirtualKey(0xFF), "past the range");
    Check(!IsBindableVirtualKey(0x230), "a mistyped extra digit");
}

}  // namespace

int main() {
    std::printf("RoadCraft head tracking - config boundary tests\n");
    std::printf("==============================================\n");
    SmoothingTests();
    PositionLimitTests();
    BindableKeyTests();
    return rc_test::Summary("config boundary");
}
