// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The field-of-view read and the zoom factor derived from it, against the
// numbers measured in the running game: the chase camera renders 73.75 degrees
// horizontal / 45.755 vertical at 16:9 with the Third-Person camera FOV slider
// at its default 59, and the game's Dynamic FOV setting widens that to 80.00 at
// full zoom-in. The cockpit renders 100.00 / 67.673 with First-Person at 80 and
// does not move at all.

#include "camera_fov.h"

#include "camera_transform.h"

#include "test_support.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace rc_ht;
using rc_test::Check;
using rc_test::CheckClose;

namespace {

// The render camera as the hook receives it: only the two field-of-view fields
// matter here, at the offsets the Steam build profile carries.
constexpr unsigned kHorizontalOffset = 0x2D0;
constexpr unsigned kVerticalOffset = 0x2D4;

constexpr float kDegToRad = 0.01745329252f;
// Degrees straight to the radians of the HALF angle, which is what every
// tangent below is of.
constexpr float kHalfDegToRad = kDegToRad * 0.5f;

constexpr float kDisplayAspect = 16.0f / 9.0f;

struct FakeCamera {
    char bytes[0x400];

    FakeCamera(float horizontal_deg, float vertical_deg) {
        std::memset(bytes, 0, sizeof(bytes));
        SetFov(horizontal_deg, vertical_deg);
    }

