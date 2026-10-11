// Passive camera target-lock logger (naga #34013). READ-ONLY diagnostics: records the NATIVE lock state and the selected target of the
// camera_lock_on::on_update per-entity body FUN_1427e6cb0(cols, ctx, dt) (pin `lockon_body`, cb2/cb13), never mutates, never gates a
// feature and never feeds combat_mod. The lock state is LockStatus byte 0 (cb2/cb4f: the LogicData tag bit 40 is NOT used as the lock
// flag; its raw slot bytes are logged only as unverified context, and the lock BUTTON is never equated with the active byte).
//
// Column struct (cb13, prologue-verified): +0x08 CameraSelectedTarget (0x10, u64 entity handle), +0x20 AimData (0x40), +0x28 LogicData
// (0x810), +0x30 LockStatus (0xf0: +0 locked byte, +0x6c out-of-range timer float, +0x90 target position vec4, +0xa0 position-valid
// byte), +0x48 row index. The detour forwards the three arguments unchanged, preserves LastError, captures AFTER the original (the
// state the system just produced) with guarded copies, and queues the record into a bounded ring flushed by the worker.
//
// OWNER: a row is `verified` only when its LogicData row address equals the observed Player's LogicData row (resolved through the world
// tables, step_binding::resolve_player); `other` when the Player resolved to a different row; `unknown` when world/actor/Player rows
// are not available - then it is NOT claimed to be the Player's lock. Target validity/enemy-tag are checked against the world tables
// only when the world is known (255 = unknown).
#pragma once
#include "held_lock_native.h"
#include "step_binding.h"

