// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once
#include "discovery_image.h"
#include <deque>

namespace rc_ht::builds::discovery {

enum class Origin { Unknown, Object, Address, Member, Global, Constant, Result, NestedMember };
struct Provenance {
    Origin kind = Origin::Unknown;
    std::int64_t value = 0;
    unsigned width = 0;
    bool operator==(const Provenance& other) const {
        return kind == other.kind && value == other.value && width == other.width;
    }
};
using Registers = std::array<Provenance, 16>;
inline Provenance OperandAddress(const Instruction& i, const Registers& regs) {
    if (i.h.p_seg || i.h.p_67) return {};
    if (i.Rip()) return {Origin::Address, i.RipTarget(), 8};
    if (i.Base() < 0) return {};
    auto value = regs[i.Base()];
    if (value.kind != Origin::Object && value.kind != Origin::Address) return {};
    value.value += i.Disp(); return value;
}
inline Provenance LoadOperand(const Instruction& i, const Registers& regs, unsigned width) {
    if (i.h.modrm_mod == 3) return width == 8 ? regs[i.Rm()] : Provenance{};
    if (!i.h.p_seg && !i.h.p_67 && i.Base() >= 0 && regs[i.Base()].kind == Origin::Member &&
        regs[i.Base()].width == 8 && regs[i.Base()].value >= 0 && regs[i.Base()].value < 0x10000 &&
        i.Disp() >= 0 && i.Disp() < 0x10000)
        return {Origin::NestedMember, (regs[i.Base()].value << 32) | i.Disp(), width};
    const auto address = OperandAddress(i, regs);
    if (address.kind == Origin::Object) return {Origin::Member, address.value, width};
    if (address.kind == Origin::Address) return {Origin::Global, address.value, width};
    return {};
}
inline Registers Transfer(const Instruction& i, Registers regs) {
    const auto op = i.h.opcode, op2 = i.h.opcode2;
    const auto r = i.Reg(), m = i.Rm();
    const auto byteRegister = [&](unsigned reg) { return !i.h.rex && reg >= 4 && reg < 8 ? reg - 4 : reg; };
    if (i.Call() || (op == 0xff && (i.h.modrm_reg == 2 || i.h.modrm_reg == 3))) {
        for (unsigned v : {0u, 1u, 2u, 8u, 9u, 10u, 11u}) regs[v] = {};
        if (i.Call()) regs[0] = {Origin::Result, i.Target(), 8};
    } else if (op == 0x8b || op == 0x8a || op == 0x63 ||
        (op == 0x0f && (op2 == 0xb6 || op2 == 0xb7 || op2 == 0xbe || op2 == 0xbf))) {
        unsigned width = i.Width();
        if (op == 0x8a || (op == 0x0f && (op2 == 0xb6 || op2 == 0xbe))) width = 1;
        if (op == 0x0f && (op2 == 0xb7 || op2 == 0xbf)) width = 2;
        if (op == 0x63) width = 4;
        regs[op == 0x8a ? byteRegister(r) : r] = LoadOperand(i, regs, width);
    } else if (op == 0x89 || op == 0x88) {
        if (i.h.modrm_mod == 3) regs[op == 0x88 ? byteRegister(m) : m] = i.Width() == 8 && op == 0x89 ? regs[r] : Provenance{};
    } else if (op == 0x8d) {
        auto value = OperandAddress(i, regs);
        if (value.kind == Origin::Unknown && i.Base() >= 0 && regs[i.Base()].kind == Origin::Constant)
            value = {Origin::Constant, regs[i.Base()].value + i.Disp(), i.Width()};
        regs[r] = i.Width() == 8 ? value : Provenance{};
    } else if (op >= 0xb8 && op <= 0xbf) {
        regs[op - 0xb8 + 8 * i.h.rex_b] = {Origin::Constant,
            i.h.rex_w ? static_cast<std::int64_t>(i.h.imm.imm64) : i.h.imm.imm32, i.Width()};
    } else if ((op == 0x33 || op == 0x31) && i.h.modrm_mod == 3 && r == m) {
        regs[r] = {Origin::Constant, 0, i.Width()};
    } else if ((op == 0x80 || op == 0x81 || op == 0x83) && i.h.modrm_reg != 7) {
        if (i.h.modrm_mod == 3) {
            auto& v = regs[op == 0x80 ? byteRegister(m) : m];
            if (op != 0x80 && (i.h.modrm_reg == 0 || i.h.modrm_reg == 5) &&
                (v.kind == Origin::Constant || (i.Width() == 8 && (v.kind == Origin::Object || v.kind == Origin::Address))))
                v.value += i.h.modrm_reg == 0 ? i.Immediate() : -i.Immediate();
            else v = {};
        }
    } else if (op == 0xc6 || op == 0xc7) {
        if (i.h.modrm_mod == 3) regs[op == 0xc6 ? byteRegister(m) : m] = op == 0xc6
            ? Provenance{} : Provenance{Origin::Constant, i.Immediate(), i.Width()};
    } else if (op >= 0xb0 && op <= 0xb7) {
        regs[byteRegister(op - 0xb0 + 8 * i.h.rex_b)] = {};
    } else if (op >= 0x58 && op <= 0x5f) {
        regs[op - 0x58 + 8 * i.h.rex_b] = {};
    } else if (op == 0x0f && op2 >= 0x40 && op2 <= 0x4f) {
        const auto source = LoadOperand(i, regs, i.Width());
        if (!(i.Width() == 8 && (regs[r].kind == Origin::Object || regs[r].kind == Origin::Result) &&
            source.kind == Origin::Constant && source.value == 0) && !(regs[r] == source)) regs[r] = {};
    } else if (op == 0x0f && ((op2 >= 0x90 && op2 <= 0x9f) || op2 == 0xc0 || op2 == 0xc1)) {
        if (i.h.modrm_mod == 3) regs[op2 == 0xc1 ? m : byteRegister(m)] = {};
        if (op2 == 0xc0 || op2 == 0xc1) regs[op2 == 0xc0 ? byteRegister(r) : r] = {};
    } else if (op == 0x0f && (op2 == 0x2c || op2 == 0x2d || op2 == 0x50 || op2 == 0xc5 || op2 == 0xd7)) {
        regs[r] = {};
    } else if (op == 0x0f && (op2 == 0x7e || op2 == 0x7f)) {
        if (i.h.modrm_mod == 3 && op2 == 0x7e && i.h.p_rep != 0xf3) regs[m] = {};
    } else if (op == 0x0f && ((op2 >= 0x10 && op2 <= 0x17) || (op2 >= 0x28 && op2 <= 0x2f) ||
        (op2 >= 0x51 && op2 <= 0x76) || op2 == 0x7f || op2 == 0xc2 || op2 == 0xc4 || op2 == 0xc6 ||
        (op2 >= 0xd0 && op2 <= 0xfe))) {
        // These SSE forms do not write a general-purpose register.
    } else if (op <= 0x35 && (op & 7) <= 5) {
        if ((op & 7) == 2 || (op & 7) == 3) regs[(op & 1) ? r : byteRegister(r)] = {};
        else if ((op & 7) == 4 || (op & 7) == 5) regs[0] = {};
        else if (i.h.modrm_mod == 3) regs[(op & 1) ? m : byteRegister(m)] = {};
    } else if (op == 0x69 || op == 0x6b || (op == 0x0f && op2 == 0xaf)) {
        regs[r] = {};
    } else if (op == 0xc0 || op == 0xc1 || (op >= 0xd0 && op <= 0xd3) ||
        (op == 0xff && i.h.modrm_reg <= 1) ||
        ((op == 0xf6 || op == 0xf7) && (i.h.modrm_reg == 2 || i.h.modrm_reg == 3))) {
        if (i.h.modrm_mod == 3) regs[(op == 0xc0 || op == 0xd0 || op == 0xd2 || op == 0xf6) ? byteRegister(m) : m] = {};
    } else if ((op >= 0x38 && op <= 0x3d) || op == 0x84 || op == 0x85 || op == 0xa8 || op == 0xa9 ||
        ((op == 0x80 || op == 0x81 || op == 0x83) && i.h.modrm_reg == 7) ||
        ((op == 0xf6 || op == 0xf7) && i.h.modrm_reg == 0) ||
        (op >= 0x50 && op <= 0x57) || op == 0x68 || op == 0x6a || (op == 0x90 && !i.h.rex_b) ||
        op == 0xc3 || op == 0xc2 || op == 0xcc || i.Jump() || i.Conditional() ||
        (op == 0xff && (i.h.modrm_reg == 4 || i.h.modrm_reg == 6)) || (op == 0x0f && op2 == 0x1f)) {
    } else {
        regs = {};
    }
    return regs;
}

inline std::map<unsigned, Registers> ObjectFlow(const Image& image, unsigned root) {
    const auto& function = image.Code(root);
    Require(function.complete, "required function has invalid control flow");
    std::map<unsigned, Registers> before;
    before[root][1] = {Origin::Object, 0, 8};
    std::deque<unsigned> pending{root};
    unsigned iterations = 0;
    while (!pending.empty()) {
        Require(++iterations < 100000, "object flow did not converge");
        const auto at = pending.front(); pending.pop_front();
        const auto& i = function.code.at(at);
        const auto after = Transfer(i, before.at(at));
        std::vector<unsigned> successors;
        if (i.Jump() || i.Conditional()) successors.push_back(static_cast<unsigned>(i.Target()));
        if (!i.Jump() && i.h.opcode != 0xc3 && i.h.opcode != 0xc2 && i.h.opcode != 0xcc &&
            !(i.h.opcode == 0xff && (i.h.modrm_reg == 4 || i.h.modrm_reg == 5))) successors.push_back(i.next);
        for (auto next : successors) {
            if (!function.code.count(next)) continue;
            auto [found, inserted] = before.emplace(next, after);
            bool changed = inserted;
            if (!inserted) {
                for (unsigned r = 0; r < 16; ++r) {
                    // Allocation failure and constructed-pointer arms join before member assignment.
                    if (found->second[r].kind == Origin::Result && after[r].kind == Origin::Constant && after[r].value == 0) continue;
                    if (after[r].kind == Origin::Result && found->second[r].kind == Origin::Constant && found->second[r].value == 0) {
                        found->second[r] = after[r]; changed = true; continue;
                    }
                    if (!(found->second[r] == after[r]) && found->second[r].kind != Origin::Unknown) {
                        found->second[r] = {}; changed = true;
                    }
                }
            }
            if (changed) pending.push_back(next);
        }
    }
    return before;
}

}
