// Passive "PhysX Character RigidBody Pusher" probe (naga #49322): the engine's own character-vs-body contact system (cb22-cb24), read-only.
// WHY: CharacterCollisions is NOT body contact. In every saved capture its only non-empty entry is {camera-locked target handle, kind 1}, present
// during an active melee attack chain (starts at the first accepted chooser row, repeated each update, drained by the core) and EMPTY for
// sustained object contact (object-contact capture, 220 K) and for standing against an enemy without attacking (enemy-contact, 580 K). It is a
// melee-attack-target relation written by the melee/AI systems, not a contact output (CB53).
// WHAT this reads (all guarded copies at the controller wrapper entry/exit, same loan as the K probe; no calls, no writes):
//   container = *(CharacterController.col[row*0x20]+8) (cb26), pusher = *(container+0xe0) validated by its vtable ptr (exe+0x4d7fc48, cb24),
//   records: ptr +0x10, u32 count +0x18, 0x198-byte records {+0 body ptr, +0x20/+0x28 doubles (last refresh), +0x40 contact count (<=12),
//   +0x44 7-float contacts}; body key = *(u64*)(*(u64*)body + 0x10) (>>1 = key of FUN_142cf0d00/142cd55c0), entity vectors A (+0x1010 ptr,
//   +0x1018 u32) and B (+0x1070 ptr, +0x1078 u32) of u64 entity handles, current time double +0x10d0; per-entity flag word
//   u16 at *(W+0x198) + idx*12 + 2 (bit 0x200 set while a record body is associated, 0x1000 for vector A; cb23).
// Output `P ...` lines (changed signature or 1 s heartbeat) through a bounded try-lock ring flushed by the worker. This is evidence collection
// for "does an enemy body ever appear in the pusher records / vectors"; it does NOT classify enemies and is not a contact signal yet.
#pragma once
#include "step_binding.h"

