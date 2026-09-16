// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "headtracking_mod.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <exception>
#include <mutex>
#include <string>

#include "builds/build_registry.h"
#include "camera_fov.h"
#include "camera_hook.h"
#include "camera_transform.h"
#include "config.h"
#include "game_state.h"
#include "hotkey_names.h"
#include "logging.h"
#include "window_centering.h"

#include "cameraunlock/input/chord_hotkeys.h"
#include "cameraunlock/input/hotkey_poller.h"
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

    cameraunlock::PositionSettings position;
    position.limit_x = config.limit_x;
    position.limit_y = config.limit_y;
    position.limit_y_down = config.limit_y_down;
    position.limit_z = config.limit_z;
    position.limit_z_back = config.limit_z_back;
    session.SetPositionSettings(position);

    session.SetMode(config.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                                            : cameraunlock::TrackingMode::RotationOnly);
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
// 1.1186, so a LimitX of 0.30 reaches the camera as 0.336 and the number the
// player configured is not the number they get. Clamping last does cost the
// compensation at the limit itself - a lean already at 0.30 is cut back from
// 0.336 to 0.300, so its screen displacement is short by that factor - but a
// limit that keeps the eye inside the cab is not a figure to round up.
LeanLimits ConfiguredLeanLimits() {
    return LeanLimits{ g_config.limit_x, g_config.limit_y, g_config.limit_y_down,
                       g_config.limit_z, g_config.limit_z_back };
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

// Under the camera path's lock: CycleMode resets the position processor's
// smoothing and the position interpolator, plain members a worker thread may be
// inside Update() writing at the same moment.
void CycleTrackingMode() {
    const std::lock_guard<std::mutex> guard(CameraPathMutex());
    const char* name = "";
    switch (g_session.CycleMode()) {
        case cameraunlock::TrackingMode::RotationAndPosition: name = "rotation and position"; break;
        case cameraunlock::TrackingMode::RotationOnly:        name = "rotation only"; break;
        case cameraunlock::TrackingMode::PositionOnly:        name = "position only"; break;
    }
    Log::Line("[input] tracking mode: %s", name);
}

struct HotkeyBinding {
    const char* action;
    int nav_key;
    int chord_key;
    void (*handler)();
};

std::array<HotkeyBinding, 2> Bindings(const Config& config) {
    return {{
        { "toggle tracking",     config.toggle_key,     config.chord_toggle_key,     ToggleTracking },
        { "cycle tracking mode", config.cycle_mode_key, config.chord_cycle_mode_key, CycleTrackingMode },
    }};
}

// HotkeyPoller::Start never returns false - it reports a thread it could not
// create by rethrowing, so that a caller cannot mistake a dead poller for a live
// one. Bootstrap runs on a raw CreateThread routine with no handler above it, so
// letting that escape would take the game down with the log ending on the
// camera-hook line. Caught here, the mod keeps tracking on whatever
// EnableOnStartup says and the log states that no key can change it.
bool RegisterHotkeys(const Config& config) {
    using namespace cameraunlock::input;
    for (const HotkeyBinding& binding : Bindings(config)) {
        g_hotkeys.AddHotkey(binding.nav_key, NavGuarded(binding.handler));
        g_hotkeys.AddHotkey(binding.chord_key, ChordGuarded(binding.handler));
    }
    try {
        return g_hotkeys.Start();
    } catch (const std::exception& e) {
        Log::Line("[boot] the hotkey thread could not be created: %s", e.what());
        return false;
    }
}

void LogHotkeys(const Config& config) {
    std::string ready = "[boot] ready.";
    for (const HotkeyBinding& binding : Bindings(config)) {
        ready += " " + HotkeyName(binding.nav_key) + "/Ctrl+Shift+"
               + HotkeyName(binding.chord_key) + " " + binding.action + ",";
    }
    ready.back() = '.';
    Log::Line("%s", ready.c_str());
}

// ---------------------------------------------------------------------------
// Bootstrap
// ---------------------------------------------------------------------------

bool OpenLogAndResolveGameDirectory(std::string& exe_dir) {
    const std::wstring exe_dir_wide = cameraunlock::os::HostExeDirectory();
    Log::Open(exe_dir_wide.empty() ? std::wstring(L"HeadTracking.log")
                                   : exe_dir_wide + L"\\HeadTracking.log");
    Log::Line("=== RoadCraft Head Tracking v%s ===", RC_HT_VERSION);

    // IniReader is ANSI-only, so a directory with no ANSI form is unusable.
    if (exe_dir_wide.empty() || !cameraunlock::os::NarrowToAnsi(exe_dir_wide, exe_dir)) {
        Log::Line("[boot] could not resolve the game directory - mod is dormant, game runs vanilla.");
        return false;
    }
    Log::Line("[boot] game directory: %s", exe_dir.c_str());
    return true;
}

void LoadAndApplyConfig(const std::string& exe_dir) {
    WriteDefaultConfigIfMissing(exe_dir);
    LoadConfig(exe_dir, g_config);
    Log::Line("[boot] config: port=%u enableOnStartup=%d localSmoothing=%.2f remoteSmoothing=%.2f "
              "position=%d limits x=%.2f y=%.2f/%.2f z=%.2f/%.2f",
              static_cast<unsigned>(g_config.udp_port), g_config.enable_on_startup ? 1 : 0,
              g_config.local_smoothing, g_config.remote_smoothing,
              g_config.position_enabled ? 1 : 0, g_config.limit_x, g_config.limit_y,
              g_config.limit_y_down, g_config.limit_z, g_config.limit_z_back);
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
    std::string exe_dir;
    if (!OpenLogAndResolveGameDirectory(exe_dir)) return;

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

    LoadAndApplyConfig(exe_dir);
    StartReceiver();

    if (!InstallCameraHook()) {
        g_receiver.Stop();
        Log::Line("[boot] the camera could not be hooked - mod is inert.");
        return;
    }

    const bool hotkeys_live = RegisterHotkeys(g_config);
    g_active.store(true, std::memory_order_release);
    if (hotkeys_live) {
        LogHotkeys(g_config);
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
// through: the config strings, the hotkey list, the ready line.
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
