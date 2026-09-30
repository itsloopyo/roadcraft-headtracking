// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "runtime_discovery.h"
#include "camera_contract.h"
#include "object_flow.h"
#include <iterator>

namespace rc_ht::builds {
namespace {
using namespace discovery;

template<class T> T Unique(const std::set<T>& values, const char* reason) {
    Require(values.size() == 1, reason); return *values.begin();
}
bool Object(Provenance v, std::int64_t offset = 0) { return v.kind == Origin::Object && v.value == offset; }
bool Calls(const Image& image, unsigned root, unsigned target) {
    for (const auto& [at, i] : image.Code(root).code)
        if ((i.Call() || i.Jump()) && i.Target() == target) return true;
    return false;
}
std::set<unsigned> Vtables(const Image& image, unsigned root) {
    const auto flow = ObjectFlow(image, root);
    std::set<unsigned> result;
    for (const auto& [at, i] : image.Code(root).code) {
        const auto& regs = flow.at(at);
        if (i.h.opcode != 0x89 || i.Width() != 8 || i.h.modrm_mod == 3 ||
            !Object(OperandAddress(i, regs))) continue;
        const auto source = regs[i.Reg()];
        if (source.kind != Origin::Address || !image.rdata.Contains(source.value, 8 * 3) || source.value % 8) continue;
        const auto table = static_cast<unsigned>(source.value);
        bool valid = true;
        for (unsigned n = 0; n < 3; ++n) valid &= image.text.Contains(image.Pointer(table + n * 8));
        if (valid) result.insert(table);
    }
    return result;
}
bool HasMethod(const Image& image, unsigned table, unsigned method) {
    for (unsigned n = 0; n < 256 && image.rdata.Contains(table + n * 8, 8); ++n) {
        const auto target = image.Pointer(table + n * 8);
        if (!image.text.Contains(target)) return false;
        if (target == method) return true;
    }
    return false;
}
std::set<std::pair<unsigned, unsigned>> Owners(const Image& image, unsigned method) {
    std::set<unsigned> roots;
    for (auto slot : image.Pointers(method)) {
        const auto lower = slot > 2048 ? std::max(image.rdata.begin, slot - 2048) : image.rdata.begin;
        const auto refs = image.ReferencingRange({lower, slot + 8});
        roots.insert(refs.begin(), refs.end());
    }
    std::set<std::pair<unsigned, unsigned>> result;
    for (auto root : roots) {
        if (!image.Code(root).complete) continue;
        for (auto table : Vtables(image, root)) if (HasMethod(image, table, method)) result.emplace(root, table);
    }
    return result;
}
unsigned AllocationSize(const Image& image, unsigned caller, unsigned constructor) {
    const auto flow = ObjectFlow(image, caller);
    std::set<unsigned> sizes;
    for (const auto& [at, i] : image.Code(caller).code) {
        const auto receiver = flow.at(at)[1];
        if (!i.Call() || i.Target() != constructor || receiver.kind != Origin::Result) continue;
        std::vector<Instruction> earlier;
        for (const auto& [pos, previous] : image.Code(caller).code)
            if (pos < at && at - pos <= 64 && previous.Call()) earlier.push_back(previous);
        if (earlier.empty() || earlier.back().Target() != receiver.value) continue;
        const auto size = flow.at(earlier.back().at)[1];
        if (size.kind == Origin::Constant && size.value >= 64 && size.value <= 0x100000)
            sizes.insert(static_cast<unsigned>(size.value));
    }
    return Unique(sizes, "missing or ambiguous object allocation size");
}
bool HasStore(const Image& image, unsigned root, unsigned field, unsigned width) {
    const auto flow = ObjectFlow(image, root);
    for (const auto& [at, i] : image.Code(root).code) {
        if (i.h.modrm_mod == 3 || !Object(OperandAddress(i, flow.at(at)), field)) continue;
        if ((i.h.opcode == 0x89 || i.h.opcode == 0xc7) && i.Width() == width) return true;
        if (i.h.opcode == 0x0f && i.h.opcode2 == 0x11 && i.h.p_rep == 0xf3 && width == 4) return true;
    }
    return false;
}
void Field(unsigned field, unsigned width, unsigned size) {
    Require(field >= 8 && field % width == 0 && size >= width && field <= size - width,
        "field width, alignment or containing object bound failed");
}

void Camera(const Image& image, DiscoveryResult& out) {
    std::map<unsigned, TransformContract> transforms;
    std::map<unsigned, FovContract> fovs;
    for (const auto& [root, ranges] : image.Functions()) {
        unsigned size = 0; for (auto r : ranges) size += r.end - r.begin;
        if (size > 512) continue;
        TransformContract transform; FovContract fov;
        if (InspectTransform(image, root, transform)) transforms.emplace(root, transform);
        if (InspectFov(image, root, fov)) fovs.emplace(root, fov);
    }
    Require(transforms.size() == 1, "missing or ambiguous camera transform ABI");
    const auto transform = transforms.begin()->first;
    const auto contract = transforms.begin()->second;
    std::set<std::pair<unsigned, unsigned>> pairs;
    for (const auto& [h, horizontal] : fovs) for (const auto& [v, vertical] : fovs) {
        if (horizontal.horizontal && !vertical.horizontal && horizontal.input == vertical.output &&
            horizontal.output == vertical.input && horizontal.aspect == vertical.aspect &&
            horizontal.update == vertical.update && horizontal.tangent == vertical.tangent &&
            horizontal.arctangent == vertical.arctangent) pairs.emplace(h, v);
    }
    const auto [horizontal, vertical] = Unique(pairs, "missing or ambiguous reciprocal FOV setters");
    const auto fov = fovs.at(horizontal);
    std::set<std::pair<unsigned, unsigned>> presenters;
    std::map<unsigned, std::vector<unsigned>> returns;
    for (auto root : image.Referencing(transform)) {
        if (!image.Code(root).complete) continue;
        const auto flow = ObjectFlow(image, root);
        std::set<unsigned> members;
        std::vector<unsigned> sites;
        bool valid = true;
        for (const auto& [at, i] : image.Code(root).code) if (i.Call() && i.Target() == transform) {
            const auto receiver = flow.at(at)[1];
            if (receiver.kind != Origin::Member || receiver.width != 8 || receiver.value < 8 || receiver.value % 8) { valid = false; break; }
            members.insert(static_cast<unsigned>(receiver.value)); sites.push_back(i.next);
        }
        if (valid && sites.size() == 2 && members.size() == 1) {
            presenters.emplace(root, *members.begin()); returns.emplace(root, std::move(sites));
        }
    }
    const auto [presenter, cameraMember] = Unique(presenters, "missing or ambiguous presentation callers");
    std::set<unsigned> fovMethods;
    for (auto root : image.Referencing(horizontal)) {
        if (!image.Code(root).complete || !Calls(image, root, vertical)) continue;
        const auto flow = ObjectFlow(image, root);
        bool valid = true; unsigned count = 0;
        for (const auto& [at, i] : image.Code(root).code)
            if (i.Call() && (i.Target() == horizontal || i.Target() == vertical)) {
                const auto receiver = flow.at(at)[1];
                valid &= receiver.kind == Origin::Member && receiver.width == 8 && receiver.value == cameraMember;
                ++count;
            }
        if (valid && count == 2) fovMethods.insert(root);
    }
    const auto fovMethod = Unique(fovMethods, "FOV setters do not share the presentation camera owner");
    const auto namedSetters = image.Named("Invalid fov passed to camera: %.4f");
    std::set<std::pair<unsigned, unsigned>> constructors;
    std::map<unsigned, unsigned> cameraConstructors;
    for (const auto& [root, table] : Owners(image, presenter)) {
        if (!HasMethod(image, table, fovMethod)) continue;
        bool named = false;
        for (auto setter : namedSetters) named |= HasMethod(image, table, setter);
        if (!named) continue;
        const auto flow = ObjectFlow(image, root);
        for (const auto& [at, i] : image.Code(root).code) {
            if (i.h.opcode != 0x89 || i.Width() != 8 || i.h.modrm_mod == 3 ||
                !Object(OperandAddress(i, flow.at(at)), cameraMember)) continue;
            const auto source = flow.at(at)[i.Reg()];
            if (source.kind != Origin::Result || !image.text.Contains(source.value)) continue;
            const auto child = static_cast<unsigned>(source.value);
            if (!Calls(image, child, fov.update) || !Calls(image, child, contract.update) ||
                !Calls(image, child, contract.initialize) ||
                !HasStore(image, child, fov.input, 4) || !HasStore(image, child, fov.output, 4) ||
                !HasStore(image, child, fov.aspect, 4)) continue;
            constructors.emplace(root, table); cameraConstructors[root] = child;
        }
    }
    const auto [constructor, table] = Unique(constructors, "camera allocation/vtable/FOV ownership did not converge");
    out.camera_size = AllocationSize(image, constructor, cameraConstructors.at(constructor));
    out.camera_aspect = fov.aspect;
    Field(fov.input, 4, out.camera_size); Field(fov.output, 4, out.camera_size); Field(fov.aspect, 4, out.camera_size);
    out.camera_component_vtable = table; out.camera_component_member = cameraMember;
    out.offsets.camera_set_transform_rva = transform;
    out.offsets.camera_system_return_a_rva = returns.at(presenter)[0];
    out.offsets.camera_system_return_b_rva = returns.at(presenter)[1];
    out.offsets.camera_horizontal_fov = fov.input; out.offsets.camera_vertical_fov = fov.output;
}

void Loading(const Image& image, DiscoveryResult& out) {
    const auto destructor = Unique(image.Named("UI Loading Screen destructor was called without calling Terminate method first!"),
        "loading-screen destructor identity is ambiguous");
    const auto categories = image.Named("ui/system/loading_screen");
    Require(categories.count(destructor) != 0, "loading-screen diagnostic owner mismatch");
    const auto flow = ObjectFlow(image, destructor);
    std::set<unsigned> globals;
    for (const auto& [at, i] : image.Code(destructor).code) {
        if (i.h.opcode == 0x39 && i.Width() == 8 && i.Rip() && image.data.Contains(i.RipTarget(), 8) &&
            flow.at(at)[i.Reg()].kind == Origin::Object) globals.insert(static_cast<unsigned>(i.RipTarget()));
    }
    const auto global = Unique(globals, "loading-screen ownership comparison is missing or ambiguous");
    Require(global % 8 == 0, "unaligned loading-screen global");
    std::set<unsigned> install, clear;
    for (auto root : image.Referencing(global)) {
        if (!image.Code(root).complete) continue;
        const auto state = ObjectFlow(image, root);
        for (const auto& [at, i] : image.Code(root).code) {
            if (!i.Rip() || i.RipTarget() != global || i.Width() != 8) continue;
            if (i.h.opcode == 0x89 && Object(state.at(at)[i.Reg()])) install.insert(root);
            if (i.h.opcode == 0xc7 && i.Immediate() == 0) clear.insert(root);
        }
    }
    Unique(install, "loading-screen singleton installer is missing or ambiguous");
    Require(Calls(image, destructor, Unique(clear, "loading-screen singleton clear is missing or ambiguous")),
        "loading-screen destructor does not clear the owned singleton");
    std::set<unsigned> callbacks;
    for (auto root : image.Named("UiLoadingScreen")) {
        for (const auto& [at, i] : image.Code(root).code) {
            if (i.h.opcode != 0x8d || !i.Rip() || !image.text.Contains(i.RipTarget())) continue;
            const auto callback = static_cast<unsigned>(i.RipTarget());
            if (image.Root(callback) != callback || !image.Code(callback).complete) continue;
            for (const auto& [pos, ins] : image.Code(callback).code)
                if (ins.h.opcode == 0x8b && ins.Width() == 8 && ins.Rip() && ins.RipTarget() == global)
                    callbacks.insert(callback);
        }
    }
    Require(callbacks.size() == 2, "named loading-screen callbacks disagree with singleton ownership");
    out.offsets.loading_screen_global_rva = global;
}

void Multiplayer(const Image& image, DiscoveryResult& out) {
    const auto initialize = Unique(image.Named("hydra_mm/mm_client/init"), "matchmaking initializer identity is ambiguous");
    const auto managerCtor = Unique(image.Named("Initial JIP set to: [%i]; Join set to: [%i]"), "session-manager constructor identity is ambiguous");
    Require(Calls(image, initialize, managerCtor), "matchmaking does not construct the named session manager");
    const auto managerVtable = Unique(Vtables(image, managerCtor), "session-manager primary vtable is ambiguous");
    const auto initFlow = ObjectFlow(image, initialize);
    std::set<unsigned> managerMembers;
    for (const auto& [at, i] : image.Code(initialize).code) {
        const auto& regs = initFlow.at(at);
        if (i.Call() && regs[1].kind == Origin::Object && regs[1].value > 0 &&
            regs[2].kind == Origin::Result && regs[2].value == managerCtor) {
            const auto assign = static_cast<unsigned>(i.Target());
            const auto assignment = ObjectFlow(image, assign);
            for (const auto& [pos, instruction] : image.Code(assign).code) {
                if (instruction.h.opcode != 0x89 || instruction.Width() != 8 || instruction.Base() < 0 || instruction.Disp() != 0) continue;
                const auto& values = assignment.at(pos);
                const auto owner = values[instruction.Base()], value = values[instruction.Reg()];
                if (owner.kind == Origin::Member && owner.value == 0 && owner.width == 8 &&
                    value.kind == Origin::Address && value.value == managerVtable)
                    managerMembers.insert(static_cast<unsigned>(regs[1].value));
            }
        }
    }
    const auto managerMember = Unique(managerMembers, "session-manager ownership member is ambiguous");
    out.manager_size = AllocationSize(image, initialize, managerCtor);
    std::set<unsigned> clientGlobals;
    for (auto root : image.Referencing(initialize)) {
        if (!image.Code(root).complete) continue;
        const auto state = ObjectFlow(image, root);
        for (const auto& [at, i] : image.Code(root).code) {
            const auto receiver = state.at(at)[1];
            if (i.Call() && i.Target() == initialize && receiver.kind == Origin::Global && receiver.width == 8 &&
                image.data.Contains(receiver.value, 8)) clientGlobals.insert(static_cast<unsigned>(receiver.value));
        }
    }
    const auto clientGlobal = Unique(clientGlobals, "named matchmaking initializer has no unique client global");
    Require(clientGlobal % 8 == 0, "unaligned client global");
    std::set<unsigned> clientCtors;
    for (auto root : image.Referencing(clientGlobal)) {
        if (!image.Code(root).complete) continue;
        const auto state = ObjectFlow(image, root);
        for (const auto& [at, i] : image.Code(root).code)
            if (i.h.opcode == 0x89 && i.Width() == 8 && i.Rip() && i.RipTarget() == clientGlobal &&
                Object(state.at(at)[i.Reg()])) clientCtors.insert(root);
    }
    const auto clientCtor = Unique(clientCtors, "client constructor/global ownership is ambiguous");
    const auto clientVtable = Unique(Vtables(image, clientCtor), "client primary vtable is ambiguous");
    Require(HasStore(image, clientCtor, managerMember, 8), "client constructor does not initialize the manager member");
    const auto clientFactory = Unique(image.Referencing(clientCtor), "client allocation route is ambiguous");
    out.client_size = AllocationSize(image, clientFactory, clientCtor);
    Field(managerMember, 8, out.client_size);
    const auto members = Unique(image.Named("GetMatchMembers() returns empty list because gameSession is null"),
        "session ownership diagnostic is ambiguous");
    Require(image.Named("GetMatchMembers() returns empty list because gameSessionMgr is null").count(members),
        "session ownership diagnostics have different owners");
    Require(HasMethod(image, clientVtable, members), "session query does not belong to the client vtable");
    const auto memberFlow = ObjectFlow(image, members);
    std::set<unsigned> sessionMembers;
    for (const auto& [at, i] : image.Code(members).code) {
        if (i.h.opcode != 0x8b || i.Width() != 8) continue;
        const auto value = LoadOperand(i, memberFlow.at(at), 8);
        if (value.kind == Origin::NestedMember && static_cast<unsigned>(value.value >> 32) == managerMember)
            sessionMembers.insert(static_cast<unsigned>(value.value & 0xffffffff));
    }
    const auto session = Unique(sessionMembers, "session pointer does not belong to the named manager");
    Field(session, 8, out.manager_size);
    Require(HasStore(image, managerCtor, session, 8), "session constructor pointer width disagrees");
    std::set<std::pair<unsigned, std::int32_t>> states;
    bool first = true;
    for (const auto name : {"JoinGameSession is not possible in state '%d'", "JoinBySessionCode is not possible in state '%d'"}) {
        const auto root = Unique(image.Named(name), "session-state diagnostic is ambiguous");
        bool owned = false;
        for (auto caller : image.Referencing(root)) {
            if (!image.Code(caller).complete || !HasMethod(image, clientVtable, caller)) continue;
            const auto callerFlow = ObjectFlow(image, caller);
            for (const auto& [at, i] : image.Code(caller).code) {
                const auto receiver = callerFlow.at(at)[1];
                if (i.Call() && i.Target() == root && receiver.kind == Origin::Member &&
                    receiver.width == 8 && receiver.value == managerMember) owned = true;
            }
        }
        Require(owned, "session-state method does not belong to the client's manager");
        const auto state = ObjectFlow(image, root);
        std::set<std::pair<unsigned, std::int32_t>> candidates;
        for (const auto& [at, i] : image.Code(root).code) {
            const auto address = OperandAddress(i, state.at(at));
            if ((i.h.opcode == 0x83 || i.h.opcode == 0x81) && i.h.modrm_reg == 7 && i.Width() == 4 &&
                address.kind == Origin::Object && address.value >= 8 && address.value < out.manager_size)
                candidates.emplace(static_cast<unsigned>(address.value), static_cast<std::int32_t>(i.Immediate()));
        }
        if (first) { states = candidates; first = false; }
        else {
            std::set<std::pair<unsigned, std::int32_t>> intersection;
            std::set_intersection(states.begin(), states.end(), candidates.begin(), candidates.end(),
                std::inserter(intersection, intersection.begin())); states = std::move(intersection);
        }
    }
    const auto [state, idle] = Unique(states, "session idle-state comparisons disagree");
    Field(state, 4, out.manager_size);
    Require(HasStore(image, managerCtor, state, 4), "session state constructor width disagrees");
    out.offsets.matchmaking_client_global_rva = clientGlobal;
    out.offsets.matchmaking_client_vtable_rva = clientVtable;
    out.offsets.client_session_manager = managerMember;
    out.offsets.session_manager_vtable_rva = managerVtable;
    out.offsets.session_manager_session = session;
    out.offsets.session_manager_state = state;
    out.session_idle = idle;
}
}

bool DiscoverRuntime(ImageView view, DiscoveryResult& result) {
    result = {};
    try {
        Image image(view);
        DiscoveryResult resolved;
        Camera(image, resolved);
        Loading(image, resolved);
        Multiplayer(image, resolved);
        result = std::move(resolved);
        return true;
    } catch (const discovery::Rejected& error) {
        result.error = error.what();
        return false;
    }
}

}