    void SetFov(float horizontal_deg, float vertical_deg) {
        std::memcpy(bytes + kHorizontalOffset, &horizontal_deg, sizeof(float));
        std::memcpy(bytes + kVerticalOffset, &vertical_deg, sizeof(float));
    }
};

void Init() {
    InitFovTracking({kHorizontalOffset, kVerticalOffset});
}

void TestReadsBothAngles() {
    std::printf("\nreads the camera's own angles\n");
    Init();
    FakeCamera chase(73.75f, 45.75494f);

    FovReading fov;
    if (!Check(ReadFov(&chase, fov), "a camera at 73.75 degrees reads")) return;
    CheckClose(fov.horizontal_deg, 73.75f, "horizontal angle is the first field");
    CheckClose(fov.vertical_deg, 45.75494f, "vertical angle is the second field");

    // tan(h/2) / tan(v/2) is the display aspect the engine's own projection
    // divides x by. 1920x1080 is what the two measured angles came off, and
    // the reading carries it so the log line can be checked against the
    // display without recomputing it at the call site.
    const float aspect = std::tan(73.75f * kHalfDegToRad) / std::tan(45.75494f * kHalfDegToRad);
    Check(std::fabs(aspect - kDisplayAspect) < 1e-3f, "the two angles imply a 16:9 aspect");
    Check(std::fabs(fov.aspect - kDisplayAspect) < 1e-3f,
          "and the reading reports that aspect");
}

void TestRestingCameraIsUnscaled() {
    std::printf("\nan un-zoomed camera scales the pose by exactly 1\n");
    Init();
    FakeCamera chase(73.75f, 45.75494f);
    FovReading fov;
    ReadFov(&chase, fov);
    CheckClose(fov.base_horizontal_deg, 73.75f, "first sight seeds the base");
    CheckClose(fov.zoom_factor, 1.0f, "zoom factor is 1.0000 in ordinary gameplay");

    HeadPose pose;
    pose.yaw = 20.0f;
    pose.pitch = -12.0f;
    pose.roll = 8.0f;
    pose.lean_x = 0.15f;
    pose.lean_y = -0.05f;
    pose.lean_z = -0.30f;
    const HeadPose scaled = ScalePoseForZoom(pose, fov.zoom_factor);
    CheckClose(scaled.yaw, pose.yaw, "yaw untouched at factor 1");
    CheckClose(scaled.pitch, pose.pitch, "pitch untouched at factor 1");
    CheckClose(scaled.lean_x, pose.lean_x, "lean x untouched at factor 1");
    CheckClose(scaled.lean_z, pose.lean_z, "lean z untouched at factor 1");
}

void TestDynamicFovWidensAndScalesUp() {
    std::printf("\nthe game's Dynamic FOV zoom scales the pose up\n");
    Init();
    FakeCamera chase(73.75f, 45.75494f);
    FovReading fov;
    ReadFov(&chase, fov);

    // Full zoom-in with Dynamic FOV on, measured: 80.00 horizontal, 50.534
    // vertical, from the same camera object.
    chase.SetFov(80.0f, 50.53401f);
    if (!Check(ReadFov(&chase, fov), "the widened camera reads")) return;
    CheckClose(fov.base_horizontal_deg, 73.75f, "the base stays at the configured angle");

    const float expected = std::tan(40.0f * kDegToRad) / std::tan(36.875f * kDegToRad);
    CheckClose(fov.zoom_factor, expected, "zoom factor is the ratio of the half-angle tangents");
    Check(fov.zoom_factor > 1.0f, "a wider frame needs MORE engine rotation per head degree");

    HeadPose pose;
    pose.yaw = 20.0f;
    pose.roll = 8.0f;
    pose.lean_x = 0.20f;
    const HeadPose scaled = ScalePoseForZoom(pose, fov.zoom_factor);
    Check(scaled.yaw > pose.yaw, "yaw grows with the wider frame");
    CheckClose(scaled.roll, pose.roll, "roll never scales");
    CheckClose(scaled.lean_x, pose.lean_x * fov.zoom_factor, "lean scales linearly");

    // The whole point: the head's share of the screen is unchanged. A yaw of
    // `a` lands at tan(a)/tan(fov/2) of the half frame.
    const float at_base = std::tan(pose.yaw * kDegToRad) / std::tan(36.875f * kDegToRad);
    const float at_zoom = std::tan(scaled.yaw * kDegToRad) / std::tan(40.0f * kDegToRad);
    CheckClose(at_zoom, at_base, "the turned view covers the same fraction of the frame");
}

// A driver looking over their shoulder sends a yaw past 90, and so can any host
// on the network. atan(tan(a) * factor) changes sign there: 91 degrees came back
// as -89.1, which threw the view to the other side of the truck for as long as
// the head stayed round. Only reachable while the game is off its base field of
// view, so it hid behind the factor == 1.0 shortcut in ordinary driving.
void TestAnglesPastTheWrapAreNotFlipped() {
    std::printf("\na head turned past 90 degrees keeps its side of the truck\n");
    Init();
    FakeCamera chase(73.75f, 45.75494f);
    FovReading fov;
    ReadFov(&chase, fov);
    chase.SetFov(80.0f, 50.53401f);
    if (!Check(ReadFov(&chase, fov), "the widened camera reads")) return;
    if (!Check(fov.zoom_factor != 1.0f, "and is off its base, so the scaling runs")) return;

    HeadPose over;
    over.yaw = 110.0f;
    over.pitch = 95.0f;
    const HeadPose scaled = ScalePoseForZoom(over, fov.zoom_factor);
    Check(scaled.yaw > 0.0f, "a yaw of 110 stays positive instead of wrapping to -70");
    Check(scaled.pitch > 0.0f, "and a pitch of 95 stays positive");
    CheckClose(scaled.yaw, 110.0f, "the yaw is passed through unscaled");
    CheckClose(scaled.pitch, 95.0f, "and so is the pitch");

    HeadPose behind;
    behind.yaw = -135.0f;
    Check(ScalePoseForZoom(behind, fov.zoom_factor).yaw < 0.0f,
          "the same holds turning the other way");

    // Just inside the handover the round trip still runs, and still lands within
    // a fraction of a degree of the pass-through above it.
    HeadPose edge;
    edge.yaw = 88.0f;
    const float edge_yaw = ScalePoseForZoom(edge, fov.zoom_factor).yaw;
    Check(edge_yaw > 88.0f && edge_yaw < 89.5f,
          "88 degrees is still scaled, and the seam at 89 is under a degree wide");
}

void TestTwoCamerasKeepSeparateBases() {
    std::printf("\nswitching cameras does not re-seed the other's base\n");
    Init();
    FakeCamera chase(73.75f, 45.75494f);
    FakeCamera cockpit(100.0f, 67.67274f);

    FovReading fov;
    ReadFov(&chase, fov);
    ReadFov(&cockpit, fov);
    CheckClose(fov.base_horizontal_deg, 100.0f, "the cockpit keeps its own base");
    CheckClose(fov.zoom_factor, 1.0f, "the cockpit is unscaled at its own angle");

    chase.SetFov(80.0f, 50.53401f);
    ReadFov(&chase, fov);
    CheckClose(fov.base_horizontal_deg, 73.75f, "the chase base survived the switch away");

    ReadFov(&cockpit, fov);
    CheckClose(fov.base_horizontal_deg, 100.0f, "the cockpit base survived the switch back");
}

void TestBaseFollowsTheConfiguredAngleDown() {
    std::printf("\nlowering the slider lowers the base\n");
    Init();
    FakeCamera chase(73.75f, 45.75494f);
    FovReading fov;
    ReadFov(&chase, fov);

    // Third-Person camera FOV moved from 59 to 55, measured as 68.75 degrees.
    chase.SetFov(68.75f, 42.09269f);
    ReadFov(&chase, fov);
    CheckClose(fov.base_horizontal_deg, 68.75f, "the narrower resting angle becomes the base");
    CheckClose(fov.zoom_factor, 1.0f, "and the factor is back to 1.0000");
}

// The bug this pins: a base that can only ever narrow leaves the old angle
// standing when the slider goes up, over-scaling every head movement for the
// rest of the level. A reading further above the base than the zoom can reach is
// the slider, and the base follows it up.
void TestRaisingTheSliderMovesTheBase() {
    std::printf("\na slider raise moves the base, a zoom does not\n");
    Init();
    FakeCamera chase(73.75f, 45.75494f);
    FovReading fov;
    ReadFov(&chase, fov);

    // Full Dynamic FOV zoom-in, measured: 80.00, which is 6.25 above the base.
    // Inside what the zoom can reach, so the base must NOT move.
    chase.SetFov(80.0f, 50.53401f);
    ReadFov(&chase, fov);
    CheckClose(fov.base_horizontal_deg, 73.75f, "the zoom leaves the base where it is");
    Check(!BaseWidenedOnLastRead(), "and is not reported as a slider change");

    // Third-Person camera FOV moved from 59 up to 80. Both measured settings
    // render 1.25 degrees per slider unit, which puts 80 at 100.00. Further
    // above the base than the zoom can reach, so it reads as the slider.
    chase.SetFov(100.0f, 67.67295f);
    ReadFov(&chase, fov);
    CheckClose(fov.base_horizontal_deg, 100.0f, "a raise past the zoom's reach moves the base");
    Check(BaseWidenedOnLastRead(), "and says so, once");
    CheckClose(fov.zoom_factor, 1.0f, "so the resting camera is unscaled rather than over-scaled");

    ReadFov(&chase, fov);
    Check(!BaseWidenedOnLastRead(), "the next reading at the same angle is not a fresh change");
}

void TestLevelLoadForgetsBases() {
    std::printf("\na level load forgets what the cameras rested at\n");
    Init();
    FakeCamera chase(73.75f, 45.75494f);
    FovReading fov;
    ReadFov(&chase, fov);

    ResetFovTracking();
    chase.SetFov(80.0f, 50.53401f);
    ReadFov(&chase, fov);
    CheckClose(fov.base_horizontal_deg, 80.0f, "the first angle after a load seeds a fresh base");
}

void TestUnusableFovIsRefused() {
    std::printf("\na camera the engine would reject is not compensated\n");
    Init();
    FovReading fov;

    FakeCamera zero(0.0f, 0.0f);
    Check(!ReadFov(&zero, fov), "a zero field of view is refused");

    FakeCamera absurd(200.0f, 170.0f);
    Check(!ReadFov(&absurd, fov), "past the engine's own 141 degree ceiling is refused");

    FakeCamera edge(140.0f, 120.0f);
    Check(ReadFov(&edge, fov), "inside the engine's range is accepted");

    // The two fields are read straight out of game memory at a pinned offset, so
    // an uninitialised camera can hand back anything a float can hold. Every
    // comparison against a NaN is false, which is what makes the range test
    // refuse it rather than pass it through as a zoom.
    const float nan_value = std::nanf("");
    FakeCamera not_a_number(nan_value, nan_value);
    Check(!ReadFov(&not_a_number, fov), "a NaN field of view is refused");

    FakeCamera negative(-73.75f, -45.75f);
    Check(!ReadFov(&negative, fov), "a negative field of view is refused");
}

}  // namespace

int main() {
    std::printf("=== camera field of view ===\n");
    TestReadsBothAngles();
    TestRestingCameraIsUnscaled();
    TestDynamicFovWidensAndScalesUp();
    TestAnglesPastTheWrapAreNotFlipped();
    TestTwoCamerasKeepSeparateBases();
    TestBaseFollowsTheConfiguredAngleDown();
    TestRaisingTheSliderMovesTheBase();
    TestLevelLoadForgetsBases();
    TestUnusableFovIsRefused();
    return rc_test::Summary("camera field of view");
}
