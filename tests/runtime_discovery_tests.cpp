// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "discovery_fixture.h"
#include "test_support.h"
#include "builds/build_registry.h"
#include "builds/camera_contract.h"
#include "builds/object_flow.h"
#include <functional>

using namespace rc_ht::builds;
using rc_test::Check;
using rc_test::DiscoveryFixture;

namespace {
void Reject(DiscoveryFixture& fixture, const char* name) {
    DiscoveryResult result;
    result.offsets.camera_set_transform_rva = 123;
    Check(!DiscoverRuntime(fixture.View(), result), name);
    Check(result.offsets.camera_set_transform_rva == 0 && !result.error.empty(), "rejection clears output and reports its cause");
}
unsigned InstructionAt(const DiscoveryFixture& fixture, const char* function,
    const std::function<bool(const discovery::Instruction&)>& predicate) {
    discovery::Image image(fixture.View());
    for (const auto& [at, i] : image.Code(fixture.labels.at(function)).code) if (predicate(i)) return at;
    throw std::runtime_error("test instruction was not found");
}
void CheckOffsets(const DiscoveryFixture& fixture, const DiscoveryResult& r) {
    Check(r.offsets.camera_set_transform_rva == fixture.labels.at("transform"), "transform follows relocated code");
    Check(r.offsets.camera_system_return_a_rva == fixture.labels.at("return_a") &&
        r.offsets.camera_system_return_b_rva == fixture.labels.at("return_b"), "both presentation branches resolve");
    Check(r.offsets.camera_horizontal_fov == fixture.horizontal && r.offsets.camera_vertical_fov == fixture.vertical,
        "typed FOV fields follow the setter operands");
    Check(r.offsets.loading_screen_global_rva == fixture.labels.at("loading_global"), "loading singleton follows ownership");
    Check(r.offsets.matchmaking_client_global_rva == fixture.labels.at("client_global") &&
        r.offsets.matchmaking_client_vtable_rva == fixture.labels.at("client_vtable"), "client global and type resolve together");
    Check(r.offsets.client_session_manager == fixture.manager_member &&
        r.offsets.session_manager_session == fixture.session_member && r.offsets.session_manager_state == fixture.state_member &&
        r.offsets.session_manager_vtable_rva == fixture.labels.at("manager_vtable"), "entire co-op chain follows typed members");
    Check(r.session_idle == fixture.options.idle, "idle state follows both named comparisons");
    Check(r.camera_component_vtable == fixture.labels.at("component_vtable") &&
        r.camera_component_member == fixture.camera_member, "presentation and FOV share the constructed owner");
    Check(r.camera_size == 0x400 + fixture.options.fields && r.client_size == 0x200 + fixture.options.fields &&
        r.manager_size == 0x180 + fixture.options.fields, "allocation sites bound all fields");
}
}

