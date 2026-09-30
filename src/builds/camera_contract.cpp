// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "camera_contract.h"
#include <cmath>

namespace rc_ht::builds::discovery {
namespace {
enum class Kind { Unknown, Argument, Stack, Saved, Element };
struct Value {
    Kind kind = Kind::Unknown;
    int id = 0;
    std::int64_t offset = 0;
};
class Frame {
public:
    Frame() {
        regs[1] = {Kind::Argument, 0}; regs[2] = {Kind::Argument, 1};
        regs[8] = {Kind::Argument, 2}; regs[9] = {Kind::Argument, 3};
        regs[4] = {Kind::Stack}; stack[40] = {Kind::Argument, 4};
        for (unsigned r : {3u, 5u, 6u, 7u, 12u, 13u, 14u, 15u}) regs[r] = {Kind::Saved, static_cast<int>(r)};
    }
    Value Address(const Instruction& i) const {
        const auto base = i.Base();
        if (base < 0 || i.h.p_seg || i.h.p_67) return {};
        auto value = regs[base]; value.offset += i.Disp(); return value;
    }
    Value Load(Value address, unsigned width) const {
        if (address.kind == Kind::Stack && width == 8) {
            const auto found = stack.find(address.offset);
            return found == stack.end() ? Value{} : found->second;
        }
        if (address.kind == Kind::Argument && address.id > 0 && width == 4 &&
            address.offset >= 0 && address.offset <= 8 && address.offset % 4 == 0)
            return {Kind::Element, address.id, address.offset};
        return {};
    }
    bool Administrative(const Instruction& i) {
        const auto op = i.h.opcode;
        if (i.h.p_seg || i.h.p_67 || i.h.p_lock || i.h.p_66 || i.h.p_rep) return false;
        if (op >= 0x50 && op <= 0x57) {
            if (regs[4].kind != Kind::Stack) return false;
            regs[4].offset -= 8; stack[regs[4].offset] = regs[op - 0x50 + 8 * i.h.rex_b];
        } else if (op >= 0x58 && op <= 0x5f) {
            if (regs[4].kind != Kind::Stack) return false;
            regs[op - 0x58 + 8 * i.h.rex_b] = Load(regs[4], 8); regs[4].offset += 8;
        } else if ((op == 0x81 || op == 0x83) && i.h.rex_w && i.h.modrm_mod == 3 &&
            i.Rm() == 4 && (i.h.modrm_reg == 0 || i.h.modrm_reg == 5)) {
            regs[4].offset += i.h.modrm_reg == 0 ? i.Immediate() : -i.Immediate();
        } else if (op == 0x8b || op == 0x89) {
            const auto width = i.Width();
            if (i.h.modrm_mod == 3) {
                const auto dst = op == 0x8b ? i.Reg() : i.Rm();
                const auto src = op == 0x8b ? i.Rm() : i.Reg();
                if (width != 8 && regs[src].kind != Kind::Element) return false;
                regs[dst] = regs[src];
            } else if (op == 0x8b) {
                regs[i.Reg()] = Load(Address(i), width);
                if (regs[i.Reg()].kind == Kind::Unknown) return false;
            } else {
                const auto address = Address(i);
                if (width != 8 || address.kind != Kind::Stack) return false;
                stack[address.offset] = regs[i.Reg()];
            }
        } else if (op == 0x90 && !i.h.rex_b) {
        } else return false;
        return true;
    }
    bool ReadyToCall() const {
        return regs[4].kind == Kind::Stack && regs[4].offset <= -32 &&
            ((-regs[4].offset) & 15) == 8;
    }
    void Call() { for (unsigned r : {0u, 1u, 2u, 8u, 9u, 10u, 11u}) regs[r] = {}; }
    bool Restored() const {
        if (regs[4].kind != Kind::Stack || regs[4].offset != 0) return false;
        for (unsigned r : {3u, 5u, 6u, 7u, 12u, 13u, 14u, 15u})
            if (regs[r].kind != Kind::Saved || regs[r].id != static_cast<int>(r)) return false;
        return true;
    }
    bool Camera(Value v) const { return v.kind == Kind::Argument && v.id == 0 && v.offset == 0; }
    std::array<Value, 16> regs{};
    std::map<std::int64_t, Value> stack;
};
}

bool InspectTransform(const Image& image, unsigned function, TransformContract& out) {
    out = {};
    const auto& code = image.Code(function);
    if (!code.complete || code.code.size() > 100) return false;
    Frame frame;
    std::set<std::pair<int, std::int64_t>> copies;
    TransformContract result;
    unsigned at = function;
    for (const auto& [address, i] : code.code) {
        if (at != address) return false;
        at = i.next;
        if (i.h.opcode == 0x89 && i.h.modrm_mod != 3 && i.Width() == 4) {
            const auto dest = frame.Address(i), source = frame.regs[i.Reg()];
            if (dest.kind != Kind::Argument || dest.id != 0 || source.kind != Kind::Element) return false;
            const int rows[] = {0, 3, 1, 0, 2};
            if (source.id < 1 || source.id > 4 || dest.offset != rows[source.id] * 16 + source.offset ||
                !copies.emplace(source.id, source.offset).second) return false;
        } else if (i.Call()) {
            if (!copies.empty() || result.initialize || !frame.ReadyToCall() ||
                !image.text.Contains(i.Target())) return false;
            result.initialize = static_cast<unsigned>(i.Target()); frame.Call();
        } else if (i.Jump()) {
            if (copies.size() != 12 || !frame.Restored() || !frame.Camera(frame.regs[1]) ||
                !image.text.Contains(i.Target()) || address != code.code.rbegin()->first) return false;
            result.update = static_cast<unsigned>(i.Target());
        } else if (!frame.Administrative(i)) return false;
    }
    if (!result.update || !result.initialize) return false;
    out = result; return true;
}

bool InspectFov(const Image& image, unsigned function, FovContract& out) {
    out = {};
    const auto& code = image.Code(function);
    if (!code.complete || code.code.size() > 50) return false;
    enum Scalar { Unknown, Input, HalfRadians, Tangent, Scaled, Angle, Degrees };
    std::array<Scalar, 16> xmm{}; xmm[1] = Input;
    Frame frame; FovContract result;
    unsigned at = function, stores = 0;
    for (const auto& [address, i] : code.code) {
        if (at != address || i.h.p_seg || i.h.p_67 || i.h.p_lock) return false;
        at = i.next;
        const auto op = i.h.opcode2;
        if (i.h.opcode == 0x0f && op == 0x11 && i.h.p_rep == 0xf3 && i.h.modrm_mod != 3) {
            auto dest = frame.Address(i);
            if (dest.kind != Kind::Argument || dest.id != 0 || dest.offset < 64 ||
                dest.offset > 0x10000 || dest.offset % 4) return false;
            if (xmm[i.Reg()] == Input && stores == 0) result.input = static_cast<unsigned>(dest.offset);
            else if (xmm[i.Reg()] == Degrees && stores == 1) result.output = static_cast<unsigned>(dest.offset);
            else return false;
            ++stores;
        } else if (i.h.opcode == 0x0f && (op == 0x28 || op == 0x29) && !i.h.p_rep &&
            !i.h.p_66 && i.h.modrm_mod == 3) {
            const auto dest = op == 0x28 ? i.Reg() : i.Rm(), source = op == 0x28 ? i.Rm() : i.Reg();
            xmm[dest] = xmm[source];
        } else if (i.h.opcode == 0x0f && (op == 0x59 || op == 0x5e) && i.h.p_rep == 0xf3 &&
            i.h.modrm_mod != 3) {
            auto& value = xmm[i.Reg()];
            if (i.Rip() && op == 0x59 && image.rdata.Contains(i.RipTarget(), 4)) {
                const auto constant = image.Read<float>(i.RipTarget());
                if (value == Input && std::fabs(constant - 0.00872664626f) < 1e-9f) value = HalfRadians;
                else if (value == Angle && std::fabs(constant - 114.591559f) < 1e-4f) value = Degrees;
                else return false;
            } else {
                const auto field = frame.Address(i);
                if (value != Tangent || field.kind != Kind::Argument || field.id != 0 ||
                    field.offset < 64 || field.offset > 0x10000 || field.offset % 4 || result.aspect) return false;
                result.aspect = static_cast<unsigned>(field.offset); result.horizontal = op == 0x59; value = Scaled;
            }
        } else if (i.Call()) {
            if (!frame.ReadyToCall() || !image.text.Contains(i.Target())) return false;
            const auto value = xmm[0];
            if (value == HalfRadians && !result.tangent) result.tangent = static_cast<unsigned>(i.Target());
            else if (value == Scaled && !result.arctangent) result.arctangent = static_cast<unsigned>(i.Target());
            else return false;
            for (unsigned r = 0; r < 6; ++r) xmm[r] = Unknown;
            xmm[0] = value == HalfRadians ? Tangent : Angle;
            frame.Call();
        } else if (i.Jump()) {
            if (stores != 2 || !frame.Restored() || !frame.Camera(frame.regs[1]) ||
                !image.text.Contains(i.Target()) || address != code.code.rbegin()->first) return false;
            result.update = static_cast<unsigned>(i.Target());
        } else if (!frame.Administrative(i)) return false;
    }
    if (!result.update || !result.tangent || !result.arctangent || result.input == result.output ||
        result.aspect == result.input || result.aspect == result.output) return false;
    out = result; return true;
}

}
