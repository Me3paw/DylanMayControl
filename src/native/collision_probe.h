// PASSIVE pre/post probe of the character controller's CharacterCollisions list (evidence cb25-cb29).
// Copies bytes only (injected CopyFn), never mutates the game, never decides anything, never feeds combat_mod. It claims
// NOTHING about "current contact" or about which row is the Player: it records what the list holds BEFORE the controller
// core consumes it (entry of FUN_14277f170) and what is left AFTER the original returns, with explicit truncation and
// validity bits, so offline analysis (scripts/re/collision_probe_analyze.py) can test repetition against independent timing.
//
// Hook target (cb26/cb29): FUN_14277f170(cols, ctx, p3, p4) - four pointer arguments, called PER ROW by dispatcher
// FUN_14277da10 (and once more from 142781870); a detour must forward all four unchanged. `cols` is a per-row stack struct of
// >= 0x1d u64: cols[0] = CharacterController column (stride 0x20, +8 = controller container object), cols[3] =
// CharacterCollisions column (stride 0x20), cols[9] = 0x40-stride world transform, cols[0x1c] = ROW INDEX.
// CharacterCollisions row: +0 entries ptr, +8 u32 count, entries 12 bytes {u64 entity handle, u8 kind}, flags +0x18/+0x19/+0x1b.
// Controller container (cb25/cb29): +0x10 = proxy object, +0x18 = OWNER ENTITY HANDLE copied at construction (0 if none),
// +0x20 = u32 flag; the pusher is at +0xe0.
#pragma once
#include "scope_probe.h"