int main() {
    DiscoveryFixture base;
    DiscoveryResult result;
    const bool baseline = DiscoverRuntime(base.View(), result);
    Check(baseline, "independent fixture resolves complete dependency set");
    if (!baseline) { std::printf("discovery: %s\n", result.error.c_str()); return rc_test::Summary("runtime discovery"); }
    CheckOffsets(base, result);
    auto highByte = base;
    highByte.bytes[highByte.labels.at("dummy")] = 0xb4;
    highByte.bytes[highByte.labels.at("dummy") + 1] = 1;
    discovery::Image highByteImage(highByte.View());
    discovery::Registers registers{};
    registers[0] = {discovery::Origin::Object, 0, 8};
    registers[4] = {discovery::Origin::Object, 64, 8};
    const auto written = discovery::Transfer(highByteImage.Decode(highByte.labels.at("dummy")), registers);
    Check(written[0].kind == discovery::Origin::Unknown && written[4] == registers[4],
        "writing AH invalidates the RAX pointer without confusing it with RSP");
    auto instruction = highByteImage.Decode(highByte.labels.at("loading_install"));
    Check(instruction.Rip(), "plain RIP-relative singleton access is recognized");
    instruction.h.p_67 = 0x67;
    Check(!instruction.Rip(), "address-size override cannot masquerade as a module global");
    instruction.h.p_67 = 0;
    instruction.h.p_seg = 0x65;
    Check(!instruction.Rip(), "segment-relative access cannot masquerade as a module global");
    DiscoveryFixture moved({0x2000, 0x28, 5, 0x7ff700000000ull, 3, false});
    Check(DiscoverRuntime(moved.View(), result), "moved code, data, fields, virtual slots, base and idle enum resolve");
    CheckOffsets(moved, result);
    Check(SelectProfile(moved.View()) == ProfileSelection::Matched, "unlisted fingerprint uses the real registry");
    Check(std::strcmp(ActiveProfile().Name, "unlisted-runtime-discovery") == 0, "registry reports the unlisted discovery route");
    Check(ActiveDiscovery().session_idle == 3, "registry publishes the discovered enum");

    auto chained = base;
    const auto split = chained.labels.at("return_a");
    for (unsigned n = 0; n < chained.functions.size(); ++n) {
        if (chained.functions[n].first != chained.labels.at("presenter")) continue;
        const auto end = chained.functions[n].second;
        const auto tail = chained.unwind + 0x1000;
        chained.bytes[tail] = 0x21;
        chained.Put<unsigned>(tail + 4, chained.functions[n].first);
        chained.Put<unsigned>(tail + 8, split);
        chained.Put<unsigned>(tail + 12, chained.unwind + n * 4);
        const auto count = static_cast<unsigned>(chained.functions.size());
        std::memmove(chained.bytes.data() + chained.pdata + (n + 2) * 12,
            chained.bytes.data() + chained.pdata + (n + 1) * 12, (count - n - 1) * 12);
        chained.Put<unsigned>(chained.pdata + n * 12 + 4, split);
        chained.Put<unsigned>(chained.pdata + (n + 1) * 12, split);
        chained.Put<unsigned>(chained.pdata + (n + 1) * 12 + 4, end);
        chained.Put<unsigned>(chained.pdata + (n + 1) * 12 + 8, tail);
        chained.Put<unsigned>(0x1a4, (count + 1) * 12);
        break;
    }
    Check(DiscoverRuntime(chained.View(), result), "presentation callers across chained unwind ranges resolve");
    CheckOffsets(chained, result);

    for (const char* anchor : {"bad_fov", "loading_name", "loading_category", "loading_dtor_name", "client_init_name",
        "manager_ctor_name", "session_null", "manager_null", "join_name", "join_code_name"}) {
        auto damaged = base; damaged.bytes[damaged.labels.at(anchor)] = '!'; Reject(damaged, "missing required named anchor rejects");
    }
    auto duplicate = base;
    const auto anchor = duplicate.labels.at("bad_fov");
    std::memcpy(duplicate.bytes.data() + 0x15000, duplicate.bytes.data() + anchor,
        std::strlen(reinterpret_cast<const char*>(duplicate.bytes.data() + anchor)) + 1);
    Reject(duplicate, "duplicate named anchor rejects");
    DiscoveryFixture ambiguous({0, 0, 0, 0x180000000ull, 0, true});
    Reject(ambiguous, "duplicate camera ABI rejects rather than taking the first");

    auto wrongOwner = base;
    const auto presenterLoad = InstructionAt(wrongOwner, "presenter", [](const auto& i) { return i.h.opcode == 0x8b && i.Base() == 12; });
    wrongOwner.Put<unsigned>(presenterLoad + 4, wrongOwner.camera_member + 8);
    Reject(wrongOwner, "presentation receiver from a different owner member rejects");
    auto wrongWidth = base;
    const auto store = InstructionAt(wrongWidth, "horizontal_fov", [](const auto& i) { return i.h.opcode == 0x0f && i.h.opcode2 == 0x11; });
    wrongWidth.bytes[store] = 0xf2;
    Reject(wrongWidth, "double FOV store cannot masquerade as float");
    auto wrongAspect = base;
    const auto divide = InstructionAt(wrongAspect, "vertical_fov", [](const auto& i) { return i.h.opcode == 0x0f && i.h.opcode2 == 0x5e; });
    wrongAspect.Put<unsigned>(divide + 4, wrongAspect.aspect + 4);
    Reject(wrongAspect, "reciprocal FOV calculations with different aspect members reject");
    auto undersized = base;
    const auto allocation = InstructionAt(undersized, "component_ctor", [](const auto& i) { return i.h.opcode == 0xb9; });
    undersized.Put<unsigned>(allocation + 1, 0x80);
    Reject(undersized, "FOV fields outside camera allocation reject");
    auto wrongManager = base;
    const auto nestedLoad = InstructionAt(wrongManager, "members", [](const auto& i) { return i.h.opcode == 0x8b && i.Base() == 1; });
    wrongManager.Put<unsigned>(nestedLoad + 3, wrongManager.manager_member + 8);
    Reject(wrongManager, "session belongs to a different manager member");
    auto wrongStateOwner = base;
    wrongStateOwner.Put<unsigned>(wrongStateOwner.labels.at("call_join") + 3, wrongStateOwner.manager_member + 8);
    Reject(wrongStateOwner, "session-state method called on a different manager rejects");
    auto wrongIdle = base; wrongIdle.Put<unsigned>(wrongIdle.labels.at("join_code") + 6, 7);
    Reject(wrongIdle, "disagreeing idle enum comparisons reject");
    auto wrongSessionWidth = base;
    const auto nested = InstructionAt(wrongSessionWidth, "members", [](const auto& i) { return i.h.opcode == 0x8b && i.Base() == 8; });
    wrongSessionWidth.bytes[nested] &= ~8u;
    Reject(wrongSessionWidth, "32-bit session pointer rejects");
    auto truncatedOwner = base;
    const auto ownerAddress = InstructionAt(truncatedOwner, "client_init", [](const auto& i) {
        return i.h.opcode == 0x8d && i.Base() == 12;
    });
    truncatedOwner.bytes[ownerAddress] &= ~8u;
    Reject(truncatedOwner, "32-bit address calculation cannot establish a 64-bit owner");
    auto wrongType = base;
    wrongType.Put<std::uint64_t>(wrongType.labels.at("component_vtable") + 8, wrongType.options.base + wrongType.labels.at("dummy"));
    Reject(wrongType, "FOV method on a different vtable rejects");
    auto malformed = base; malformed.Put<unsigned>(malformed.pdata + 4, static_cast<unsigned>(malformed.bytes.size()) + 1);
    Reject(malformed, "malformed function extent rejects");
    auto overlapping = base; overlapping.Put<unsigned>(0x208 + 40 + 12, 0x1000);
    Reject(overlapping, "overlapping PE sections reject");
    auto cyclic = base;
    cyclic.bytes[cyclic.unwind] = 0x21;
    cyclic.Put<unsigned>(cyclic.unwind + 4, cyclic.functions[0].first);
    cyclic.Put<unsigned>(cyclic.unwind + 8, cyclic.functions[0].second);
    cyclic.Put<unsigned>(cyclic.unwind + 12, cyclic.unwind);
    Reject(cyclic, "cyclic unwind chain rejects");
    auto truncated = base; truncated.bytes.resize(0x400);
    Reject(truncated, "truncated image rejects without an out-of-bounds read");
    auto wrongCopy = base;
    const auto copy = InstructionAt(wrongCopy, "transform", [](const auto& i) { return i.h.opcode == 0x89 && i.Base() == 12 && i.Width() == 4; });
    wrongCopy.Put<unsigned>(copy + 4, 0x3c);
    Reject(wrongCopy, "incorrect vector component output layout rejects");
    auto wrongStack = base;
    const auto argument = InstructionAt(wrongStack, "transform", [](const auto& i) { return i.h.opcode == 0x8b && i.Base() == 4; });
    wrongStack.Put<unsigned>(argument + 4, 120);
    Reject(wrongStack, "wrong fifth argument stack position rejects");

    Check(SelectProfile(base.View()) == ProfileSelection::Matched, "successful selection before rejection");
    Check(SelectProfile(wrongCopy.View()) == ProfileSelection::NoMatch, "real registry rejects changed ABI");
    Check(ActiveProfile().Name == nullptr && ActiveProfile().Offsets.camera_set_transform_rva == 0 &&
        ActiveDiscovery().camera_size == 0, "rejection leaves no stale active result");
    auto falselyKnown = base;
    falselyKnown.Put<unsigned>(0x108, kSteamProfile_20260911.Fingerprint.TimeDateStamp);
    falselyKnown.Put<unsigned>(0x158, kSteamProfile_20260911.Fingerprint.CheckSum);
    // Size is part of the fingerprint; an independent fixture cannot represent the historical image.
    Check(SelectProfile(falselyKnown.View()) == ProfileSelection::Matched, "partial fingerprint similarity does not select historical addresses");
    Check(ActiveProfile().Offsets.camera_set_transform_rva == base.labels.at("transform"), "registry retains discovered values");
    return rc_test::Summary("runtime discovery");
}
