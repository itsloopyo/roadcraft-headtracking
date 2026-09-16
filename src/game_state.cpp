// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "game_state.h"

#include <windows.h>

#include <atomic>
#include <cstdint>

#include "builds/build_registry.h"
#include "logging.h"

#include "cameraunlock/hooks/hook_manager.h"

namespace rc_ht {

namespace {

// Exported by the game executable for its sound plugins, so the setter is found
// by name rather than pinned to an address. The string overload tail-calls this
// one, so hooking the ID overload sees every state change.
constexpr char kSetStateByIdExport[] = "?SetState@SoundEngine@AK@@YA?AW4AKRESULT@@KK@Z";

using SetStateByIdFn = int(__fastcall*)(std::uint32_t, std::uint32_t);
SetStateByIdFn g_original_set_state = nullptr;

std::uint32_t g_menu_group_id = 0;
std::uint32_t g_menu_off_id = 0;

std::atomic<std::uint32_t> g_menu_state{0};
std::atomic<bool> g_menu_state_known{false};

std::uintptr_t g_module_base = 0;
builds::OffsetTable g_offsets{};
std::atomic<bool> g_logged_client_mismatch{false};
std::atomic<bool> g_logged_manager_mismatch{false};

int __fastcall SetStateByIdDetour(std::uint32_t group, std::uint32_t state) {
    if (group == g_menu_group_id) {
        g_menu_state.store(state, std::memory_order_relaxed);
        g_menu_state_known.store(true, std::memory_order_relaxed);
    }
    return g_original_set_state(group, state);
}

std::uintptr_t ReadPointer(std::uintptr_t address) {
    return *reinterpret_cast<const std::uintptr_t*>(address);
}

// A vtable that is not the one the profile was derived against means the
// pointer is some other class, so nothing at these offsets can be trusted.
// Said once per object, because the caller runs on the camera path.
bool HasProfileVtable(std::uintptr_t object, unsigned vtable_rva,
                      std::atomic<bool>& logged, const char* what) {
    if (ReadPointer(object) == g_module_base + vtable_rva) return true;
    if (!logged.exchange(true)) {
        Log::Line("[state] the %s at 0x%llX is not the class this build profile expects - "
                  "treating every frame as a co-op session.",
                  what, static_cast<unsigned long long>(object));
    }
    return false;
}

}  // namespace

std::uint32_t WwiseShortId(const char* name) {
    std::uint32_t hash = 2166136261u;
    for (const char* c = name; *c != '\0'; ++c) {
        const char lower = (*c >= 'A' && *c <= 'Z') ? static_cast<char>(*c + ('a' - 'A')) : *c;
        hash *= 16777619u;
        hash ^= static_cast<std::uint8_t>(lower);
    }
    return hash;
}

bool InitGameState() {
    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    using cameraunlock::hooks::HookStatusToString;

    const HMODULE module = GetModuleHandleW(nullptr);
    g_module_base = reinterpret_cast<std::uintptr_t>(module);
    g_offsets = builds::ActiveProfile().Offsets;
    g_menu_group_id = WwiseShortId("ui_menu_toggle");
    g_menu_off_id = WwiseShortId("menu_off");

    void* const set_state = reinterpret_cast<void*>(GetProcAddress(module, kSetStateByIdExport));
    if (set_state == nullptr) {
        Log::Line("[state] the game no longer exports %s - the menu gate cannot be resolved.",
                  kSetStateByIdExport);
        return false;
    }

    HookManager& hooks = HookManager::Instance();
    const HookStatus initialized = hooks.Initialize();
    if (initialized != HookStatus::Ok && initialized != HookStatus::ErrorAlreadyInitialized) {
        Log::Line("[state] MinHook init failed: %s", HookStatusToString(initialized));
        return false;
    }
    cameraunlock::hooks::ScopedHook hook;
    const HookStatus created = hook.Create(set_state, reinterpret_cast<void*>(&SetStateByIdDetour),
                                           reinterpret_cast<void**>(&g_original_set_state));
    if (created != HookStatus::Ok) {
        Log::Line("[state] hooking the sound state setter failed: %s", HookStatusToString(created));
        return false;
    }
    hook.Release();

    Log::Line("[state] watching sound state group ui_menu_toggle (%08X) at 0x%p; menu_off is %08X",
              g_menu_group_id, set_state, g_menu_off_id);
    return true;
}

bool IsMenuOpen() {
    if (!g_menu_state_known.load(std::memory_order_relaxed)) return true;
    return g_menu_state.load(std::memory_order_relaxed) != g_menu_off_id;
}

bool IsLoadingScreenUp() {
    return ReadPointer(g_module_base + g_offsets.loading_screen_global_rva) != 0;
}

bool IsMultiplayerSessionLive() {
    const std::uintptr_t client = ReadPointer(g_module_base + g_offsets.matchmaking_client_global_rva);
    if (client == 0) return false;
    if (!HasProfileVtable(client, g_offsets.matchmaking_client_vtable_rva,
                          g_logged_client_mismatch, "matchmaking client")) {
        return true;
    }

    const std::uintptr_t manager = ReadPointer(client + g_offsets.client_session_manager);
    if (manager == 0) return false;
    if (!HasProfileVtable(manager, g_offsets.session_manager_vtable_rva,
                          g_logged_manager_mismatch, "game session manager")) {
        return true;
    }

    const std::uintptr_t session = ReadPointer(manager + g_offsets.session_manager_session);
    const std::int32_t state =
        *reinterpret_cast<const std::int32_t*>(manager + g_offsets.session_manager_state);
    return session != 0 || state != 0;
}

bool ShouldFollowHead(bool tracking_enabled, bool menu_open, bool loading,
                      bool multiplayer_session_live) {
    return tracking_enabled && !menu_open && !loading && !multiplayer_session_live;
}

}  // namespace rc_ht
