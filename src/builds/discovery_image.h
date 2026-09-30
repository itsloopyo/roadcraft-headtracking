// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include "runtime_discovery.h"
#include <hde64.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace rc_ht::builds::discovery {

class Rejected : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
inline void Require(bool condition, const char* message) {
    if (!condition) throw Rejected(message);
}
struct Range {
    unsigned begin = 0, end = 0;
    bool Contains(std::uint64_t at, std::size_t count = 1) const {
        return at >= begin && at <= end && count <= end - at;
    }
};
struct Instruction {
    unsigned at = 0, next = 0;
    hde64s h{};
    unsigned Reg() const { return h.modrm_reg + 8u * h.rex_r; }
    unsigned Rm() const { return h.modrm_rm + 8u * h.rex_b; }
    int Base() const {
        if (!(h.flags & F_MODRM) || h.modrm_mod == 3) return -1;
        if (h.flags & F_SIB) {
            if (h.sib_index != 4 || h.rex_x || (h.modrm_mod == 0 && h.sib_base == 5)) return -1;
            return h.sib_base + 8 * h.rex_b;
        }
        if (h.modrm_mod == 0 && h.modrm_rm == 5) return -1;
        return static_cast<int>(Rm());
    }
    int Disp() const {
        return (h.flags & F_DISP8) ? static_cast<std::int8_t>(h.disp.disp8)
            : (h.flags & F_DISP32) ? static_cast<std::int32_t>(h.disp.disp32) : 0;
    }
    unsigned Width() const { return h.rex_w ? 8u : h.p_66 ? 2u : 4u; }
    std::int64_t Immediate() const {
        return (h.flags & F_IMM8) ? static_cast<std::int8_t>(h.imm.imm8)
            : static_cast<std::int32_t>(h.imm.imm32);
    }
    bool Rip() const {
        return !h.p_67 && !h.p_seg && (h.flags & F_MODRM) && !(h.flags & F_SIB) &&
            h.modrm_mod == 0 && h.modrm_rm == 5;
    }
    std::int64_t RipTarget() const { return static_cast<std::int64_t>(next) + Disp(); }
    bool Call() const { return h.opcode == 0xe8; }
    bool Jump() const { return h.opcode == 0xe9 || h.opcode == 0xeb; }
    bool Conditional() const {
        return (h.opcode >= 0x70 && h.opcode <= 0x7f) ||
            (h.opcode == 0x0f && h.opcode2 >= 0x80 && h.opcode2 <= 0x8f);
    }
    std::int64_t Target() const { return static_cast<std::int64_t>(next) + Immediate(); }
};
struct Function {
    std::map<unsigned, Instruction> code;
    bool complete = true;
};

