// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>

#include "builds/build_profile.h"
#include "runtime_discovery.h"

namespace rc_ht::builds {

extern const BuildProfile kSteamProfile_20260911;
extern const BuildProfile kSteamProfile_20260902;

extern const BuildProfile* const kKnownProfiles[];
extern const std::size_t kKnownProfileCount;

enum class ProfileSelection {
    Matched,
    NoMatch,
};

// Discovers and validates the running module. Must run before any
// hook is installed; anything other than Matched leaves the mod dormant.
ProfileSelection SelectProfile(void* moduleBase);
ProfileSelection SelectProfile(ImageView image);

const BuildProfile& ActiveProfile();
const DiscoveryResult& ActiveDiscovery();

}  // namespace rc_ht::builds
