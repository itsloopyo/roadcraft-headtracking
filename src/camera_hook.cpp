// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_hook.h"

#include <windows.h>
#include <intrin.h>

#include <cstdint>
#include <cmath>
#include <cstring>
#include <mutex>

#include "builds/build_registry.h"
#include "camera_fov.h"
#include "camera_transform.h"
#include "headtracking_mod.h"
#include "logging.h"
#include "game_memory.h"

#include "cameraunlock/hooks/hook_manager.h"

namespace rc_ht {

namespace {

using SetTransformFn = void(__fastcall*)(void* camera, const float* position, const float* up,
                                         const float* right, const float* forward);

SetTransformFn g_original_set_transform = nullptr;
std::uintptr_t g_return_a = 0;
std::uintptr_t g_return_b = 0;

// The camera the component system last drove, so a change of view (chase to
// cockpit) shows up in the log once rather than never. Plain, like the other
// logging state below: everything in this file is reached only from
// ComposeBasis, which holds CameraPathMutex() for its whole body.
void* g_last_camera = nullptr;

// Every term of the zoom factor, on the first frame each camera updates rather
// than the first frame a pose arrives, so the numbers are checkable without a
// tracker connected and without loading a save. Both angles fed to the factor
// are the camera's own horizontal field of view, and the aspect printed beside
// them is derived from the vertical one the engine stores next to it - if that
// does not come out as the display's aspect, the two fields are not what this
// mod reads them as. The gate is that the factor is 1.0000 in ordinary play.
void LogCameraChange(void* camera, const FovReading& fov) {
    if (g_last_camera == camera) return;
    g_last_camera = camera;
    Log::Line("[camera] now driving camera 0x%p: field of view %.4f deg horizontal, "
              "%.4f deg vertical (aspect %.4f); base %.4f deg horizontal; zoom factor %.4f",
              camera, fov.horizontal_deg, fov.vertical_deg, fov.aspect,
              fov.base_horizontal_deg, fov.zoom_factor);
}

// The zoom, on the way out and on the way back, plus a sample every couple of
// seconds while it is held. Transitions alone cannot tell a compensation that is
// engaging from one that has quietly stopped, and this is the only line that
// shows the factor following a field of view the game is animating.
void LogZoomExcursion(const FovReading& fov) {
    constexpr float kOffBaseDegrees = 0.25f;
    constexpr unsigned long long kSampleIntervalMs = 2000;
    static bool off_base = false;
    static unsigned long long last_sample_ms = 0;

    const bool now_off_base = fov.horizontal_deg - fov.base_horizontal_deg > kOffBaseDegrees;
    // At the base and it was last frame too: ordinary play, nothing to say, and
    // nothing here worth reading a clock for.
    if (!now_off_base && !off_base) return;

    const unsigned long long now_ms = GetTickCount64();
    if (now_off_base == off_base && now_ms - last_sample_ms < kSampleIntervalMs) return;

    off_base = now_off_base;
    last_sample_ms = now_ms;
    Log::Line("[camera] %s %.2f degrees across the frame against a base of %.2f - "
              "scaling the head pose by %.4f",
              now_off_base ? "zoomed to" : "back to", fov.horizontal_deg,
              fov.base_horizontal_deg, fov.zoom_factor);
}

void LogUnreadableFov(void* camera) {
    static ULONGLONG last = 0;
    const auto now = GetTickCount64();
    if (last && now - last < 2000) return;
    last = now;
    Log::Line("[camera] camera 0x%p failed live object/FOV validation; head tracking is held off", camera);
}

void LogComposedFrame(const CameraBasis& clean, const CameraBasis& tracked,
                           const HeadPose& pose) {
    static ULONGLONG last = 0;
    const auto now = GetTickCount64();
    if (last && now - last < 1000) return;
    last = now;
    Log::Line("[camera] composed frame: pose yaw=%.2f pitch=%.2f roll=%.2f lean=%.3f %.3f "
              "%.3f; forward (%.3f %.3f %.3f) -> (%.3f %.3f %.3f); eye moved %.3f %.3f %.3f",
              pose.yaw, pose.pitch, pose.roll, pose.lean_x, pose.lean_y, pose.lean_z,
              clean.forward[0], clean.forward[1], clean.forward[2],
              tracked.forward[0], tracked.forward[1], tracked.forward[2],
              tracked.eye[0] - clean.eye[0], tracked.eye[1] - clean.eye[1],
              tracked.eye[2] - clean.eye[2]);
}

CameraBasis BasisFromArguments(const float* position, const float* up, const float* right,
                               const float* forward) {
    CameraBasis basis;
    std::memcpy(basis.right, right, sizeof(basis.right));
    std::memcpy(basis.up, up, sizeof(basis.up));
    std::memcpy(basis.forward, forward, sizeof(basis.forward));
    std::memcpy(basis.eye, position, sizeof(basis.eye));
    return basis;
}

// Everything that reads or advances shared state, under CameraPathMutex(). The
// original is called by the caller once the lock is back off: it is the engine's
// own function, and holding a non-recursive lock across a call back into the
// engine is how a hook deadlocks itself.
bool ComposeBasis(void* camera, const float* position, const float* up, const float* right,
                  const float* forward, CameraBasis& composed) {
    const std::lock_guard<std::mutex> guard(CameraPathMutex());

    const auto& discovery = builds::ActiveDiscovery();
    float aspect = 0;
    const auto address = reinterpret_cast<std::uintptr_t>(camera);
    if (!ReadableGameObject(address, discovery.camera_size) ||
        !ReadGameValue(address + discovery.camera_aspect, aspect) ||
        !ReadableGameObject(reinterpret_cast<std::uintptr_t>(position), 12) ||
        !ReadableGameObject(reinterpret_cast<std::uintptr_t>(up), 12) ||
        !ReadableGameObject(reinterpret_cast<std::uintptr_t>(right), 12) ||
        !ReadableGameObject(reinterpret_cast<std::uintptr_t>(forward), 12)) {
        LogUnreadableFov(camera); return false;
    }
    FovReading fov;
    const bool have_fov = ReadFov(camera, fov);
    if (have_fov && std::isfinite(aspect) && std::fabs(aspect * fov.aspect - 1.0f) < 0.002f) {
        LogCameraChange(camera, fov);
        if (BaseWidenedOnLastRead()) {
            Log::Line("[camera] camera 0x%p now rests at %.4f deg horizontal - reading it as the "
                      "field of view being changed in Settings, not as a zoom", camera,
                      fov.base_horizontal_deg);
        }
        LogZoomExcursion(fov);
    } else {
        LogUnreadableFov(camera);
        return false;
    }

    HeadPose pose;
    if (!PoseForThisFrame(pose, fov.zoom_factor)) return false;

    const CameraBasis clean = BasisFromArguments(position, up, right, forward);
    composed = clean;
    ApplyHeadPose(composed, pose);
    LogComposedFrame(clean, composed, pose);
    return true;
}

void __fastcall SetTransformDetour(void* camera, const float* position, const float* up,
                                   const float* right, const float* forward) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (caller != g_return_a && caller != g_return_b) {
        g_original_set_transform(camera, position, up, right, forward);
        return;
    }

