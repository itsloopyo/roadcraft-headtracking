// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once
#include <windows.h>
#include <cstdint>
#include <limits>

namespace rc_ht {

inline bool ReadableGameObject(std::uintptr_t address, std::size_t size) {
    if (!address || !size || address > std::numeric_limits<std::uintptr_t>::max() - size) return false;
    const auto end = address + size;
    while (address < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) ||
            region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD)) return false;
        const auto flags = region.Protect & 0xff;
        if (flags != PAGE_READONLY && flags != PAGE_READWRITE && flags != PAGE_WRITECOPY &&
            flags != PAGE_EXECUTE_READ && flags != PAGE_EXECUTE_READWRITE && flags != PAGE_EXECUTE_WRITECOPY) return false;
        const auto begin = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (region.RegionSize > std::numeric_limits<std::uintptr_t>::max() - begin || begin + region.RegionSize <= address) return false;
        address = begin + region.RegionSize;
    }
    return true;
}

template<class T> bool ReadGameValue(std::uintptr_t address, T& value) {
    if (address % alignof(T) != 0) return false;
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), &value, sizeof(value), &copied) && copied == sizeof(value);
}

}
