// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "build_registry.h"
#include "logging.h"
#include <cstring>

namespace rc_ht::builds {

const BuildProfile* const kKnownProfiles[] = {&kSteamProfile_20260911, &kSteamProfile_20260902};
const std::size_t kKnownProfileCount = sizeof(kKnownProfiles) / sizeof(kKnownProfiles[0]);

namespace {
BuildProfile g_active{};
DiscoveryResult g_discovery{};
}

const BuildProfile& ActiveProfile() { return g_active; }
const DiscoveryResult& ActiveDiscovery() { return g_discovery; }

ProfileSelection SelectProfile(ImageView view) {
    g_active = {};
    g_discovery = {};
    DiscoveryResult result;
    if (!DiscoverRuntime(view, result)) {
        Log::Line("[build] runtime discovery rejected: %s; no dependent hooks installed", result.error.c_str());
        return ProfileSelection::NoMatch;
    }
    unsigned nt;
    std::memcpy(&nt, view.data + 0x3c, sizeof(nt));
    cameraunlock::memory::PeFingerprint fingerprint{};
    std::memcpy(&fingerprint.TimeDateStamp, view.data + nt + 8, 4);
    std::memcpy(&fingerprint.SizeOfImage, view.data + nt + 24 + 56, 4);
    std::memcpy(&fingerprint.CheckSum, view.data + nt + 24 + 64, 4);
    const char* name = "unlisted-runtime-discovery";
    for (const auto* profile : kKnownProfiles) {
        if (!fingerprint.Matches(profile->Fingerprint)) continue;
        static_assert(sizeof(OffsetTable) == 12 * sizeof(unsigned), "offset table has padding");
        if (std::memcmp(&profile->Offsets, &result.offsets, sizeof(OffsetTable)) != 0 || result.session_idle != 0) {
            Log::Line("[build] runtime discovery disagrees with historical profile %s; staying dormant", profile->Name);
            return ProfileSelection::NoMatch;
        }
        name = profile->Name;
    }
    g_active = {name, fingerprint, result.offsets};
    g_discovery = std::move(result);
    const auto& o = g_active.Offsets;
    Log::Line("[build] runtime discovery selected %s: %08X/%08X/%08X", name,
        fingerprint.TimeDateStamp, fingerprint.SizeOfImage, fingerprint.CheckSum);
    Log::Line("[build] camera=%08X callers=%08X/%08X FOV=%X/%X size=%X owner-vtable=%08X member=%X",
        o.camera_set_transform_rva, o.camera_system_return_a_rva, o.camera_system_return_b_rva,
        o.camera_horizontal_fov, o.camera_vertical_fov, g_discovery.camera_size,
        g_discovery.camera_component_vtable, g_discovery.camera_component_member);
    Log::Line("[build] loading=%08X client=%08X/%08X size=%X manager=%X/%08X size=%X session=%X state=%X idle=%d",
        o.loading_screen_global_rva, o.matchmaking_client_global_rva, o.matchmaking_client_vtable_rva,
        g_discovery.client_size, o.client_session_manager, o.session_manager_vtable_rva,
        g_discovery.manager_size, o.session_manager_session, o.session_manager_state, g_discovery.session_idle);
    return ProfileSelection::Matched;
}

}
