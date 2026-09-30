// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once
#include "builds/runtime_discovery.h"
#include <array>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace rc_test {

struct DiscoveryFixture {
    struct Options {
        unsigned shift = 0;
        unsigned fields = 0;
        unsigned slots = 0;
        std::uint64_t base = 0x180000000ull;
        int idle = 0;
        bool duplicate_transform = false;
    };
    struct Fixup { unsigned at, next; std::string name; bool pointer; };
    std::vector<std::uint8_t> bytes;
    std::map<std::string, unsigned> labels;
    std::vector<Fixup> fixups;
    std::vector<std::pair<unsigned, unsigned>> functions;
    Options options;
    unsigned cursor, strings, pdata, unwind;
    unsigned horizontal, vertical, aspect, camera_member, manager_member, session_member, state_member;

    template<class T> void Put(unsigned at, T value) { std::memcpy(bytes.data() + at, &value, sizeof(value)); }
    void Emit(std::initializer_list<unsigned> code) { for (auto b : code) bytes.at(cursor++) = static_cast<std::uint8_t>(b); }
    void Dword(unsigned value) { Put(cursor, value); cursor += 4; }
    void Rip(std::initializer_list<unsigned> prefix, const std::string& label) {
        Emit(prefix); const auto at = cursor; Dword(0); fixups.push_back({at, cursor, label, false});
    }
    void Call(const std::string& label) { Rip({0xe8}, label); }
    void Tail(const std::string& label) { Rip({0xe9}, label); }
    void Begin(const std::string& label) { cursor = (cursor + 15) & ~15u; labels[label] = cursor; }
    void End(const std::string& label) { functions.emplace_back(labels.at(label), cursor); }
    void String(const std::string& label, const char* value) {
        labels[label] = strings;
        const auto size = std::strlen(value) + 1;
        std::memcpy(bytes.data() + strings, value, size); strings += static_cast<unsigned>(size) + 1;
    }
    void Pointer(unsigned at, const std::string& label) { fixups.push_back({at, 0, label, true}); }
    void Push(unsigned r) { if (r >= 8) Emit({0x41}); Emit({0x50 + (r & 7)}); }
    void Pop(unsigned r) { if (r >= 8) Emit({0x41}); Emit({0x58 + (r & 7)}); }
    void Mov(unsigned dst, unsigned src) { Emit({0x48u | (dst >= 8 ? 4u : 0u) | (src >= 8 ? 1u : 0u), 0x8b, 0xc0u | ((dst & 7) << 3) | (src & 7)}); }
    void Load(unsigned dst, unsigned base, unsigned displacement, bool wide = true) {
        Emit({(wide ? 0x48u : 0x40u) | (dst >= 8 ? 4u : 0u) | (base >= 8 ? 1u : 0u),
            0x8b, 0x80u | ((dst & 7) << 3) | (base & 7)});
        if ((base & 7) == 4) Emit({0x24});
        Dword(displacement);
    }
    void Store(unsigned base, unsigned displacement, unsigned source, bool wide = true) {
        Emit({(wide ? 0x48u : 0x40u) | (source >= 8 ? 4u : 0u) | (base >= 8 ? 1u : 0u),
            0x89, 0x80u | ((source & 7) << 3) | (base & 7)});
        if ((base & 7) == 4) Emit({0x24});
        Dword(displacement);
    }
    void Immediate(unsigned base, unsigned displacement, unsigned value, bool wide = false) {
        Emit({(wide ? 0x48u : 0x40u) | (base >= 8 ? 1u : 0u), 0xc7, 0x80u | (base & 7)});
        if ((base & 7) == 4) Emit({0x24});
        Dword(displacement); Dword(value);
    }
    void Lea(unsigned dest, unsigned base, unsigned displacement) {
        Emit({0x48u | (dest >= 8 ? 4u : 0u) | (base >= 8 ? 1u : 0u), 0x8d,
            0x80u | ((dest & 7) << 3) | (base & 7)});
        if ((base & 7) == 4) Emit({0x24});
        Dword(displacement);
    }
    void Prologue() { Push(12); Emit({0x48, 0x83, 0xec, 0x20}); Mov(12, 1); }
    void Epilogue() { Emit({0x48, 0x83, 0xc4, 0x20}); Pop(12); Emit({0xc3}); }
    void Allocation(unsigned size, const std::string& constructor) {
        Emit({0xb9}); Dword(size); Call("allocate"); Mov(1, 0); Call(constructor);
    }
    void Table(const std::string& name, const std::vector<std::string>& methods) {
        strings = (strings + 7) & ~7u;
        labels[name] = strings;
        for (const auto& method : methods) { Pointer(strings, method); strings += 8; }
        strings += 16;
    }
    void SetTable(const std::string& table) { Rip({0x48, 0x8d, 0x05}, table); Store(12, 0, 0); }
    void Transform(const std::string& name) {
        Begin(name);
        for (unsigned r : {12u, 13u, 14u, 15u}) Push(r);
        Emit({0x48, 0x83, 0xec, 0x28});
        Mov(12, 1); Mov(13, 2); Mov(14, 8); Mov(15, 9);
        Call("identity");
        Load(11, 4, 112);
        for (unsigned axis : {8u, 0u, 4u}) {
            Load(0, 13, axis, false); Store(12, 48 + axis, 0, false);
            Load(0, 11, axis, false); Store(12, 32 + axis, 0, false);
            Load(0, 14, axis, false); Store(12, 16 + axis, 0, false);
            Load(0, 15, axis, false); Store(12, axis, 0, false);
        }
        Mov(1, 12); Emit({0x48, 0x83, 0xc4, 0x28});
        for (unsigned r : {15u, 14u, 13u, 12u}) Pop(r);
        Tail("view_update"); End(name);
    }
    void Fov(const std::string& name, bool across) {
        Begin(name); Push(7); Emit({0x48, 0x83, 0xec, 0x20}); Mov(7, 1);
        Emit({0xf3, 0x0f, 0x11, 0x8f}); Dword(across ? horizontal : vertical);
        Emit({0x0f, 0x28, 0xc1}); Rip({0xf3, 0x0f, 0x59, 0x05}, "radians"); Call("tangent");
        Emit({0xf3, 0x0f, across ? 0x59u : 0x5eu, 0x87}); Dword(aspect);
        Call("arctangent"); Rip({0xf3, 0x0f, 0x59, 0x05}, "degrees");
        Emit({0xf3, 0x0f, 0x11, 0x87}); Dword(across ? vertical : horizontal);
        Mov(1, 7); Emit({0x48, 0x83, 0xc4, 0x20}); Pop(7); Tail("projection"); End(name);
    }
    DiscoveryFixture() : DiscoveryFixture(Options{}) {}
    explicit DiscoveryFixture(Options settings) : bytes(0x30000 + settings.shift), options(settings),
        cursor(0x1000 + settings.shift), strings(0xc000 + settings.shift),
        pdata(0x18000 + settings.shift), unwind(0x21000 + settings.shift),
        horizontal(0x180 + settings.fields), vertical(0x188 + settings.fields), aspect(0x190 + settings.fields),
        camera_member(0x38 + settings.fields), manager_member(0x58 + settings.fields),
        session_member(0x48 + settings.fields), state_member(0x98 + settings.fields) {
        Put<std::uint16_t>(0, 0x5a4d); Put<unsigned>(0x3c, 0x100); Put<unsigned>(0x100, 0x4550);
        Put<std::uint16_t>(0x104, 0x8664); Put<std::uint16_t>(0x106, 5); Put<std::uint16_t>(0x114, 240);
        Put<std::uint16_t>(0x118, 0x20b); Put<std::uint64_t>(0x130, options.base);
        Put<unsigned>(0x150, static_cast<unsigned>(bytes.size()));
        unsigned section = 0x208;
        for (const auto& entry : std::vector<std::pair<std::string, std::array<unsigned, 3>>>{
            {".text", {0x1000, 0xb000, 0x60000000}}, {".rdata", {0xc000, 0xa000, 0x40000000}},
            {".data", {0x16000, 0x2000, 0xc0000000}}, {".pdata", {0x18000, 0x8000, 0x40000000}},
            {".xdata", {0x20000, 0x10000, 0x40000000}}}) {
            std::memcpy(bytes.data() + section, entry.first.c_str(), entry.first.size());
            Put<unsigned>(section + 8, entry.second[1]); Put<unsigned>(section + 12, entry.second[0] + options.shift);
            Put<unsigned>(section + 36, entry.second[2]); section += 40;
        }
        String("bad_fov", "Invalid fov passed to camera: %.4f");
        String("loading_dtor_name", "UI Loading Screen destructor was called without calling Terminate method first!");
        String("loading_category", "ui/system/loading_screen"); String("loading_name", "UiLoadingScreen");
        String("client_init_name", "hydra_mm/mm_client/init");
        String("manager_ctor_name", "Initial JIP set to: [%i]; Join set to: [%i]");
        String("session_null", "GetMatchMembers() returns empty list because gameSession is null");
        String("manager_null", "GetMatchMembers() returns empty list because gameSessionMgr is null");
        String("join_name", "JoinGameSession is not possible in state '%d'");
        String("join_code_name", "JoinBySessionCode is not possible in state '%d'");
        strings = (strings + 7) & ~7u;
        labels["radians"] = strings; Put<float>(strings, 0.00872664626f); strings += 8;
        labels["degrees"] = strings; Put<float>(strings, 114.591559f); strings += 8;
        labels["loading_global"] = 0x16000 + options.shift;
        labels["client_global"] = 0x16010 + options.shift;
        for (const std::string name : {"allocate", "identity", "view_update", "projection", "tangent", "arctangent", "dummy"}) {
            Begin(name); Emit({0xc3}); End(name);
        }
        Transform("transform"); if (options.duplicate_transform) Transform("duplicate_transform");
        Fov("horizontal_fov", true); Fov("vertical_fov", false);
        Begin("presenter"); Prologue();
        for (unsigned n = 0; n < 2; ++n) { Load(1, 12, camera_member); Call("transform"); labels[n ? "return_b" : "return_a"] = cursor; }
        Epilogue(); End("presenter");
        Begin("fov_method"); Prologue();
        Load(1, 12, camera_member); Call("vertical_fov"); Load(1, 12, camera_member); Call("horizontal_fov");
        Epilogue(); End("fov_method");
        Begin("named_fov"); Rip({0x48, 0x8d, 0x15}, "bad_fov"); Emit({0xc3}); End("named_fov");
        Begin("camera_ctor"); Prologue();
        Immediate(12, horizontal, 0x42a00000); Immediate(12, vertical, 0x42700000); Immediate(12, aspect, 0x3f400000);
        Mov(1, 12); Call("projection"); Mov(1, 12); Call("view_update"); Mov(1, 12); Call("identity");
        Mov(0, 12); Epilogue(); End("camera_ctor");
        Begin("component_ctor"); Prologue(); SetTable("component_vtable");
        Allocation(0x400 + options.fields, "camera_ctor"); Store(12, camera_member, 0); Epilogue(); End("component_ctor");
        std::vector<std::string> methods(options.slots, "dummy");
        methods.insert(methods.end(), {"presenter", "fov_method", "named_fov"}); Table("component_vtable", methods);
        Begin("loading_install"); Rip({0x48, 0x89, 0x0d}, "loading_global"); Emit({0xc3}); End("loading_install");
        Begin("loading_clear"); Rip({0x48, 0xc7, 0x05}, "loading_global"); Dword(0); fixups.back().next = cursor;
        Emit({0xc3}); End("loading_clear");
        Begin("loading_dtor"); Rip({0x48, 0x39, 0x0d}, "loading_global");
        Rip({0x48, 0x8d, 0x15}, "loading_category"); Rip({0x4c, 0x8d, 0x05}, "loading_dtor_name");
        Call("loading_clear"); Emit({0xc3}); End("loading_dtor");
        for (const std::string name : {"loading_callback_a", "loading_callback_b"}) {
            Begin(name); Rip({0x48, 0x8b, 0x05}, "loading_global"); Emit({0xc3}); End(name);
        }
        Begin("loading_registration"); Rip({0x48, 0x8d, 0x05}, "loading_name");
        Rip({0x48, 0x8d, 0x15}, "loading_callback_a"); Rip({0x4c, 0x8d, 0x05}, "loading_callback_b");
        Emit({0xc3}); End("loading_registration");
        Table("manager_vtable", {"dummy", "dummy", "dummy"});
        Begin("manager_ctor"); Prologue(); SetTable("manager_vtable");
        Immediate(12, session_member, 0, true); Immediate(12, state_member, options.idle);
        Rip({0x48, 0x8d, 0x15}, "manager_ctor_name"); Mov(0, 12); Epilogue(); End("manager_ctor");
        Begin("manager_assign"); Load(0, 1, 0); Rip({0x4c, 0x8d, 0x05}, "manager_vtable");
        Store(0, 0, 8); Store(1, 0, 2); Emit({0xc3}); End("manager_assign");
        Begin("client_init"); Prologue(); Rip({0x48, 0x8d, 0x15}, "client_init_name");
        Allocation(0x180 + options.fields, "manager_ctor"); Mov(2, 0); Lea(1, 12, manager_member);
        Call("manager_assign"); Epilogue(); End("client_init");
        Begin("global_client_init"); Rip({0x48, 0x8b, 0x0d}, "client_global"); Call("client_init"); Emit({0xc3}); End("global_client_init");
        Table("client_vtable", {"members", "call_join", "call_join_code"});
        Begin("client_ctor"); Prologue(); SetTable("client_vtable"); Immediate(12, manager_member, 0, true);
        Rip({0x4c, 0x89, 0x25}, "client_global"); Mov(0, 12); Epilogue(); End("client_ctor");
        Begin("client_factory"); Prologue(); Allocation(0x200 + options.fields, "client_ctor"); Epilogue(); End("client_factory");
        Begin("members"); Rip({0x48, 0x8d, 0x15}, "session_null"); Rip({0x4c, 0x8d, 0x05}, "manager_null");
        Load(8, 1, manager_member); Load(0, 8, session_member); Emit({0xc3}); End("members");
        for (const std::string name : {"join", "join_code"}) {
            Begin("call_" + name); Load(1, 1, manager_member); Call(name); Emit({0xc3}); End("call_" + name);
            Begin(name); Emit({0x81, 0xb9}); Dword(state_member); Dword(options.idle);
            Rip({0x48, 0x8d, 0x15}, name + "_name"); Emit({0xc3}); End(name);
        }
        for (const auto& fix : fixups) {
            if (fix.pointer) Put<std::uint64_t>(fix.at, options.base + labels.at(fix.name));
            else Put<std::int32_t>(fix.at, static_cast<std::int32_t>(labels.at(fix.name)) - static_cast<std::int32_t>(fix.next));
        }
        for (unsigned n = 0; n < functions.size(); ++n) {
            Put<unsigned>(pdata + n * 12, functions[n].first); Put<unsigned>(pdata + n * 12 + 4, functions[n].second);
            Put<unsigned>(pdata + n * 12 + 8, unwind + n * 4); bytes[unwind + n * 4] = 1;
        }
        Put<unsigned>(0x1a0, pdata); Put<unsigned>(0x1a4, static_cast<unsigned>(functions.size()) * 12);
    }
    rc_ht::builds::ImageView View() const { return {bytes.data(), bytes.size(), options.base}; }
};

}