namespace camera_lock_log {
using combat_native::at;
using combat_native::copy_checked;
using combat_native::qword;
using LockFn = void (*)(void**, void*, void*);
using QpcFn = uint64_t (*)();

constexpr uint32_t kRvaLockOn = 0x27e6cb0;
constexpr size_t kRing = 256; constexpr uint32_t kBudget = 16384, kHeartbeatMs = 1000;
enum Owner : uint8_t { kOwnerUnknown = 0, kOwnerVerified = 1, kOwnerOther = 2 };

struct Rec {
    uint32_t seq = 0, ms = 0; uint64_t qpc = 0, row = 0, status_ptr = 0, logic_ptr = 0, target = 0;
    uint8_t owner = 0, heartbeat = 0, lock = 0, pos_valid = 0, target_valid = 255, target_enemy = 255;
    uint8_t status_ok = 0, aim_ok = 0, logic_ok = 0, target_ok = 0;
    uint32_t out_of_range_bits = 0; uint8_t target_pos[16] = {}, aim_pos[16] = {}, tag40[0x28] = {}, tag27[0x28] = {};
};
struct Seen { uint64_t row = 0, sig = 0; uint32_t ms = 0; bool used = false; };
struct State {
    LockFn orig = nullptr; QpcFn qpc = nullptr; bool pins_ok = false, hooked = false;
    Rec ring[kRing]; uint32_t head = 0, tail = 0; std::atomic<uint32_t> dropped{0}, pushed{0}, calls{0}, invalid{0};
    std::atomic_flag lock; Seen seen[4];
};
inline State& st() { static State s; return s; }

inline bool init(uint64_t base, combat_native::CopyFn cp, QpcFn qpc) {
    State& s = st(); s.qpc = qpc;
    static const char* const names[] = {"lockon_body"};
    const char* bad = nullptr;
    s.pins_ok = native_pins::verify_set(cp, base, names, 1, &bad);
    return s.pins_ok;
}
inline void set_original(LockFn f) { st().orig = f; }
inline void set_hooked(bool on) { st().hooked = on; }
inline const char* missing() {
    State& s = st();
    return !s.pins_ok ? "pin_mismatch:lockon_body" : !s.orig ? "original_missing" : !s.hooked ? "hook_not_installed" : "";
}

inline uint64_t signature(const Rec& r) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint8_t b) { h ^= b; h *= 1099511628211ull; };
    mix(r.owner); mix(r.lock); mix(r.pos_valid); mix(r.target_valid); mix(r.target_enemy); mix(r.status_ok);
    for (int i = 0; i < 8; ++i) mix(static_cast<uint8_t>(r.target >> (8 * i)));
    mix(r.tag40[0x1c]); mix(r.tag27[0x1c]);
    return h;
}
// Capture after the original returned. Never throws, never blocks, restores nothing itself (the detour restores LastError).
inline void observe(void** cols) {
    if constexpr(!diagnostics::enabled)return;
    State& s = st(); auto& e = step_binding::env(); auto& b = combat_native::binding();
    if (!e.copy || s.pushed.load(std::memory_order_relaxed)>=kBudget) return;
    s.calls.fetch_add(1, std::memory_order_relaxed);
    uint8_t c[combat_native::cols::kLockOnSize];
    if (!copy_checked(e.copy, c, reinterpret_cast<uintptr_t>(cols), sizeof c)) { s.invalid.fetch_add(1, std::memory_order_relaxed); return; }
    const uint64_t row = qword(c, combat_native::cols::kLockOnRow);
    if (row >= combat_native::cols::kMaxRow) { s.invalid.fetch_add(1, std::memory_order_relaxed); return; }
    Rec r;
    r.row = row; r.ms = b.clock ? b.clock() : 0; r.qpc = s.qpc ? s.qpc() : 0;
    r.status_ptr = at(qword(c, combat_native::cols::kLockOnStatus), row, 0xf0);
    const uint64_t sel = at(qword(c, combat_native::cols::kLockOnSelected), row, 0x10), aim = at(qword(c, combat_native::cols::kLockOnAim), row, 0x40);
    r.logic_ptr = at(qword(c, 0x28), row, 0x810);
    uint8_t status[0xa8];
    if (r.status_ptr && copy_checked(e.copy, status, r.status_ptr, sizeof status)) {
        r.status_ok = 1; r.lock = status[0]; r.pos_valid = status[0xa0];
        std::memcpy(&r.out_of_range_bits, status + 0x6c, 4); std::memcpy(r.target_pos, status + 0x90, 16);
    }
    if (sel && copy_checked(e.copy, &r.target, sel, 8)) r.target_ok = 1;
    if (aim && copy_checked(e.copy, r.aim_pos, aim, 16)) r.aim_ok = 1;
    if (r.logic_ptr) {
        r.logic_ok = copy_checked(e.copy, r.tag40, at(r.logic_ptr, 40, 0x28), sizeof r.tag40) &&
                     copy_checked(e.copy, r.tag27, at(r.logic_ptr, 27, 0x28), sizeof r.tag27) ? 1 : 0;   // raw context, meaning unverified
    }
    const uint64_t w = step_binding::mail().world.load(std::memory_order_relaxed);
    const uint64_t actor = e.player ? e.player() : 0;
    uint64_t player_logic=0;
    if (w && actor && r.logic_ptr && step_binding::resolve_player_column(w,actor,
        step_binding::kColLogic,0x810,&player_logic))
        r.owner=player_logic==r.logic_ptr ? kOwnerVerified : kOwnerOther;
    if (r.owner == kOwnerOther) return;                                          // another entity's lock row: not logged
    if (w && r.target_ok && static_cast<uint32_t>(r.target) != 0xffffffffu && r.target) {
        step_binding::Loc loc;
        r.target_valid = step_binding::locate(w, r.target, &loc) ? 1 : 0;
        r.target_enemy = r.target_valid ? (step_binding::is_enemy(w, r.target) ? 1 : 0) : 255;
    } else if (w && r.target_ok) r.target_valid = 0;
    // change / heartbeat filter (per row, 4 rows tracked)
    const uint64_t sig = signature(r);
    if (s.lock.test_and_set(std::memory_order_acquire)) { s.dropped.fetch_add(1, std::memory_order_relaxed); return; }
    Seen* slot = nullptr;
    for (Seen& x : s.seen) if (x.used && x.row == r.status_ptr) slot = &x;
    if (!slot) for (Seen& x : s.seen) if (!x.used) { slot = &x; break; }
    if (!slot) slot = &s.seen[row & 3];
    const bool changed = !slot->used || slot->row != r.status_ptr || slot->sig != sig;
    if ((changed || r.ms - slot->ms >= kHeartbeatMs) && s.pushed.load(std::memory_order_relaxed) < kBudget) {
        if (s.head - s.tail >= kRing) s.dropped.fetch_add(1, std::memory_order_relaxed);
        else {
            r.heartbeat = changed ? 0 : 1; r.seq = s.pushed.fetch_add(1, std::memory_order_relaxed) + 1;
            s.ring[s.head % kRing] = r; ++s.head;
            slot->used = true; slot->row = r.status_ptr; slot->sig = sig; slot->ms = r.ms;
        }
    }
    s.lock.clear(std::memory_order_release);
}
// The detour installed by the shared loader (root): exact forward of (cols, ctx, dt); LastError preserved around the capture.
inline void lockon_detour(void** cols, void* ctx, void* dt) {
    auto& b = combat_native::binding();
    const LockFn orig = st().orig;
    if (!orig) return;
    const uint32_t incoming = b.get_last_error ? b.get_last_error() : 0;
    uint32_t forwarded=0;
    { held_lock_native::Scope scope(cols,false,ctx);
      if (b.set_last_error) b.set_last_error(incoming);
      orig(cols, ctx, dt);
      forwarded = b.get_last_error ? b.get_last_error() : 0; }
    if constexpr(diagnostics::enabled)if(st().pins_ok)observe(cols);
    if (b.set_last_error) b.set_last_error(forwarded);
}
inline size_t hex_to(const uint8_t* p, size_t n, char* dst) { static const char d[] = "0123456789abcdef"; for (size_t i = 0; i < n; ++i) { dst[2 * i] = d[p[i] >> 4]; dst[2 * i + 1] = d[p[i] & 15]; } return 2 * n; }
// Worker side: writes up to max_lines pending records as `Y ...` lines; a busy lock skips the call.
inline unsigned flush(bool (*write)(const char*, size_t, void*), void* ctx, unsigned max_lines) {
    State& s = st(); static Rec batch[16]; static char line[1024];
    if (max_lines > 16) max_lines = 16;
    if (s.lock.test_and_set(std::memory_order_acquire)) return 0;
    unsigned n = 0;
    while (n < max_lines && s.tail != s.head) { batch[n++] = s.ring[s.tail % kRing]; ++s.tail; }
    s.lock.clear(std::memory_order_release);
    for (unsigned i = 0; i < n; ++i) {
        const Rec& r = batch[i];
        int o = std::snprintf(line, sizeof line, "Y %u qpc=%llu ms=%u hb=%u own=%u lock=%u posv=%u tgt=%llx tv=%u en=%u oor=%08x row=%llx st=%llx lg=%llx ok=%u%u%u%u POS:",
            r.seq, static_cast<unsigned long long>(r.qpc), r.ms, r.heartbeat, r.owner, r.lock, r.pos_valid, static_cast<unsigned long long>(r.target),
            r.target_valid, r.target_enemy, r.out_of_range_bits, static_cast<unsigned long long>(r.row), static_cast<unsigned long long>(r.status_ptr),
            static_cast<unsigned long long>(r.logic_ptr), r.status_ok, r.target_ok, r.aim_ok, r.logic_ok);
        if (o <= 0) continue;
        size_t k = static_cast<size_t>(o);
        k += hex_to(r.target_pos, 16, line + k);
        k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, " AIM:"));
        k += hex_to(r.aim_pos, 16, line + k);
        k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, " T40:"));
        k += hex_to(r.tag40, sizeof r.tag40, line + k);
        k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, " T27:"));
        k += hex_to(r.tag27, sizeof r.tag27, line + k);
        line[k++] = '\n'; line[k] = 0;
        if (!write(line, k, ctx)) break;
    }
    const uint32_t lost = s.dropped.exchange(0, std::memory_order_relaxed);
    if (lost) { const int o = std::snprintf(line, sizeof line, "Y dropped=%u\n", lost); if (o > 0) (void)write(line, static_cast<size_t>(o), ctx); }
    return n;
}
}  // namespace camera_lock_log
