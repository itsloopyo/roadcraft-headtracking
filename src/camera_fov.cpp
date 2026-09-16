// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_fov.h"

#include <cmath>
#include <cstring>

#include "cameraunlock/camera/zoom_compensation.h"

namespace rc_ht {

namespace {

constexpr float kDegreesToRadians = 0.01745329252f;

// The bounds Camera::SetHorizontalFov itself tests its argument against before
// it will store it, read out of the shipped binary. Anything outside them is a
// camera the engine would have rejected, so the mod reads it as no usable field
// of view rather than as a zoom.
constexpr float kMinFovDegrees = 1.0f;
constexpr float kMaxFovDegrees = 141.0f;

// The field of view each camera rests at, which is what the player set in
// Settings > Camera. RoadCraft never stores that number anywhere the mod can
// read: the game's script layer computes it from the slider and writes only the
// result, already carrying whatever the Dynamic FOV setting has added, into the
// camera every frame. What the engine does guarantee is the direction - the
// third-person camera renders exactly the configured angle with the zoom out or
// with Dynamic FOV off, and Dynamic FOV only ever WIDENS it from there, by up
// to 6.25 degrees at full zoom-in (measured at two slider settings: 59 renders
// 73.75 and reaches 80.00, 55 renders 68.75 and reaches 75.00). So the
// narrowest angle a camera has been seen rendering is the configured one.
//
// That holds only for as long as the slider does not move under it. Raising it
// mid-session leaves the old, narrower angle standing as the base, and every
// head movement is over-scaled against it for the rest of the level. What
// separates the two cases is how far above the base the reading sits: the zoom
// adds a fixed 6.25 degrees and no more, because the game renders 1.25 degrees
// per slider unit and Dynamic FOV adds five of them (59 renders 73.75 and
// reaches 80.00, 55 renders 68.75 and reaches 75.00). Further above than that is
// not a zoom this game can produce, so it is the slider, and the base follows it
// up. A raise too small to clear the window is left to the zoom to settle: the
// next zoom-in pushes the total past the window, the base jumps to the zoomed
// angle, and the next zoom-out narrows it onto the new configured one.
//
// Doing it here rather than off the gameplay gate is deliberate. Clearing the
// bases when the menu closes looks equivalent and is worse: the reseed happens
// on whatever the camera is rendering at that instant, so unpausing while the
// wheel zoom is held in seeds the base at the ZOOMED angle and under-scales
// every head movement until the player happens to zoom out again - on every
// pause, map and photo-mode exit, rather than once per slider change.
constexpr float kMaxZoomWideningDegrees = 7.5f;

// A slot per camera the component system drives, not per camera the mod
// composes into: the field of view is read before the gate is consulted, so the
// loading screen's camera takes a slot as readily as the chase and cockpit ones,
// and the map and photo mode bring their own. Eight covers those five with room
// spare, because the eviction is round-robin - the oldest INSERTION goes, which
// is systematically the camera the player has been driving all level.
struct CameraBase {
    const void* camera;
    float horizontal_deg;
};

constexpr int kBaseSlots = 8;
CameraBase g_bases[kBaseSlots];
int g_next_slot = 0;

FovOffsets g_offsets{};
bool g_base_widened = false;

float TanHalf(float degrees) { return std::tan(degrees * 0.5f * kDegreesToRadians); }

float ReadFloat(const void* object, unsigned offset) {
    float value = 0.0f;
    std::memcpy(&value, static_cast<const char*>(object) + offset, sizeof(value));
    return value;
}

// The narrowest this camera has been seen at, seeding a slot on first sight.
// Narrows on any narrower reading, and widens only on one too far above the base
// for the zoom to account for, which is the player having moved the slider.
float BaseFor(const void* camera, float horizontal_deg) {
    for (int i = 0; i < kBaseSlots; ++i) {
        if (g_bases[i].camera != camera) continue;
        const float above = horizontal_deg - g_bases[i].horizontal_deg;
        if (above > kMaxZoomWideningDegrees) g_base_widened = true;
        if (above < 0.0f || above > kMaxZoomWideningDegrees) {
            g_bases[i].horizontal_deg = horizontal_deg;
        }
        return g_bases[i].horizontal_deg;
    }
    g_bases[g_next_slot].camera = camera;
    g_bases[g_next_slot].horizontal_deg = horizontal_deg;
    g_next_slot = (g_next_slot + 1) % kBaseSlots;
    return horizontal_deg;
}

}  // namespace

void InitFovTracking(const FovOffsets& offsets) {
    g_offsets = offsets;
    ResetFovTracking();
}

void ResetFovTracking() {
    for (int i = 0; i < kBaseSlots; ++i) g_bases[i] = CameraBase{nullptr, 0.0f};
    g_next_slot = 0;
}

bool ReadFov(const void* camera, FovReading& out) {
    const float horizontal = ReadFloat(camera, g_offsets.horizontal);
    const float vertical = ReadFloat(camera, g_offsets.vertical);
    const bool usable = horizontal > kMinFovDegrees && horizontal < kMaxFovDegrees
                     && vertical > kMinFovDegrees && vertical < kMaxFovDegrees;
    if (!usable) return false;

    out.horizontal_deg = horizontal;
    out.vertical_deg = vertical;
    g_base_widened = false;
    out.base_horizontal_deg = BaseFor(camera, horizontal);
    // Both tangents are of the horizontal angle, off the same field of the same
    // camera, so there is no axis to mismatch and the factor is 1.0 exactly
    // whenever the camera is at its configured angle.
    const float tan_half_horizontal = TanHalf(out.horizontal_deg);
    out.zoom_factor = cameraunlock::camera::FovZoomFactor(tan_half_horizontal,
                                                          TanHalf(out.base_horizontal_deg));
    out.aspect = tan_half_horizontal / TanHalf(out.vertical_deg);
    return true;
}

bool BaseWidenedOnLastRead() { return g_base_widened; }

}  // namespace rc_ht
