// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "headtracking_mod.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <exception>
#include <filesystem>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include "builds/build_registry.h"
#include "camera_fov.h"
#include "camera_hook.h"
#include "camera_transform.h"
#include "config.h"
#include "game_state.h"
#include "logging.h"
#include "window_centering.h"

#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/os/module_paths.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"

namespace rc_ht {

namespace {

using Session = cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>;
static_assert(Session::kHasRemoteConnection,
              "UdpReceiver must expose IsRemoteConnection() to select Local/RemoteSmoothing");

Config g_config;

// Deliberately leaked, and references rather than objects so that no destructor
// is registered in the module's onexit table. MSVC calls that table from
// DLL_PROCESS_DETACH whatever the reason for the detach - __scrt_dllmain_-
// uninitialize_c() runs before the is_terminating check, not after it - so a
// plain object here would have the receiver and the poller joining their threads
// from inside DllMain at process exit, under the loader lock, after the kernel
// has already killed those threads without unwinding. If one of them died
// holding the CRT heap lock the teardown that follows the join deadlocks, and
// the game sits in Task Manager after the player has quit it. The table is not
// empty either way - HookManager puts an entry in it - so this is about these
// three and the threads they own. See the note in dllmain.cpp.
cameraunlock::UdpReceiver& g_receiver = *new cameraunlock::UdpReceiver();
Session& g_session = *new Session(g_receiver);
cameraunlock::input::HotkeyPoller& g_hotkeys = *new cameraunlock::input::HotkeyPoller();

// An ordinary object: what makes the three above dangerous at detach is that
// their destructors JOIN threads, and ~mutex neither joins nor blocks.
std::mutex g_camera_path_mutex;
cameraunlock::time::FrameClock g_frame_clock;

std::atomic<bool> g_tracking_enabled{false};

// Publishes the bootstrap's Config and session settings to the camera threads.
std::atomic<bool> g_active{false};

std::atomic<bool> g_pinned{false};

void ApplyConfigToPipeline(const Config& config, Session& session) {
    session.SetLocalSmoothing(config.local_smoothing);
    session.SetRemoteSmoothing(config.remote_smoothing);
    session.SetPositionSettings(config::ToPositionSettings(config));
    session.SetMode(config::StartupTrackingMode(config));
}

// ---------------------------------------------------------------------------
// Diagnostics. Edge-triggered: this runs on the camera path every frame.
// ---------------------------------------------------------------------------

void LogTrackerConnection() {
    static bool last_receiving = false;
    const bool receiving = g_receiver.IsReceiving();
    if (receiving == last_receiving) return;
    last_receiving = receiving;
    Log::Line("[udp] tracker data %s",
              receiving ? "is arriving" : "stopped arriving - holding the last pose");
}

void LogConnectionLocality() {
    static bool last_remote = false;
    static bool known = false;
    const bool is_remote = g_session.IsRemoteConnection();
    if (known && is_remote == last_remote) return;
    last_remote = is_remote;
    known = true;
    Log::Line("[udp] tracker source is %s - smoothing=%.2f",
              is_remote ? "a remote device" : "on this machine",
              cameraunlock::math::GetEffectiveSmoothing(
                  g_config.local_smoothing, g_config.remote_smoothing, is_remote));
}

void LogGateChange(bool menu_open, bool loading, bool multiplayer) {
    static int last = -1;
    const int now = (menu_open ? 1 : 0) | (loading ? 2 : 0) | (multiplayer ? 4 : 0);
    if (now == last) return;
    last = now;
    Log::Line("[state] menu %s, loading screen %s, co-op session %s - head tracking is %s",
              menu_open ? "open" : "closed", loading ? "up" : "down",
              multiplayer ? "LIVE" : "not running",
              (menu_open || loading || multiplayer) ? "held off" : "allowed");
}

// ---------------------------------------------------------------------------
// Per-frame state the camera path advances
// ---------------------------------------------------------------------------

// A level load replaces the cameras, so the field of view each one rests at has
// to be learned again rather than carried over from the last level. A slider
// moved mid-level is not this function's business: clearing the bases on the
// menu close would reseed them at whatever zoom the camera happened to be
// holding, so BaseFor recognises a slider move by its size instead.
void ForgetFovBasesOnLevelLoad(bool loading) {
    static bool last_loading = false;
    if (loading && !last_loading) ResetFovTracking();
    last_loading = loading;
}

// The configured travel limits, for the re-clamp after the zoom scaling. The
// processor clamps the lean as its last step and the scaling then multiplies
// what it handed back: at the widest frame this game renders that factor is
// 1.1186, so a PositionLimitX of 0.30 reaches the camera as 0.336 and the number the
// player configured is not the number they get. Clamping last does cost the
// compensation at the limit itself - a lean already at 0.30 is cut back from
// 0.336 to 0.300, so its screen displacement is short by that factor - but a
// limit that keeps the eye inside the cab is not a figure to round up.
LeanLimits ConfiguredLeanLimits() {
    return LeanLimits{ g_config.position_limit_x, g_config.position_limit_y,
                       g_config.position_limit_y_down, g_config.position_limit_z,
                       g_config.position_limit_z_back };
}

// How far the composed pose is from identity when the gate opens or shuts: it
// eases rather than snapping, so leaving the pause menu with the head turned
// swings the view round instead of cutting to it. A 120ms time constant.
constexpr float kGateBlendSpeed = 1.0f / 0.12f;
float g_gate_weight = 0.0f;

// Within a thousandth of an end the weight is snapped to it, so a gate that
// is shut stops composing rather than easing forever on a pose that never
// quite reaches identity.
constexpr float kGateShut = 0.001f;
constexpr float kGateOpen = 0.999f;

float AdvanceGateWeight(bool following, float dt) {
    const float target = following ? 1.0f : 0.0f;
    // Already there, which is every frame of steady driving and every frame of a
    // menu: the blend below would move it by nothing, at the cost of an exp.
    if (g_gate_weight == target) return g_gate_weight;

    g_gate_weight += (target - g_gate_weight) * (1.0f - std::exp(-kGateBlendSpeed * dt));
    if (!following && g_gate_weight < kGateShut) g_gate_weight = 0.0f;
    if (following && g_gate_weight > kGateOpen) g_gate_weight = 1.0f;
    return g_gate_weight;
}

// ---------------------------------------------------------------------------
// Hotkeys
// ---------------------------------------------------------------------------

void ToggleTracking() {
    const bool on = !g_tracking_enabled.load();
    g_tracking_enabled.store(on);
    Log::Line("[input] tracking %s", on ? "enabled" : "disabled");
}

const char* ModeName(cameraunlock::TrackingMode mode) {
    switch (mode) {
        case cameraunlock::TrackingMode::RotationAndPosition: return "rotation and position";
        case cameraunlock::TrackingMode::RotationOnly:        return "rotation only";
        case cameraunlock::TrackingMode::PositionOnly:        return "position only";
    }
    throw std::logic_error("TrackingMode outside its three modes");
}

// Runs on the hotkey poller's thread. The mode changes under the camera path's
// lock, since CycleMode resets the position processor's smoothing and the
// position interpolator, plain members a worker thread may be inside Update()
// writing at the same moment. The save runs once the lock is released, so a
// slow disk never holds up the camera path: the session has the new mode first,
// then CameraUnlock.ini saves it, so the next start begins in it.
//
// The poller calls this from a bare std::thread with no handler above it, so an
// exception escaping here is std::terminate and the game closes on a key press.
// Save reports most disk failures through its result but still throws for some
// (reading the file's write time back after a committed write, for one), and a
// failed save is the same outcome either way: logged, and the session keeps the
// mode the player chose.
void CycleTrackingMode() {
    cameraunlock::TrackingMode mode;
    {
        const std::lock_guard<std::mutex> guard(CameraPathMutex());
        mode = g_session.CycleMode();
    }
    Log::Line("[input] tracking mode: %s", ModeName(mode));
    try {
        config::SaveTrackingMode(mode);
    } catch (const std::exception& e) {
        Log::Line("[config] saving the tracking mode failed: %s - this session keeps it, the next "
                  "start does not", e.what());
    }
}

// The table's hotkey codec lets only a list ParseKeyBindings reads into the
// settings.
void RegisterList(const std::string& list, std::function<void()> action) {
    const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) throw std::logic_error("hotkey list '" + list + "': " + parsed.error);
    cameraunlock::input::RegisterKeyBindings(g_hotkeys, parsed.bindings, std::move(action));
}

// HotkeyPoller::Start never returns false - it reports a thread it could not
// create by rethrowing, so that a caller cannot mistake a dead poller for a live
// one. Bootstrap runs on a raw CreateThread routine with no handler above it, so
// letting that escape would take the game down with the log ending on the
// camera-hook line. Caught here, the mod keeps tracking on whatever
// EnableOnStartup says and the log states that no key can change it.
bool RegisterHotkeys(const Config& config) {
    RegisterList(config.toggle_key, ToggleTracking);
    RegisterList(config.cycle_tracking_mode_key, CycleTrackingMode);
    try {
        return g_hotkeys.Start();
    } catch (const std::exception& e) {
        Log::Line("[boot] the hotkey thread could not be created: %s", e.what());
        return false;
    }
}

// ---------------------------------------------------------------------------
// Bootstrap
// ---------------------------------------------------------------------------

bool OpenLogAndResolveGameDirectory(std::wstring& exe_dir_wide, std::string& exe_dir) {
    exe_dir_wide = cameraunlock::os::HostExeDirectory();
    Log::Open(exe_dir_wide.empty() ? std::wstring(L"HeadTracking.log")
                                   : exe_dir_wide + L"\\HeadTracking.log");
    Log::Line("=== RoadCraft Head Tracking v%s ===", RC_HT_VERSION);

    // The log's lines take narrow text, and every earlier build stayed dormant
    // on a directory with no ANSI form, since its HeadTracking.ini reader was
    // ANSI-only. The frozen import of that file still is, so this stays as it
    // was.
    if (exe_dir_wide.empty() || !cameraunlock::os::NarrowToAnsi(exe_dir_wide, exe_dir)) {
        Log::Line("[boot] could not resolve the game directory - mod is dormant, game runs vanilla.");
        return false;
    }
    Log::Line("[boot] game directory: %s", exe_dir.c_str());
    return true;
}

void LoadAndApplyConfig(const std::wstring& exe_dir) {
    g_config = config::Load(std::filesystem::path(exe_dir), cameraunlock::config::DefaultsFile::PerUser());
    Log::Line("[boot] config: port=%u enableOnStartup=%d localSmoothing=%.2f remoteSmoothing=%.2f "
              "mode=%s limits x=%.2f y=%.2f/%.2f z=%.2f/%.2f",
              static_cast<unsigned>(g_config.udp_port), g_config.enable_on_startup ? 1 : 0,
              g_config.local_smoothing, g_config.remote_smoothing,
              ModeName(config::StartupTrackingMode(g_config)), g_config.position_limit_x,
              g_config.position_limit_y, g_config.position_limit_y_down, g_config.position_limit_z,
              g_config.position_limit_z_back);
    ApplyConfigToPipeline(g_config, g_session);
    g_tracking_enabled.store(g_config.enable_on_startup);
}

void StartReceiver() {
    g_receiver.SetLog([](const std::string& msg) { Log::Line("[udp] %s", msg.c_str()); });
    if (g_receiver.Start(g_config.udp_port)) {
        Log::Line("[boot] listening for OpenTrack data on UDP %u",
                  static_cast<unsigned>(g_config.udp_port));
    }
}

// A FreeLibrary must not unmap an inline detour a game thread may be inside,
// and there is no safe teardown to run from DllMain instead.
bool PinModule() {
    HMODULE self = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                  | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&ApplyConfigToPipeline),
                              &self) != FALSE;
}

