// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "build_registry.h"
#include "logging.h"
#include <windows.h>
#include <algorithm>
#include <limits>
#include <vector>

namespace rc_ht::builds {

ProfileSelection SelectProfile(void* moduleBase) {
    cameraunlock::memory::PeFingerprint fingerprint{};
    if (!cameraunlock::memory::ReadPeFingerprint(moduleBase, fingerprint) || !fingerprint.SizeOfImage ||
        fingerprint.SizeOfImage > 0x40000000) return SelectProfile(ImageView{});
    const auto base = reinterpret_cast<std::uintptr_t>(moduleBase);
    if (base > std::numeric_limits<std::uintptr_t>::max() - fingerprint.SizeOfImage)
        return SelectProfile(ImageView{});
    std::vector<std::uint8_t> snapshot(fingerprint.SizeOfImage);
    const auto start = GetTickCount64();
    for (std::uintptr_t at = base; at < base + snapshot.size();) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<const void*>(at), &region, sizeof(region)) ||
            region.RegionSize > std::numeric_limits<std::uintptr_t>::max() - reinterpret_cast<std::uintptr_t>(region.BaseAddress)) {
            Log::Line("[build] module memory query failed (%lu)", GetLastError());
            return SelectProfile(ImageView{});
        }
        const auto end = std::min(base + snapshot.size(), reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize);
        if (end <= at) return SelectProfile(ImageView{});
        const auto protection = region.Protect & 0xff;
        const bool readable = protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
            protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        if (region.State == MEM_COMMIT && readable && !(region.Protect & PAGE_GUARD)) {
            SIZE_T read = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(at), snapshot.data() + at - base, end - at, &read) || read != end - at) {
                Log::Line("[build] module snapshot failed (%lu)", GetLastError());
                return SelectProfile(ImageView{});
            }
        }
        at = end;
    }
    const auto selected = SelectProfile(ImageView{snapshot.data(), snapshot.size(), base});
    if (selected == ProfileSelection::Matched) {
        const auto& offsets = ActiveProfile().Offsets;
        for (auto rva : {offsets.camera_set_transform_rva, offsets.camera_system_return_a_rva,
                        offsets.camera_system_return_b_rva}) {
            MEMORY_BASIC_INFORMATION region{};
            const auto address = reinterpret_cast<const void*>(base + rva);
            if (!VirtualQuery(address, &region, sizeof(region)) || region.AllocationBase != moduleBase ||
                region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD) ||
                !(region.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
                Log::Line("[build] discovered camera code is not executable in the running module");
                return SelectProfile(ImageView{});
            }
        }
    }
    Log::Line("[build] discovery finished in %llu ms", GetTickCount64() - start);
    return selected;
}

}
