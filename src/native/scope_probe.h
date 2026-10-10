// PASSIVE runtime evidence probe for the trusted actor/world/frame stamps (evidence cb14). It only COPIES bytes (through
// the injected CopyFn) into a bounded ring and formats them as text lines for scripts/re/scope_probe_analyze.py.
// It never produces a Scope, never feeds the mailbox and never mutates the game: the stamps stay uninstalled until the
// analysis proves which fields are a per-update counter / actor identity.
// Static facts it is built on (cb13/cb14): a per-entity body receives (column struct, ctx, dt*); ctx[0] = the ECS world
// object W (also loaded by every dispatcher from *SysCtx); the world table fields the bodies validate against live at
// W+0x58478..+0x58530; dt* is the third argument of the system call (args array at SysCtx+0x58, element 2).
#pragma once
#include "combat_native.h"

namespace scope_probe {
using combat_native::CopyFn;
using combat_native::copy_checked;
using combat_native::qword;

constexpr size_t kWorldHead = 0x40, kWorldTables = 0xd0, kDtBytes = 0x40, kCtxBytes = 0x40;
constexpr uint64_t kWorldTablesOffset = 0x58470;   // covers +0x58478 .. +0x58540
enum Kind : uint8_t { kLockOn = 0, kMovement = 1, kChooser = 2 };

struct Record {
    uint32_t seq;
    uint8_t kind, phase, valid;                   // valid bit0 ctx, bit1 world head, bit2 world tables, bit3 dt
    uint64_t world, ctx, dt, row_id, aim_id;
    uint8_t ctx_bytes[kCtxBytes], world_head[kWorldHead], world_tables[kWorldTables], dt_bytes[kDtBytes];
};

inline bool capture(CopyFn cp, Kind kind, combat_native::Phase phase, const void* ctx, const void* dt, uint64_t row_id,
                    uint64_t aim_id, uint32_t seq, Record* out) {
    Record r{};
    r.seq = seq; r.kind = kind; r.phase = static_cast<uint8_t>(phase); r.ctx = reinterpret_cast<uintptr_t>(ctx);
    r.dt = reinterpret_cast<uintptr_t>(dt); r.row_id = row_id; r.aim_id = aim_id;
    if (copy_checked(cp, r.ctx_bytes, r.ctx, kCtxBytes)) {
        r.valid |= 1;
        r.world = qword(r.ctx_bytes, 0);
        if (copy_checked(cp, r.world_head, r.world, kWorldHead)) {   // the world base must itself be readable before any offset is derived
            r.valid |= 2;
            if (copy_checked(cp, r.world_tables, combat_native::at(r.world, 0, 0, kWorldTablesOffset), kWorldTables)) r.valid |= 4;
        }
    }
    if (copy_checked(cp, r.dt_bytes, r.dt, kDtBytes)) r.valid |= 8;
    if (!r.valid) return false;
    *out = r;
    return true;
}

// Bounded ring (oldest overwritten); a spin lock like the other state; records are plain copies.
class Ring {
public:
    static constexpr size_t kCapacity = 256;
    uint32_t next_seq() { lock(); const uint32_t s = ++seq_; unlock(); return s; }
    void push(const Record& r) { lock(); buf_[head_ % kCapacity] = r; ++head_; unlock(); }
    size_t size() { lock(); const size_t n = head_ < kCapacity ? head_ : kCapacity; unlock(); return n; }
    bool at(size_t i, Record* out) {   // i = 0 oldest retained
        lock();
        const size_t n = head_ < kCapacity ? head_ : kCapacity;
        const bool ok = i < n;
        if (ok) *out = buf_[(head_ - n + i) % kCapacity];
        unlock();
        return ok;
    }
private:
    void lock() { while (flag_.test_and_set(std::memory_order_acquire)) {} }
    void unlock() { flag_.clear(std::memory_order_release); }
    std::atomic_flag flag_;
    Record buf_[kCapacity] = {};
    size_t head_ = 0;
    uint32_t seq_ = 0;
};

inline size_t hex(const uint8_t* p, size_t n, char* dst) {
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) { dst[2 * i] = d[p[i] >> 4]; dst[2 * i + 1] = d[p[i] & 15]; }
    return 2 * n;
}
// "P <seq> <kind> <phase> <valid> <world> <ctx> <dt> <row> <aim> C:<hex> W:<hex> T:<hex> D:<hex>\n"; returns length (0 if too small).
inline size_t format_line(const Record& r, char* dst, size_t cap) {
    const size_t need = 200 + 2 * (kCtxBytes + kWorldHead + kWorldTables + kDtBytes) + 16;
    if (cap < need) return 0;
    int n = std::snprintf(dst, cap, "P %u %u %u %u %llx %llx %llx %llx %llx C:", r.seq, r.kind, r.phase, r.valid,
                          static_cast<unsigned long long>(r.world), static_cast<unsigned long long>(r.ctx),
                          static_cast<unsigned long long>(r.dt), static_cast<unsigned long long>(r.row_id),
                          static_cast<unsigned long long>(r.aim_id));
    if (n < 0) return 0;
    size_t o = static_cast<size_t>(n);
    o += hex(r.ctx_bytes, kCtxBytes, dst + o); dst[o++] = ' '; dst[o++] = 'W'; dst[o++] = ':';
    o += hex(r.world_head, kWorldHead, dst + o); dst[o++] = ' '; dst[o++] = 'T'; dst[o++] = ':';
    o += hex(r.world_tables, kWorldTables, dst + o); dst[o++] = ' '; dst[o++] = 'D'; dst[o++] = ':';
    o += hex(r.dt_bytes, kDtBytes, dst + o); dst[o++] = '\n'; dst[o] = 0;
    return o;
}

}  // namespace scope_probe