void Bootstrap() {
    std::wstring exe_dir_wide;
    std::string exe_dir;
    if (!OpenLogAndResolveGameDirectory(exe_dir_wide, exe_dir)) return;

    if (!g_pinned.load()) {
        Log::Line("[boot] this module could not be pinned against unloading - mod is dormant, "
                  "game runs vanilla.");
        return;
    }
    if (builds::SelectProfile(GetModuleHandleW(nullptr)) != builds::ProfileSelection::Matched) {
        Log::Line("[boot] no usable build profile - mod is dormant, game runs vanilla.");
        return;
    }
    if (!InitGameState()) {
        Log::Line("[boot] the gameplay gate could not be resolved - mod is dormant, game runs "
                  "vanilla.");
        return;
    }

    LoadAndApplyConfig(exe_dir_wide);
    StartReceiver();

    if (!InstallCameraHook()) {
        g_receiver.Stop();
        Log::Line("[boot] the camera could not be hooked - mod is inert.");
        return;
    }

    const bool hotkeys_live = RegisterHotkeys(g_config);
    g_active.store(true, std::memory_order_release);
    if (hotkeys_live) {
        Log::Line("[boot] ready. %s toggle tracking, %s cycle tracking mode.",
                  g_config.toggle_key.c_str(), g_config.cycle_tracking_mode_key.c_str());
    } else {
        // The ready line names the keys and is the only place the log says what
        // they do, so printing it regardless would leave a player whose keys do
        // nothing reading a log that says they work.
        Log::Line("[boot] ready, but the hotkey thread could not start - tracking stays on the "
                  "EnableOnStartup setting (%d) and no key can change it.",
                  g_config.enable_on_startup ? 1 : 0);
    }

    // Last, because it blocks for as long as the engine takes to place its
    // window: the hook, the receiver and the hotkeys are all live before this
    // waits on anything. It is also below the dormant returns above on purpose -
    // a build this mod does not know leaves the game entirely alone, and moving
    // the player's window would be the one thing it still did.
    CenterWindowWhenReady();
}

