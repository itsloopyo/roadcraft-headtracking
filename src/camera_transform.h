// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace rc_ht {

// Tracker pose as the core pipeline hands it over: degrees, and metres with x
// right, y up and NEGATIVE z forward.
struct HeadPose {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float lean_x = 0.0f;
    float lean_y = 0.0f;
    float lean_z = 0.0f;
};

// The camera as Camera::SetTransform receives it: world-space unit rows and the
// eye. The engine's basis has right x up = -forward, measured in game.
struct CameraBasis {
    float right[3];
    float up[3];
    float forward[3];
    float eye[3];
};

// Turns the basis by the head's rotation, camera-local (yaw about the camera's
// up, then pitch about the turned right, then roll about the turned forward),
// and moves the eye along the CLEAN axes so the lean follows the vehicle rather
// than where the head is pointing. Rotation never moves the eye.
//
// Signs at this boundary, as the tests pin them: +yaw turns the view right,
// +pitch looks up, +roll tilts the view left, +y moves the eye up. The tracker's
// lateral and depth axes arrive mirrored against the engine's right and forward
// rows, so +x moves the eye left and -z moves it forward.
void ApplyHeadPose(CameraBasis& basis, const HeadPose& pose);

// The pose scaled toward identity by `weight` in [0,1], for easing tracking in
// and out when the gameplay gate opens and closes.
HeadPose BlendPose(const HeadPose& pose, float weight);

// The pose rescaled so it displaces the picture by as much as it would have at
// the camera's configured field of view. `factor` is tan(live/2) / tan(base/2).
// Yaw, pitch and the lean all translate the image, so they all scale; roll
// turns the image about the view axis by the same angle at every field of view,
// so it is left alone. A yaw or pitch past 89 degrees is passed through rather
// than scaled, because the tangent round trip the scaling is built on changes
// sign at 90 and would throw the view to the other side.
HeadPose ScalePoseForZoom(const HeadPose& pose, float factor);

// How far the eye may travel from where the game put it, in metres. Mirrors the
// core processor's own bounds: asymmetric on y and z, symmetric on x.
struct LeanLimits {
    float x;
    float y_up;
    float y_down;
    float z_forward;
    float z_back;
};

// The lean cut to `limits`. The processor clamps as its last step, but the zoom
// scaling above then multiplies what it handed back, so a lean at the limit
// leaves the pipeline past it. This is what makes the configured limit the one
// the camera actually gets.
HeadPose ClampLean(const HeadPose& pose, const LeanLimits& limits);

}  // namespace rc_ht