namespace collision_probe {
using combat_native::CopyFn;
using combat_native::copy_checked;
using combat_native::qword;

constexpr size_t kColsRead = 0x1d * 8, kMaxEntries = 16, kEntryStride = 12, kTransform = 0x40;
constexpr uint64_t kMaxRow = 1u << 20;
constexpr uint32_t kMaxCount = 4096;                 // a larger count is treated as corrupt, never read
enum Phase : uint8_t { kPre = 0, kPost = 1 };
// valid bits: 1 cols, 2 cc row, 4 entries, 8 container, 16 transform
struct Entry { uint64_t handle; uint8_t kind; };
struct Record {
    uint32_t seq = 0, tid = 0, now_ms = 0;
    uint8_t phase = 0, valid = 0, truncated = 0, corrupt = 0;
    uint64_t row = 0, cols = 0, cc_row = 0, entries_ptr = 0, container = 0, owner = 0, proxy = 0;
    uint32_t owner_flag = 0, count = 0, copied = 0;
    uint8_t flags[4] = {};                            // CharacterCollisions +0x18..+0x1b
    Entry e[kMaxEntries] = {};
    uint8_t transform[kTransform] = {};
};

inline bool capture(CopyFn cp, const void* cols_ptr, Phase phase, uint32_t seq, uint32_t tid, uint32_t now_ms, Record* out) {
    Record r;
    r.seq = seq; r.tid = tid; r.now_ms = now_ms; r.phase = phase;
    r.cols = reinterpret_cast<uintptr_t>(cols_ptr);
    uint8_t c[kColsRead];
    if (!copy_checked(cp, c, r.cols, sizeof c)) return false;
    r.valid |= 1;
    r.row = qword(c, 0x1c * 8);
    if (r.row >= kMaxRow) { r.corrupt = 1; *out = r; return true; }
    const uint64_t cc_base = qword(c, 3 * 8), ctl_base = qword(c, 0), tr_base = qword(c, 9 * 8);
    uint8_t cc[0x20];
    const uint64_t cc_addr = combat_native::at(cc_base, r.row, 0x20);
    if (!cc_addr && cc_base) r.corrupt = 1;                                   // base present but the sum wraps/leaves user mode: never attributed to a row
    if (cc_addr && copy_checked(cp, cc, cc_addr, sizeof cc)) {
        r.valid |= 2;
        r.cc_row = cc_addr;
        r.entries_ptr = qword(cc, 0);
        std::memcpy(&r.count, cc + 8, 4);
        std::memcpy(r.flags, cc + 0x18, 4);
        if (r.count > kMaxCount) {
            r.corrupt = 1;                            // never read through a corrupt count
        } else if (r.count == 0) {
            r.valid |= 4;
        } else {
            const uint32_t take = r.count < kMaxEntries ? r.count : static_cast<uint32_t>(kMaxEntries);
            uint8_t raw[kMaxEntries * kEntryStride];
            if (copy_checked(cp, raw, r.entries_ptr, take * kEntryStride)) {
                for (uint32_t i = 0; i < take; ++i) { std::memcpy(&r.e[i].handle, raw + i * kEntryStride, 8); r.e[i].kind = raw[i * kEntryStride + 8]; }
                r.copied = take;
                r.valid |= 4;
            }
            r.truncated = r.count > take ? 1 : 0;
        }
    }
    uint64_t container = 0;
    const uint64_t ctl_addr = combat_native::at(ctl_base, r.row, 0x20, 8);
    if (!ctl_addr && ctl_base) r.corrupt = 1;
    if (ctl_addr && copy_checked(cp, &container, ctl_addr, 8) && container) {
        r.container = container;
        uint8_t ct[0x18];
        const uint64_t ct_addr = combat_native::at(container, 0, 0, 0x10);
        if (!ct_addr) r.corrupt = 1;
        if (ct_addr && copy_checked(cp, ct, ct_addr, sizeof ct)) {
            r.proxy = qword(ct, 0); r.owner = qword(ct, 8); std::memcpy(&r.owner_flag, ct + 0x10, 4);
            r.valid |= 8;
        }
    }
    const uint64_t tr_addr = combat_native::at(tr_base, r.row, kTransform);
    if (!tr_addr && tr_base) r.corrupt = 1;
    if (tr_addr && copy_checked(cp, r.transform, tr_addr, kTransform)) r.valid |= 16;
    *out = r;
    return true;
}

struct Config {
    bool log_empty = false;              // also log rows whose list is empty (needed to measure gaps; heavy)
    uint64_t watch_owner = 0;            // always log rows whose container owner handle equals this (integrator-chosen, NOT authority)
};

class Probe {
public:
    static constexpr size_t kCapacity = 512;
    void configure(const Config& c) { lock(); cfg_ = c; unlock(); }
    void set_env(CopyFn cp, uint32_t (*tid)(), uint32_t (*clock)()) { lock(); cp_ = cp; tid_ = tid; clock_ = clock; unlock(); }
    // Called by the detour: `pre` BEFORE forwarding to the original, `post` AFTER it returned (same thread, same args).
    void observe(const void* cols, Phase phase) {
        lock();
        const CopyFn cp = cp_; const auto tid = tid_; const auto clk = clock_; const Config cfg = cfg_;
        unlock();
        if (!cp) return;
        Record r;
        if (!capture(cp, cols, phase, next_seq(), tid ? tid() : 0, clk ? clk() : 0, &r)) { count_failed(); return; }
        const bool interesting = r.count != 0 || r.corrupt || cfg.log_empty || (cfg.watch_owner != 0 && r.owner == cfg.watch_owner);
        if (!interesting) { count_skipped(); return; }
        lock();
        buf_[head_ % kCapacity] = r; ++head_;
        unlock();
    }
    size_t size() { lock(); const size_t n = head_ < kCapacity ? head_ : kCapacity; unlock(); return n; }
    bool at(size_t i, Record* out) {
        lock();
        const size_t n = head_ < kCapacity ? head_ : kCapacity;
        const bool ok = i < n;
        if (ok) *out = buf_[(head_ - n + i) % kCapacity];
        unlock();
        return ok;
    }
    uint32_t skipped() { lock(); const uint32_t v = skipped_; unlock(); return v; }
    uint32_t failed() { lock(); const uint32_t v = failed_; unlock(); return v; }
    uint32_t overwritten() { lock(); const uint32_t v = head_ > kCapacity ? static_cast<uint32_t>(head_ - kCapacity) : 0; unlock(); return v; }
private:
    uint32_t next_seq() { lock(); const uint32_t s = ++seq_; unlock(); return s; }
    void count_failed() { lock(); ++failed_; unlock(); }
    void count_skipped() { lock(); ++skipped_; unlock(); }
    void lock() { while (flag_.test_and_set(std::memory_order_acquire)) {} }
    void unlock() { flag_.clear(std::memory_order_release); }
    std::atomic_flag flag_;
    Config cfg_{}; CopyFn cp_ = nullptr; uint32_t (*tid_)() = nullptr; uint32_t (*clock_)() = nullptr;
    Record buf_[kCapacity] = {};
    size_t head_ = 0; uint32_t seq_ = 0, skipped_ = 0, failed_ = 0;
};

// One text line per record (kLineMax bytes needed); 0 when the buffer is too small.
constexpr size_t kLineMax = 1024;
inline size_t format_line(const Record& r, char* dst, size_t cap) {
    if (cap < kLineMax) return 0;
    int n = std::snprintf(dst, cap, "K %u %u %u %u %u %u %u %llx %llx %llx %llx %llx %llx %u %u %u FL:",
                          r.seq, r.phase, r.valid, r.truncated, r.corrupt, r.tid, r.now_ms,
                          static_cast<unsigned long long>(r.row), static_cast<unsigned long long>(r.cc_row),
                          static_cast<unsigned long long>(r.entries_ptr), static_cast<unsigned long long>(r.container),
                          static_cast<unsigned long long>(r.owner), static_cast<unsigned long long>(r.proxy),
                          r.owner_flag, r.count, r.copied);
    if (n < 0) return 0;
    size_t o = static_cast<size_t>(n);
    o += scope_probe::hex(r.flags, 4, dst + o);
    dst[o++] = ' '; dst[o++] = 'E'; dst[o++] = ':';
    for (uint32_t i = 0; i < r.copied; ++i) {
        if (i) dst[o++] = ',';
        n = std::snprintf(dst + o, cap - o, "%llx/%u", static_cast<unsigned long long>(r.e[i].handle), r.e[i].kind);
        if (n < 0) return 0;
        o += static_cast<size_t>(n);
    }
    dst[o++] = ' '; dst[o++] = 'T'; dst[o++] = ':';
    o += scope_probe::hex(r.transform, kTransform, dst + o);
    dst[o++] = '\n'; dst[o] = 0;
    return o;
}

}  // namespace collision_probe