// A CreateThread start routine has no handler above it, so anything that escapes
// Bootstrap reaches UnhandledExceptionFilter and takes the game down with it -
// over a mod that is only ever meant to fail dormant. Bootstrap allocates all
// through: the config, the hotkey lists, the ready line.
DWORD WINAPI BootstrapThread(LPVOID) {
    try {
        Bootstrap();
    } catch (const std::exception& e) {
        Log::Line("[boot] bootstrap failed: %s - mod is dormant, game runs vanilla.", e.what());
    } catch (...) {
        Log::Line("[boot] bootstrap failed - mod is dormant, game runs vanilla.");
    }
    return 0;
}

}  // namespace

std::mutex& CameraPathMutex() { return g_camera_path_mutex; }

bool PoseForThisFrame(HeadPose& pose, float zoom_factor) {
    if (!g_active.load(std::memory_order_acquire)) return false;

    const float dt = g_frame_clock.Tick();
    if (g_session.Update(dt)) LogConnectionLocality();
    LogTrackerConnection();

    HeadPose tracked;
    const bool have_rotation = g_session.GetRotation(tracked.yaw, tracked.pitch, tracked.roll);
    g_session.GetPositionOffset(tracked.lean_x, tracked.lean_y, tracked.lean_z);

    const bool menu_open = IsMenuOpen();
    const bool loading = IsLoadingScreenUp();
    const bool multiplayer = IsMultiplayerSessionLive();
    LogGateChange(menu_open, loading, multiplayer);
    ForgetFovBasesOnLevelLoad(loading);

    const bool following = have_rotation
        && ShouldFollowHead(g_tracking_enabled.load(std::memory_order_relaxed), menu_open,
                            loading, multiplayer);

    const float weight = AdvanceGateWeight(following, dt);
    if (weight == 0.0f || !have_rotation) return false;

    pose = ClampLean(ScalePoseForZoom(BlendPose(tracked, weight), zoom_factor),
                     ConfiguredLeanLimits());
    return true;
}

void Initialize() {
    g_pinned.store(PinModule());
    // CreateThread rather than std::thread: DllMain must not throw, and a
    // bootstrap that cannot start leaves the game running vanilla.
    const HANDLE thread = CreateThread(nullptr, 0, &BootstrapThread, nullptr, 0, nullptr);
    if (thread != nullptr) CloseHandle(thread);
}

}  // namespace rc_ht
