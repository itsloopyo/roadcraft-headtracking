// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include "build_profile.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace rc_ht::builds {

struct ImageView {
    const std::uint8_t* data;
    std::size_t size;
    std::uint64_t base;
};

struct DiscoveryResult {
    OffsetTable offsets{};
    unsigned camera_size = 0;
    unsigned camera_aspect = 0;
    unsigned client_size = 0;
    unsigned manager_size = 0;
    unsigned camera_component_vtable = 0;
    unsigned camera_component_member = 0;
    std::int32_t session_idle = 0;
    std::string error;
};

bool DiscoverRuntime(ImageView image, DiscoveryResult& result);

}