    CameraBasis composed;
    if (!ComposeBasis(camera, position, up, right, forward, composed)) {
        g_original_set_transform(camera, position, up, right, forward);
        return;
    }
    g_original_set_transform(camera, composed.eye, composed.up, composed.right, composed.forward);
}

}  // namespace

bool InstallCameraHook() {
    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    using cameraunlock::hooks::HookStatusToString;

    const builds::BuildProfile& profile = builds::ActiveProfile();
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    g_return_a = base + profile.Offsets.camera_system_return_a_rva;
    g_return_b = base + profile.Offsets.camera_system_return_b_rva;
    InitFovTracking({profile.Offsets.camera_horizontal_fov, profile.Offsets.camera_vertical_fov});
    void* const target = reinterpret_cast<void*>(base + profile.Offsets.camera_set_transform_rva);

    HookManager& hooks = HookManager::Instance();
    const HookStatus initialized = hooks.Initialize();
    if (initialized != HookStatus::Ok && initialized != HookStatus::ErrorAlreadyInitialized) {
        Log::Line("[camera] MinHook init failed: %s", HookStatusToString(initialized));
        return false;
    }

    cameraunlock::hooks::ScopedHook hook;
    const HookStatus created = hook.Create(target, reinterpret_cast<void*>(&SetTransformDetour),
                                           reinterpret_cast<void**>(&g_original_set_transform));
    if (created != HookStatus::Ok) {
        Log::Line("[camera] hooking Camera::SetTransform failed: %s", HookStatusToString(created));
        return false;
    }
    hook.Release();

    Log::Line("[camera] hooked Camera::SetTransform at 0x%p (profile %s)", target, profile.Name);
    return true;
}

}  // namespace rc_ht
