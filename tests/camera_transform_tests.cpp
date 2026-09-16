// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The composition the camera hook writes into Camera::SetTransform, compiled
// from the production translation unit. The fixture basis is the one read off the
// running game with the truck parked: right x up = -forward.

#include "camera_transform.h"

#include "test_support.h"

#include <cmath>
#include <cstdio>

using namespace rc_ht;
using rc_test::Check;
using rc_test::CheckClose;

namespace {

constexpr float kDeg = 0.01745329252f;

float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void Cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

// The chase camera as measured: looking down 25 degrees, heading off-axis.
CameraBasis MeasuredBasis() {
    return CameraBasis{
        { -0.24285f, 0.0f, -0.97006f },
        { -0.40899f, 0.90677f, 0.10239f },
        { -0.87963f, -0.42162f, 0.22021f },
        { 898.74786f, 74.34801f, -394.83212f },
    };
}

// A level camera looking down +z, left-handed as the game has it:
// right x up = -forward.
CameraBasis LevelBasis() {
    return CameraBasis{
        { -1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f },
        { 10.0f, 2.0f, -5.0f },
    };
}

void CheckOrthonormal(const CameraBasis& b, const char* what) {
    const bool unit = std::fabs(Dot(b.right, b.right) - 1.0f) < 1e-4f
                   && std::fabs(Dot(b.up, b.up) - 1.0f) < 1e-4f
                   && std::fabs(Dot(b.forward, b.forward) - 1.0f) < 1e-4f;
    const bool orthogonal = std::fabs(Dot(b.right, b.up)) < 1e-4f
                         && std::fabs(Dot(b.right, b.forward)) < 1e-4f
                         && std::fabs(Dot(b.up, b.forward)) < 1e-4f;
    float cross[3];
    Cross(b.right, b.up, cross);
    const bool handed = Dot(cross, b.forward) < -0.999f;
    Check(unit && orthogonal && handed, what);
}

// Angle the forward row turned towards `axis` of the clean basis, in degrees.
float TurnedTowards(const CameraBasis& clean, const CameraBasis& tracked, const float axis[3]) {
    return std::asin(Dot(tracked.forward, axis)) / kDeg - std::asin(Dot(clean.forward, axis)) / kDeg;
}

void SignTests() {
    std::printf("Signs at the engine boundary\n");
    const CameraBasis clean = LevelBasis();

    HeadPose yaw;
    yaw.yaw = 30.0f;
    CameraBasis turned = clean;
    ApplyHeadPose(turned, yaw);
    CheckClose(TurnedTowards(clean, turned, clean.right), 30.0f,
               "+yaw turns the forward row 30 degrees toward right, so the view turns right");
    CheckOrthonormal(turned, "yaw keeps the basis orthonormal and left-handed as the game has it");

    HeadPose pitch;
    pitch.pitch = 20.0f;
    CameraBasis pitched = clean;
    ApplyHeadPose(pitched, pitch);
    CheckClose(TurnedTowards(clean, pitched, clean.up), 20.0f, "+pitch looks up by 20 degrees");

    HeadPose roll;
    roll.roll = 15.0f;
    CameraBasis rolled = clean;
    ApplyHeadPose(rolled, roll);
    CheckClose(std::asin(Dot(rolled.up, clean.right)) / kDeg, -15.0f,
               "+roll tilts the camera's up toward its left: the view tilts left");
    CheckClose(Dot(rolled.forward, clean.forward), 1.0f, "roll leaves the forward row alone");

    HeadPose lean;
    lean.lean_x = 0.10f;
    lean.lean_y = 0.05f;
    lean.lean_z = -0.20f;
    CameraBasis leaned = clean;
    ApplyHeadPose(leaned, lean);
    const float moved[3] = { leaned.eye[0] - clean.eye[0], leaned.eye[1] - clean.eye[1],
                             leaned.eye[2] - clean.eye[2] };
    CheckClose(Dot(moved, clean.right), -0.10f, "+x moves the eye along -right");
    CheckClose(Dot(moved, clean.up), 0.05f, "+y moves the eye up");
    CheckClose(Dot(moved, clean.forward), 0.20f, "-z moves the eye forward");
    CheckClose(Dot(leaned.forward, clean.forward), 1.0f, "a lean never rotates the view");
}

void CompositionTests() {
    std::printf("Composition on the measured chase camera\n");
    const CameraBasis clean = MeasuredBasis();

    HeadPose rotation_only;
    rotation_only.yaw = 75.0f;
    rotation_only.pitch = 30.0f;
    rotation_only.roll = 25.0f;
    CameraBasis rotated = clean;
    ApplyHeadPose(rotated, rotation_only);
    CheckOrthonormal(rotated, "a combined pose keeps the basis orthonormal");
    Check(rotated.eye[0] == clean.eye[0] && rotated.eye[1] == clean.eye[1]
              && rotated.eye[2] == clean.eye[2],
          "rotation never moves the eye");

    // Yaw first about the camera's own up, then pitch about the turned right: the
    // pitched forward stays in the plane of turned-right's normal.
    HeadPose yaw_pitch;
    yaw_pitch.yaw = 40.0f;
    yaw_pitch.pitch = 20.0f;
    CameraBasis composed = clean;
    ApplyHeadPose(composed, yaw_pitch);
    HeadPose yaw_only;
    yaw_only.yaw = 40.0f;
    CameraBasis yawed = clean;
    ApplyHeadPose(yawed, yaw_only);
    CheckClose(Dot(composed.right, yawed.right), 1.0f,
               "pitch turns about the right row the yaw produced");
    CheckClose(Dot(composed.forward, clean.up) - Dot(yawed.forward, clean.up),
               std::sin(20.0f * kDeg) * Dot(yawed.up, clean.up)
                   - (1.0f - std::cos(20.0f * kDeg)) * Dot(yawed.forward, clean.up),
               "and lifts the yawed forward toward the yawed up");

    // The lean uses the clean axes whatever the head is doing.
    HeadPose both = rotation_only;
    both.lean_x = 0.3f;
    CameraBasis leaned = clean;
    ApplyHeadPose(leaned, both);
    const float moved[3] = { leaned.eye[0] - clean.eye[0], leaned.eye[1] - clean.eye[1],
                             leaned.eye[2] - clean.eye[2] };
    // Eye coordinates near 900 leave a subtraction about 1e-4 of float precision.
    Check(std::fabs(Dot(moved, clean.right) + 0.3f) < 1e-3f,
          "a lean under a turned head follows the clean right");
    Check(std::fabs(Dot(moved, clean.forward)) < 1e-3f, "and does not leak into forward");
}

void BlendTests() {
    std::printf("Gate blending\n");
    HeadPose pose;
    pose.yaw = 20.0f;
    pose.pitch = -10.0f;
    pose.roll = 12.0f;
    pose.lean_x = 0.2f;
    pose.lean_y = 0.1f;
    pose.lean_z = -0.3f;

    const HeadPose half = BlendPose(pose, 0.5f);
    CheckClose(half.yaw, 10.0f, "blend scales yaw");
    CheckClose(half.lean_z, -0.15f, "blend scales the lean");
    const HeadPose none = BlendPose(pose, 0.0f);
    Check(none.yaw == 0.0f && none.roll == 0.0f && none.lean_x == 0.0f,
          "weight 0 is identity");
}

// Every bound a different number, in the order LeanLimits names them. The
// shipped defaults happen to set LimitY and LimitYDown to the same 0.20, and a
// fixture that copied them would pass just as happily against a clamp that used
// the up limit on both sides of y - which is the one field AGENTS.md calls out
// as only LOOKING symmetric.
constexpr LeanLimits kDefaultLimits{ 0.30f, 0.20f, 0.12f, 0.40f, 0.10f };

void LeanClampTests() {
    std::printf("\nthe lean is cut to the configured limits\n");

    HeadPose inside;
    inside.lean_x = 0.10f;
    inside.lean_y = -0.05f;
    inside.lean_z = -0.25f;
    const HeadPose kept = ClampLean(inside, kDefaultLimits);
    CheckClose(kept.lean_x, 0.10f, "a lean inside the limits is untouched");
    CheckClose(kept.lean_y, -0.05f, "on every axis");
    CheckClose(kept.lean_z, -0.25f, "including the generous forward one");

    // The bug this pins: the processor clamps to the limits, then the zoom
    // scaling multiplies what it handed back, so a lean that was exactly at the
    // limit leaves the pipeline past it. 1.1186 is the widest factor this game
    // was measured producing.
    HeadPose at_limit;
    at_limit.lean_x = kDefaultLimits.x;
    at_limit.lean_z = -kDefaultLimits.z_forward;
    const HeadPose scaled = ScalePoseForZoom(at_limit, 1.1186f);
    Check(scaled.lean_x > kDefaultLimits.x, "scaling for zoom pushes a clamped lean past the limit");

    const HeadPose clamped = ClampLean(scaled, kDefaultLimits);
    CheckClose(clamped.lean_x, kDefaultLimits.x, "the re-clamp puts it back on the limit");
    CheckClose(clamped.lean_z, -kDefaultLimits.z_forward, "and on the forward limit too");

    // Asymmetric on y and on z, mirroring the core processor's own bounds.
    HeadPose past;
    past.lean_y = 5.0f;
    past.lean_z = 5.0f;
    const HeadPose cut = ClampLean(past, kDefaultLimits);
    CheckClose(cut.lean_y, 0.20f, "up is bounded by LimitY");
    CheckClose(cut.lean_z, 0.10f, "leaning back is bounded by the tighter LimitZBack");

    HeadPose past_other_way;
    past_other_way.lean_y = -5.0f;
    past_other_way.lean_z = -5.0f;
    const HeadPose cut_other = ClampLean(past_other_way, kDefaultLimits);
    CheckClose(cut_other.lean_y, -0.12f, "down is bounded by the separate LimitYDown");
    CheckClose(cut_other.lean_z, -0.40f, "and leaning forward by the generous LimitZ");
}

}  // namespace

int main() {
    std::printf("RoadCraft head tracking - camera transform tests\n");
    std::printf("================================================\n");
    SignTests();
    CompositionTests();
    BlendTests();
    LeanClampTests();
    return rc_test::Summary("camera transform");
}
