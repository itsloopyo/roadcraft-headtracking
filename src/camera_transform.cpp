// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_transform.h"

#include <cmath>

#include "cameraunlock/camera/zoom_compensation.h"

namespace rc_ht {

namespace {

constexpr float kDegreesToRadians = 0.01745329252f;

// Rotates the orthonormal pair (a, b) within their plane by `degrees`, carrying a
// toward b: a' = a cos - b sin, b' = a sin + b cos. With (right, forward) a
// positive angle turns the view right; this is the measured anchor every sign
// below is expressed against.
void RotatePair(float a[3], float b[3], float degrees) {
    const float radians = degrees * kDegreesToRadians;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    for (int k = 0; k < 3; ++k) {
        const float av = a[k];
        const float bv = b[k];
        a[k] = av * c - bv * s;
        b[k] = av * s + bv * c;
    }
}

// ScaleAngleForZoom is atan(tan(a) * factor), which is only single-valued
// inside +/-90 degrees: a degree past it tan changes sign and the scaled angle
// comes back just short of -90, swinging the view to the opposite side of the
// truck. A driver looking over their shoulder reaches that, and a tracker is
// free to send it. Past the handover the angle is passed through instead - a
// head that far round is looking out of the frame, where there is no screen
// displacement left to hold constant, and the round trip already returns almost
// the angle it was given there (89 degrees comes back as 89.1 at the widest
// factor this game produces), so the seam costs a fraction of a degree.
constexpr float kMaxScaledAngleDegrees = 89.0f;

float ScaleAngleInRange(float degrees, float factor) {
    if (degrees > kMaxScaledAngleDegrees || degrees < -kMaxScaledAngleDegrees) return degrees;
    return cameraunlock::camera::ScaleAngleForZoom(degrees, factor);
}

}  // namespace

void ApplyHeadPose(CameraBasis& basis, const HeadPose& pose) {
    const CameraBasis clean = basis;

    // Yaw goes in unnegated and the lateral lean is subtracted along right: the
    // first build had each the other way round, and turning and leaning a real
    // head showed both going the wrong way.
    RotatePair(basis.right, basis.forward, pose.yaw);
    RotatePair(basis.up, basis.forward, pose.pitch);
    RotatePair(basis.right, basis.up, -pose.roll);

    for (int k = 0; k < 3; ++k) {
        basis.eye[k] = clean.eye[k]
                     - clean.right[k] * pose.lean_x
                     + clean.up[k] * pose.lean_y
                     - clean.forward[k] * pose.lean_z;
    }
}

HeadPose ScalePoseForZoom(const HeadPose& pose, float factor) {
    // Exactly 1.0 whenever the camera is at the field of view the player set,
    // which is where it sits for most of a session. Handing the pose straight
    // back is both cheaper than the tangent round trip below and exact, where
    // atan(tan(a)) is only exact to within its own rounding.
    if (factor == 1.0f) return pose;

    HeadPose scaled = pose;
    scaled.yaw = ScaleAngleInRange(pose.yaw, factor);
    scaled.pitch = ScaleAngleInRange(pose.pitch, factor);
    scaled.lean_x = pose.lean_x * factor;
    scaled.lean_y = pose.lean_y * factor;
    scaled.lean_z = pose.lean_z * factor;
    return scaled;
}

HeadPose ClampLean(const HeadPose& pose, const LeanLimits& limits) {
    const auto clamp = [](float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    };
    HeadPose clamped = pose;
    clamped.lean_x = clamp(pose.lean_x, -limits.x, limits.x);
    clamped.lean_y = clamp(pose.lean_y, -limits.y_down, limits.y_up);
    clamped.lean_z = clamp(pose.lean_z, -limits.z_forward, limits.z_back);
    return clamped;
}

HeadPose BlendPose(const HeadPose& pose, float weight) {
    HeadPose blended;
    blended.yaw = pose.yaw * weight;
    blended.pitch = pose.pitch * weight;
    blended.roll = pose.roll * weight;
    blended.lean_x = pose.lean_x * weight;
    blended.lean_y = pose.lean_y * weight;
    blended.lean_z = pose.lean_z * weight;
    return blended;
}

}  // namespace rc_ht