class Image {
public:
    explicit Image(ImageView view) : view_(view) {
        Require(view.data && view.size <= UINT32_MAX, "invalid image buffer");
        Require(Read<std::uint16_t>(0) == 0x5a4d, "missing DOS header");
        const auto nt = Read<unsigned>(0x3c);
        Require(Read<unsigned>(nt) == 0x4550 && Read<std::uint16_t>(nt + 4ull) == 0x8664 &&
            Read<std::uint16_t>(nt + 24ull) == 0x20b, "expected x64 PE image");
        const auto opt = static_cast<std::uint64_t>(nt) + 24;
        Require(Read<unsigned>(opt + 56) == view.size, "image size disagrees with PE header");
        const auto count = Read<std::uint16_t>(nt + 6ull);
        const auto optSize = Read<std::uint16_t>(nt + 20ull);
        Require(count && count <= 96 && optSize >= 160, "invalid section directory");
        std::vector<Range> occupied;
        for (unsigned i = 0; i < count; ++i) {
            const auto at = opt + optSize + i * 40ull;
            Bounds(at, 40);
            const auto begin = Read<unsigned>(at + 12), size = Read<unsigned>(at + 8);
            Bounds(begin, size);
            Range r{begin, begin + size};
            for (auto old : occupied) Require(r.begin >= old.end || old.begin >= r.end, "overlapping sections");
            occupied.push_back(r);
            const auto flags = Read<unsigned>(at + 36);
            char name[9]{}; std::memcpy(name, view.data + at, 8);
            if (std::strcmp(name, ".text") == 0) {
                Require((flags & 0x60000000) == 0x60000000 && !text.end, "invalid executable section"); text = r;
            } else if (std::strcmp(name, ".rdata") == 0) {
                Require((flags & 0x40000000) && !(flags & 0xa0000000) && !rdata.end, "invalid read-only section"); rdata = r;
            } else if (std::strcmp(name, ".data") == 0) {
                Require((flags & 0xc0000000) == 0xc0000000 && !data.end, "invalid data section"); data = r;
            }
        }
        Require(text.end && rdata.end && data.end, "missing required sections");
        const auto exception = Read<unsigned>(opt + 136), bytes = Read<unsigned>(opt + 140);
        Require(bytes && bytes % 12 == 0, "invalid exception directory"); Bounds(exception, bytes);
        unsigned lastEnd = 0;
        for (unsigned i = 0; i < bytes; i += 12) {
            const auto b = Read<unsigned>(exception + i), e = Read<unsigned>(exception + i + 4);
            Require(b >= lastEnd && e > b && text.Contains(b, e - b), "invalid function bounds");
            unwind_.emplace(b, std::make_pair(e, Read<unsigned>(exception + i + 8)));
            lastEnd = e;
        }
        for (const auto& [b, info] : unwind_) {
            const auto root = Root(b);
            ranges_[root].push_back({b, info.first});
        }
    }
    template<class T> T Read(std::uint64_t at) const {
        Bounds(at, sizeof(T)); T value; std::memcpy(&value, view_.data + at, sizeof(T)); return value;
    }
    void Bounds(std::uint64_t at, std::size_t size) const {
        Require(at <= view_.size && size <= view_.size - at, "image read out of bounds");
    }
    unsigned Pointer(unsigned at) const {
        const auto p = Read<std::uint64_t>(at);
        return p >= view_.base && p - view_.base < view_.size ? static_cast<unsigned>(p - view_.base) : 0;
    }
    unsigned Root(unsigned at) const {
        auto it = unwind_.upper_bound(at);
        if (it == unwind_.begin() || at >= std::prev(it)->second.first) return 0;
        --it;
        std::set<unsigned> seen;
        for (;;) {
            const auto b = it->first, u = it->second.second;
            Require(seen.insert(b).second && seen.size() <= 32, "cyclic chained unwind");
            const auto flags = Read<std::uint8_t>(u);
            Require((flags & 7) == 1 || (flags & 7) == 2, "unsupported unwind version");
            if (!(flags & 0x20)) return b;
            Require(!(flags & 0x18), "chained unwind has a handler");
            const auto tail = static_cast<std::uint64_t>(u) + 4 + ((Read<std::uint8_t>(u + 2ull) + 1u) & ~1u) * 2;
            const auto parent = Read<unsigned>(tail);
            it = unwind_.find(parent);
            Require(it != unwind_.end() && it->second.first == Read<unsigned>(tail + 4) &&
                it->second.second == Read<unsigned>(tail + 8), "invalid chained unwind parent");
        }
    }
    Instruction Decode(unsigned at) const {
        Require(text.Contains(at), "instruction outside executable section");
        std::uint8_t buffer[32]{};
        const auto available = std::min<unsigned>(15, text.end - at);
        std::memcpy(buffer, view_.data + at, available);
        Instruction i{}; i.at = at;
        const auto length = hde64_disasm(buffer, &i.h);
        Require(length && length <= available && !(i.h.flags & F_ERROR), "invalid instruction");
        i.next = at + length;
        return i;
    }
    const Function& Code(unsigned root) const {
        const auto cached = code_.find(root);
        if (cached != code_.end()) return cached->second;
        Require(ranges_.count(root) != 0, "function lacks primary unwind entry");
        Function result;
        std::vector<unsigned> pending{root};
        const auto inside = [&](unsigned at, unsigned size = 1) {
            for (auto r : ranges_.at(root)) if (r.Contains(at, size)) return true;
            return false;
        };
        try {
            while (!pending.empty()) {
                unsigned at = pending.back(); pending.pop_back();
                for (;;) {
                    if (result.code.count(at)) break;
                    Require(result.code.size() < 32768 && inside(at), "control flow escapes function");
                    auto next = result.code.upper_bound(at);
                    Require(next == result.code.begin() || std::prev(next)->second.next <= at, "branch enters an instruction");
                    const auto i = Decode(at);
                    Require(inside(at, i.next - at) && (next == result.code.end() || i.next <= next->first), "overlapping instruction boundaries");
                    result.code.emplace(at, i);
                    if (i.h.opcode == 0xc3 || i.h.opcode == 0xc2 || i.h.opcode == 0xcc) break;
                    if (i.Jump() || i.Conditional()) {
                        const auto target = i.Target();
                        Require(target >= 0 && static_cast<std::uint64_t>(target) < view_.size && text.Contains(target), "invalid branch target");
                        if (inside(static_cast<unsigned>(target))) pending.push_back(static_cast<unsigned>(target));
                        else Require(i.Jump(), "conditional branch escapes function");
                        if (i.Jump()) break;
                    }
                    if (i.h.opcode == 0xff && (i.h.modrm_reg == 4 || i.h.modrm_reg == 5)) break;
                    at = i.next;
                }
            }
        } catch (const Rejected&) { result.complete = false; }
        return code_.emplace(root, std::move(result)).first->second;
    }
    std::vector<unsigned> Strings(const char* name) const {
        std::vector<unsigned> out;
        const auto size = std::strlen(name) + 1;
        auto p = view_.data + rdata.begin;
        const auto end = view_.data + rdata.end;
        for (;;) {
            p = std::search(p, end, name, name + size);
            if (p == end) break;
            const auto at = static_cast<unsigned>(p - view_.data);
            if (at == rdata.begin || view_.data[at - 1] == 0) out.push_back(at);
            ++p;
        }
        return out;
    }
    std::set<unsigned> Referencing(unsigned target) const {
        std::set<unsigned> roots;
        for (unsigned disp = text.begin; text.Contains(disp, 4); ++disp) {
            const auto delta = static_cast<std::int64_t>(target) - disp - 4 - Read<std::int32_t>(disp);
            if (delta < 0 || delta > 8) continue;
            for (unsigned back = 1; back <= 8 && disp >= text.begin + back; ++back) {
                const auto at = disp - back, root = Root(at);
                if (!root || roots.count(root)) continue;
                const auto& code = Code(root).code;
                const auto found = code.find(at);
                if (found == code.end()) continue;
                const auto& i = found->second;
                if ((i.Rip() && i.RipTarget() == target) ||
                    ((i.Call() || i.Jump()) && i.Target() == target)) {
                    roots.insert(root);
                }
            }
        }
        return roots;
    }
    std::set<unsigned> Named(const char* name) const {
        const auto strings = Strings(name);
        if (strings.size() != 1) throw Rejected(std::string("missing or ambiguous named anchor: ") + name);
        return Referencing(strings.front());
    }
    std::vector<unsigned> Pointers(unsigned target) const {
        std::vector<unsigned> out;
        for (unsigned at = rdata.begin; rdata.Contains(at, 8); at += 8)
            if (Pointer(at) == target) out.push_back(at);
        return out;
    }
    std::set<unsigned> ReferencingRange(Range range) const {
        std::set<unsigned> roots;
        for (unsigned disp = text.begin; text.Contains(disp, 4); ++disp) {
            const auto target = static_cast<std::int64_t>(disp) + 4 + Read<std::int32_t>(disp);
            if (target < static_cast<std::int64_t>(range.begin) - 8 || target >= range.end) continue;
            for (unsigned back = 2; back <= 8 && disp >= text.begin + back; ++back) {
                const auto at = disp - back, root = Root(at);
                if (!root || roots.count(root)) continue;
                const auto& code = Code(root).code;
                const auto found = code.find(at);
                if (found != code.end() && found->second.Rip() && range.Contains(found->second.RipTarget())) roots.insert(root);
            }
        }
        return roots;
    }
    const std::map<unsigned, std::vector<Range>>& Functions() const { return ranges_; }
    Range text, rdata, data;
private:
    ImageView view_;
    std::map<unsigned, std::pair<unsigned, unsigned>> unwind_;
    std::map<unsigned, std::vector<Range>> ranges_;
    mutable std::map<unsigned, Function> code_;
};

}