namespace pusher_probe {
using combat_native::at;
using combat_native::copy_checked;
using combat_native::qword;

constexpr uint32_t kRvaPusherVtbl = 0x4d7fc48, kMaxRecords = 4, kMaxHandles = 8, kBudget = 16384, kHeartbeatMs = 1000;
constexpr size_t kRing = 128;
struct Rec {
    uint32_t seq = 0, ms = 0; uint64_t qpc = 0, container = 0, pusher = 0, vt = 0; double now = 0;
    uint8_t phase = 0, ok = 0;                       // ok bits: 1 container, 2 pusher vtable verified, 4 records, 8 vector A, 16 vector B
    uint32_t nrec = 0, na = 0, nb = 0; uint8_t shown = 0, shown_a = 0, shown_b = 0;
    struct R { uint64_t body = 0, inner = 0, keyraw = 0; double t20 = 0, t28 = 0; uint8_t cc = 0, have = 0, c0[0x1c] = {}; } rec[kMaxRecords];
    uint64_t a[kMaxHandles] = {}, b[kMaxHandles] = {}; uint16_t fa[kMaxHandles] = {}, fb[kMaxHandles] = {}; uint8_t fa_ok[kMaxHandles] = {}, fb_ok[kMaxHandles] = {};
    // NewTrackedEntities (character-controller update system column 0x11 = 18th lookup of builder FUN_142781010, stride 0x10 {u64* handles, u32 count}): entities the controller core
    // appended this update (cb27/cb55); read-only copy of the column the wrapper already receives. The column INDEX 0x11 comes from the builder's lookup order and is a
    // CANDIDATE (tag lookups between could shift it): the handles are validated against the world tables and printed with '?' when they are not live entities.
    uint8_t nt_ok = 0, nt_shown = 0; uint32_t nnt = 0; uint64_t nt[kMaxHandles] = {}; uint16_t fnt[kMaxHandles] = {}; uint8_t fnt_ok[kMaxHandles] = {};
};
struct State {
    Rec ring[kRing]; uint32_t head = 0, tail = 0, pushed = 0; uint64_t last_sig = 0; uint32_t last_ms = 0; bool have_sig = false;
    std::atomic<uint32_t> dropped{0}, calls{0}, bad{0}; std::atomic_flag lock; uint64_t (*qpc)() = nullptr;
};
inline State& st() { static State s; return s; }
inline void set_qpc_source(uint64_t (*f)()) { st().qpc = f; }

inline uint16_t entity_flag(uint64_t w, uint64_t handle, bool* ok) {
    auto& e = step_binding::env(); uint64_t table = 0; uint16_t v = 0; *ok = false;
    const uint32_t idx = static_cast<uint32_t>(handle);
    if (!w || idx == 0xffffffffu || !copy_checked(e.copy, &table, at(w, 0, 0, 0x198), 8) || !table || !copy_checked(e.copy, &v, at(table, idx, 12, 2), 2)) return 0;
    *ok = true; return v;
}
inline void read_vector(uint64_t pusher, uint32_t ptr_off, uint32_t cnt_off, uint64_t w, uint32_t* n, uint64_t* h, uint16_t* f, uint8_t* fok, uint8_t* shown, bool* ok) {
    auto& e = step_binding::env(); uint64_t p = 0; uint32_t c = 0; *ok = false; *n = 0; *shown = 0;
    if (!copy_checked(e.copy, &p, at(pusher, 0, 0, ptr_off), 8) || !copy_checked(e.copy, &c, at(pusher, 0, 0, cnt_off), 4) || c > 4096) return;
    *ok = true; *n = c;
    const uint32_t take = c < kMaxHandles ? c : kMaxHandles;
    if (take && p && copy_checked(e.copy, h, p, static_cast<size_t>(take) * 8)) {
        *shown = static_cast<uint8_t>(take);
        for (uint32_t i = 0; i < take; ++i) f[i] = entity_flag(w, h[i], reinterpret_cast<bool*>(&fok[i]));
    }
}
inline uint64_t signature(const Rec& r) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { for (int i = 0; i < 8; ++i) { h ^= static_cast<uint8_t>(v >> (8 * i)); h *= 1099511628211ull; } };
    mix(r.ok); mix(r.nrec); mix(r.na); mix(r.nb); mix(r.nnt);
    for (uint32_t i = 0; i < r.nt_shown; ++i) { mix(r.nt[i]); mix(r.fnt[i]); }
    for (uint32_t i = 0; i < r.shown; ++i) { mix(r.rec[i].body); mix(r.rec[i].cc); }
    for (uint32_t i = 0; i < r.shown_a; ++i) { mix(r.a[i]); mix(r.fa[i]); }
    for (uint32_t i = 0; i < r.shown_b; ++i) { mix(r.b[i]); mix(r.fb[i]); }
    return h;
}
// phase 0 = before the wrapper (core drains CharacterCollisions inside), 1 = after the original returned.
inline void capture(const void* cols, uint8_t phase) {
    State& s = st(); auto& e = step_binding::env(); auto& b = combat_native::binding();
    if (!e.copy || !e.base) return;
    s.calls.fetch_add(1, std::memory_order_relaxed);
    uint8_t c[0x1d * 8];
    if (!copy_checked(e.copy, c, reinterpret_cast<uintptr_t>(cols), sizeof c)) { s.bad.fetch_add(1, std::memory_order_relaxed); return; }
    const uint64_t row = qword(c, 0x1c * 8);
    if (row >= combat_native::cols::kMaxRow) { s.bad.fetch_add(1, std::memory_order_relaxed); return; }
    Rec r; r.phase = phase; r.ms = b.clock ? b.clock() : 0; r.qpc = s.qpc ? s.qpc() : 0;
    const uint64_t w = step_binding::mail().world.load(std::memory_order_relaxed);
    if (copy_checked(e.copy, &r.container, at(qword(c, 0), row, 0x20, 8), 8) && r.container) r.ok |= 1;
    if (!(r.ok & 1) || !copy_checked(e.copy, &r.pusher, at(r.container, 0, 0, 0xe0), 8) || !r.pusher) { s.bad.fetch_add(1, std::memory_order_relaxed); return; }   // no container/pusher pointer at all
    uint64_t recs = 0;
    const bool verified = copy_checked(e.copy, &r.vt, r.pusher, 8) && r.vt == e.base + kRvaPusherVtbl;
    if (verified) {                                  // unverified object: nothing but the pointer and its first qword is copied (explicit `ok` without bit 2)
        r.ok |= 2;
        if (copy_checked(e.copy, &recs, at(r.pusher, 0, 0, 0x10), 8) && copy_checked(e.copy, &r.nrec, at(r.pusher, 0, 0, 0x18), 4) && r.nrec <= 64 && recs) r.ok |= 4;
        copy_checked(e.copy, &r.now, at(r.pusher, 0, 0, 0x10d0), 8);
    }
    if (r.ok & 4) {
        r.shown = static_cast<uint8_t>(r.nrec < kMaxRecords ? r.nrec : kMaxRecords);
        for (uint32_t i = 0; i < r.shown; ++i) {
            const uint64_t ra = at(recs, i, 0x198); Rec::R& x = r.rec[i];
            if (!ra || !copy_checked(e.copy, &x.body, ra, 8)) continue;
            copy_checked(e.copy, &x.t20, at(ra, 0, 0, 0x20), 8); copy_checked(e.copy, &x.t28, at(ra, 0, 0, 0x28), 8);
            copy_checked(e.copy, &x.cc, at(ra, 0, 0, 0x40), 1);
            if (x.cc && x.cc <= 12) copy_checked(e.copy, x.c0, at(ra, 0, 0, 0x44), sizeof x.c0);
            uint64_t first = 0;
            if (x.body && copy_checked(e.copy, &first, x.body, 8) && first && copy_checked(e.copy, &x.keyraw, at(first, 0, 0, 0x10), 8)) x.inner = first;
            x.have = 1;
        }
    }
    bool va = false, vb = false;
    if (verified) {
        read_vector(r.pusher, 0x1010, 0x1018, w, &r.na, r.a, r.fa, r.fa_ok, &r.shown_a, &va); if (va) r.ok |= 8;
        read_vector(r.pusher, 0x1070, 0x1078, w, &r.nb, r.b, r.fb, r.fb_ok, &r.shown_b, &vb); if (vb) r.ok |= 16;
    }
    {   // NewTrackedEntities (guarded; independent of the pusher verification)
        const uint64_t ntcol = qword(c, 0x11 * 8), ra = ntcol ? at(ntcol, row, 0x10) : 0; uint8_t hdr[0x10];
        if (ra && copy_checked(e.copy, hdr, ra, sizeof hdr)) {
            const uint64_t ptr = qword(hdr, 0); uint32_t cnt = 0; std::memcpy(&cnt, hdr + 8, 4);
            if (cnt <= 4096) {
                r.nt_ok = 1; r.nnt = cnt; const uint32_t take = cnt < kMaxHandles ? cnt : kMaxHandles;
                if (take && ptr && copy_checked(e.copy, r.nt, ptr, static_cast<size_t>(take) * 8)) {
                    r.nt_shown = static_cast<uint8_t>(take);
                    for (uint32_t i = 0; i < take; ++i) {
                        bool fok = false; r.fnt[i] = entity_flag(w, r.nt[i], &fok);
                        step_binding::Loc loc; r.fnt_ok[i] = (fok && step_binding::locate(w, r.nt[i], &loc)) ? 1 : 0;   // '?' in the log = NOT a live entity handle (wrong column index would show here)
                    }
                }
            }
        }
    }
    const uint64_t sig = signature(r);
    if (s.lock.test_and_set(std::memory_order_acquire)) { s.dropped.fetch_add(1, std::memory_order_relaxed); return; }
    const bool changed = !s.have_sig || s.last_sig != sig;
    if ((changed || r.ms - s.last_ms >= kHeartbeatMs) && s.pushed < kBudget) {
        if (s.head - s.tail >= kRing) s.dropped.fetch_add(1, std::memory_order_relaxed);
        else { r.seq = ++s.pushed; s.ring[s.head % kRing] = r; ++s.head; s.last_sig = sig; s.last_ms = r.ms; s.have_sig = true; }
    }
    s.lock.clear(std::memory_order_release);
}
inline size_t hex_to(const uint8_t* p, size_t n, char* dst) { static const char d[] = "0123456789abcdef"; for (size_t i = 0; i < n; ++i) { dst[2 * i] = d[p[i] >> 4]; dst[2 * i + 1] = d[p[i] & 15]; } return 2 * n; }
inline unsigned flush(bool (*write)(const char*, size_t, void*), void* ctx, unsigned max_lines) {
    State& s = st(); static Rec batch[8]; static char line[2048];
    if (max_lines > 8) max_lines = 8;
    if (s.lock.test_and_set(std::memory_order_acquire)) return 0;
    unsigned n = 0;
    while (n < max_lines && s.tail != s.head) { batch[n++] = s.ring[s.tail % kRing]; ++s.tail; }
    s.lock.clear(std::memory_order_release);
    for (unsigned i = 0; i < n; ++i) {
        const Rec& r = batch[i];
        int o = std::snprintf(line, sizeof line, "P %u qpc=%llu ms=%u ph=%u ok=%u cont=%llx pusher=%llx vt=%llx now=%.3f nrec=%u na=%u nb=%u REC:", r.seq, static_cast<unsigned long long>(r.qpc), r.ms,
            r.phase, r.ok, static_cast<unsigned long long>(r.container), static_cast<unsigned long long>(r.pusher), static_cast<unsigned long long>(r.vt), r.now, r.nrec, r.na, r.nb);
        if (o <= 0) continue;
        size_t k = static_cast<size_t>(o);
        for (uint32_t j = 0; j < r.shown && k + 300 < sizeof line; ++j) {
            const Rec::R& x = r.rec[j];
            k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, "%s%llx/%llx/%llx/%.3f/%.3f/%u/", j ? ";" : "", static_cast<unsigned long long>(x.body), static_cast<unsigned long long>(x.inner),
                static_cast<unsigned long long>(x.keyraw), x.t20, x.t28, x.cc));
            k += hex_to(x.c0, sizeof x.c0, line + k);
        }
        k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, " A:"));
        for (uint32_t j = 0; j < r.shown_a && k + 40 < sizeof line; ++j) k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, "%s%llx/%s%04x", j ? "," : "", static_cast<unsigned long long>(r.a[j]), r.fa_ok[j] ? "" : "?", r.fa[j]));
        k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, " B:"));
        for (uint32_t j = 0; j < r.shown_b && k + 40 < sizeof line; ++j) k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, "%s%llx/%s%04x", j ? "," : "", static_cast<unsigned long long>(r.b[j]), r.fb_ok[j] ? "" : "?", r.fb[j]));
        k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, " NT:%s%u:", r.nt_ok ? "" : "?", r.nnt));
        for (uint32_t j = 0; j < r.nt_shown && k + 40 < sizeof line; ++j) k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, "%s%llx/%s%04x", j ? "," : "", static_cast<unsigned long long>(r.nt[j]), r.fnt_ok[j] ? "" : "?", r.fnt[j]));
        line[k++] = '\n'; line[k] = 0;
        if (!write(line, k, ctx)) break;
    }
    const uint32_t lost = s.dropped.exchange(0, std::memory_order_relaxed);
    if (lost) { const int o = std::snprintf(line, sizeof line, "{\"event\":\"pusher_probe_dropped\",\"count\":%u}\n", lost); if (o > 0) (void)write(line, static_cast<size_t>(o), ctx); }
    return n;
}
}  // namespace pusher_probe
