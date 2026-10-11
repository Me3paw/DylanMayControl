// Enemy-step binding (cb33/cb34): REAL hooks on two native per-entity bodies plus guarded writes; no timers, no fake cancel.
//
//  - player_locomotion::update_locomotion_state body FUN_142278f90(cols, x): exposes, for ONE entity row, DodgeData cols[0] (0x40),
//    Jump cols[3] (0x28), Airborne cols[7] (0xc, +8 = airborne byte), CharacterCollisions cols[15] (0x20), row index cols[0x12].
//    The hook observes only. It binds to the PLAYER by identity, not by role: the observed Player actor handle (loader-supplied) is
//    resolved through the world tables to its own Jump column row; only the row whose Jump address equals that is the player's.
//  - jump::handle_input body FUN_1423f9e80(cols, p2, p3, dt): cols[1] Input (0x40), cols[3] Jump (0x28), cols[4] NextJumpRequest (2),
//    cols[8] row. Fresh press = pinned native flag-A helper edge (down now && up on the previous call of the PLAYER row), so a held jump
//    never repeats. When armed (player row, airborne, and the LATEST locomotion observation of that row listed an ENEMY-tagged entity in
//    CharacterCollisions: state, not a grace timer) the proven jump-counter and air-dodge resets are written BEFORE the original, so the
//    native logic starts the jump itself; afterwards NextJumpRequest type 3 = the native gate refused (cancel unbound).
//  - Enemy = the entity has the TAG heron::faction_tag::component::NPCEnemy and not the Dead tag (tag key array, W+0x18448, count
//    W+0x18460; NOT the data keys), resolved from the collided entity handle exactly like the native faction builder FUN_1420f34b0.
//
// World layout, relative to W = *ctx = *param_1 (raw FUN_1427783a0 / FUN_1427ebea0, Sol's resolver): entity count W+0x58510, generations
// W+0x584e8, packed location W+0x58530 (low16 archetype, high32 row), per-archetype row count W+0x10448+arch*4, chunk W+0x50+arch*8,
// entity of a row at chunk+row*8+0x10, metadata W+0x18448+arch*0x20 = {tags*, keys*, offsets*, u32 tag_count, u32 key_count}.
//
// Mutation is gated by Proof (cb34): enemy predicate (proven), contact (CharacterCollisions producer/kind/current-vs-event, UNPROVEN until
// the H2 log is read) and cancel (native attack-end route, UNBOUND). Until all hold the feature is Unbound and nothing is written; the
// observation is read-only and logs `enemy_in_cc` status lines (rate-limited) so the human run supplies the contact evidence.
#pragma once
#include "combat_mod.h"
#include "feature_status.h"
#include "native_pins.h"

namespace step_binding {
using feature_status::Event;
using feature_status::Feature;
using feature_status::State;
using combat_native::CopyFn;
using combat_native::at;
using combat_native::copy_checked;
using combat_native::qword;

constexpr uint32_t kRvaLoco = 0x22b31b0, kRvaJump = 0x243b190;
using WriteFn = bool (*)(void* dst, const void* src, size_t n);   // guarded (VirtualQuery writable) write supplied by the loader
using PlayerFn = uint64_t (*)();                                  // observed full Player actor handle (0 = unknown)
using LocoFn = void (*)(void**, void*);
using JumpFn = void (*)(void**, void*, void*, float*);

// Component type hash (proven for all 17 columns of the locomotion state builder, cb33): 32-bit FNV-1a, case sensitive, full type name.
constexpr uint32_t fnv1a(const char* s) { uint32_t h = 0x811c9dc5u; while (*s) { h ^= static_cast<uint8_t>(*s++); h *= 0x01000193u; } return h; }
static_assert(fnv1a("heron::jump::component::Jump") == 0x728fb980u, "component hash function (cb33)");
static_assert(fnv1a("coregame::component::CharacterCollisions") == 0x51d983cfu, "component hash function (cb33)");
constexpr uint32_t kHashNPCEnemy = fnv1a("heron::faction_tag::component::NPCEnemy");
static_assert(kHashNPCEnemy == 0xaef1a562u, "enemy tag hash (cb34)");
static_assert(fnv1a("heron::faction_tag::component::NPCFriendly") == 0xf11ba161u, "friendly tag hash (cb34)");
constexpr uint32_t kTagPlayer = 0xf714ec8eu, kTagDead = 0x6e63a89cu;                   // tag hashes used by the loadout actor resolver (Sol, raw-ASM verified)
constexpr uint32_t kColCombo = 0x3bfcf661u, kColLogic = 0x9988f341u;                // ComboData (0x48), LogicData (0x810): loadout actor resolver
constexpr uint32_t kColDodge = 0xa52b1cf2u, kColJump = 0x728fb980u, kColAirborne = 0x4e2a6c69u, kColCollisions = 0x51d983cfu;   // locomotion builder cols 0/3/7/15
constexpr uint32_t kColAirAttacks=fnv1a("heron::melee_attack::component::AirAttacksData");
static_assert(kColAirAttacks==0xd7eff982u);

struct Proof { std::atomic<bool> contact{false},cancel{false}; };
inline Proof& proof() { static Proof p; return p; }
struct Env {
    uint64_t base = 0; CopyFn copy = nullptr; WriteFn write = nullptr; PlayerFn player = nullptr;
    bool (*can_write)(void*,size_t)=nullptr;
    using ContactReader=bool (*)(uint64_t,uint64_t,uint64_t*);
    std::atomic<ContactReader> contact_now{nullptr};
    bool (*accept)(const char*, size_t) = nullptr;                 // raw log line sink; true = queued (read-only gate capture only)
    bool pins_ok = false, have_jump_action = false, loco_hooked = false, jump_hooked = false;
    uint16_t jump_action = 0xffff, dodge_action = 0xffff;       // input action indexes DAT_145ceb488 / DAT_145ceb490 (cb7/cb9)
    bool have_dodge_action = false, dodge_flag_read = false;     // dodge flag-A read is OFF until its key presence on the Player Input row is proven (the helper crashes on an absent key)
    LocoFn loco_orig = nullptr; JumpFn jump_orig = nullptr;
};
inline Env& env() { static Env e; return e; }
struct Mail {                       // latest observation of the PLAYER row (relaxed atomics; a state, not a timer)
    std::atomic<uint64_t> world{0}, actor{0}, jump_row{0}, dodge_row{0}, air_row{0}, loco_updates{0}, contact_handle{0}, steps_applied{0};
    std::atomic<uint32_t> airborne{0}, prev_down{0}, rows_invalid{0}, not_player{0};
};
inline Mail& mail() { static Mail m; return m; }

template <class F> inline F fn(uint32_t rva) { return reinterpret_cast<F>(static_cast<uintptr_t>(env().base + rva)); }

// ---- world pointer (ctx[0] of the system bodies, cb26): fed by the collision-wrapper detour; validated by the entity-table size ----
inline void feed_world_from_ctx(const void* ctx) {
    Env& e = env(); uint64_t w = 0, count = 0;
    if (!e.copy || !ctx || !copy_checked(e.copy, &w, reinterpret_cast<uintptr_t>(ctx), 8) || !at(w, 0, 0)) return;
    if (!copy_checked(e.copy, &count, at(w, 0, 0, 0x58510), 8) || count == 0 || count > (1u << 26)) return;
    mail().world.store(w, std::memory_order_relaxed);
}

// ---- entity resolution (same checks as the loadout actor resolver) ----
struct Meta { uint64_t tags = 0, keys = 0, offsets = 0; uint32_t tag_count = 0, key_count = 0; };
struct Loc { uint64_t chunk = 0; uint32_t row = 0; uint16_t arch = 0; };
constexpr uint32_t kMetaMax = 1024;   // the Player archetype holds 539 tags / 519 keys (single-jump capture): 256 made every resolve fail
inline bool locate(uint64_t w, uint64_t handle, Loc* out) {
    Env& e = env(); const uint32_t idx = static_cast<uint32_t>(handle), gen = static_cast<uint32_t>(handle >> 32);
    if (!w || !e.copy || idx == 0xffffffffu) return false;
    uint64_t count = 0, gens = 0, locs = 0, gword = 0, packed = 0, chunk = 0, actual = 0; uint32_t rows = 0;
    if (!copy_checked(e.copy, &count, at(w, 0, 0, 0x58510), 8) || idx >= count) return false;
    if (!copy_checked(e.copy, &gens, at(w, 0, 0, 0x584e8), 8) || !copy_checked(e.copy, &gword, at(gens, idx, 8), 8) || static_cast<uint32_t>(gword) != gen) return false;
    if (!copy_checked(e.copy, &locs, at(w, 0, 0, 0x58530), 8) || !copy_checked(e.copy, &packed, at(locs, idx, 8), 8)) return false;
    const uint16_t arch = static_cast<uint16_t>(packed); const uint32_t row = static_cast<uint32_t>(packed >> 32);
    if (arch == 0xffff) return false;
    if (!copy_checked(e.copy, &chunk, at(w, arch, 8, 0x50), 8) || !chunk || !copy_checked(e.copy, &rows, at(w, arch, 4, 0x10448), 4) || row >= rows) return false;
    if (!copy_checked(e.copy, &actual, at(chunk, row, 8, 0x10), 8) || actual != handle) return false;
    out->chunk = chunk; out->row = row; out->arch = arch;
    return true;
}
inline bool read_meta(uint64_t w, uint16_t arch, Meta* m) {
    Env& e = env(); uint8_t raw[0x20];
    if (!copy_checked(e.copy, raw, at(w, arch, 0x20, 0x18448), sizeof raw)) return false;
    m->tags = qword(raw, 0); m->keys = qword(raw, 8); m->offsets = qword(raw, 0x10);
    std::memcpy(&m->tag_count, raw + 0x18, 4); std::memcpy(&m->key_count, raw + 0x1c, 4);
    return m->tag_count <= kMetaMax && m->key_count <= kMetaMax;
}
inline bool read_u32s(uint64_t ptr,uint32_t n,uint32_t* out) {
    if (n>kMetaMax) return false;
    if (!n) return true;
    return out && copy_checked(env().copy,out,ptr,static_cast<size_t>(n)*sizeof(uint32_t));
}
// Tag membership (presence only): the tag key array, never the data keys.
inline bool has_tag(const Meta& m, uint32_t hash, bool* present) {
    uint32_t tags[kMetaMax];
    if (!read_u32s(m.tags, m.tag_count, tags)) return false;
    *present = false;
    for (uint32_t i = 0; i < m.tag_count; ++i) if (tags[i] == hash) *present = true;
    return true;
}
inline bool is_enemy(uint64_t w, uint64_t handle) {
    Loc l; Meta m; bool enemy = false, dead = false;
    return locate(w, handle, &l) && read_meta(w, l.arch, &m) && has_tag(m, kHashNPCEnemy, &enemy) && enemy && has_tag(m, kTagDead, &dead) && !dead;
}
// Entity labels as the native Lua error reporter FUN_141a0a010 reads them (cb39): DebugName (0x0bca075d, 16-byte row) and GlobalID
// (0x58372dc9, 8-byte row) of the callback owner entity. Read-only; false when the handle does not resolve (FUN_140466d00 semantics).
constexpr uint32_t kColDebugName = 0x0bca075du, kColGlobalId = 0x58372dc9u;
inline bool entity_column_bytes(uint64_t w, uint64_t handle, uint32_t hash, size_t stride, uint8_t* out) {
    Env& e = env(); Loc l; Meta m;
    if (stride == 0 || stride > 0x40 || !locate(w, handle, &l) || !read_meta(w, l.arch, &m)) return false;
    uint32_t keys[kMetaMax], offs[kMetaMax];
    if (!read_u32s(m.keys, m.key_count, keys) || !read_u32s(m.offsets, m.key_count, offs)) return false;
    for (uint32_t i = 0; i < m.key_count; ++i) {
        if (keys[i] != hash) continue;
        const uint64_t base = offs[i] ? at(l.chunk, 0, 0, offs[i]) : 0, row = base ? at(base, l.row, stride) : 0;
        return row && copy_checked(e.copy, out, row, stride);
    }
    return false;
}
inline bool entity_labels(uint64_t w, uint64_t handle, uint8_t debug_name[16], uint64_t* global_id) {
    uint8_t g[8];
    if (!entity_column_bytes(w, handle, kColDebugName, 16, debug_name) || !entity_column_bytes(w, handle, kColGlobalId, 8, g)) return false;
    std::memcpy(global_id, g, 8); return true;
}
struct PlayerRows { uint64_t jump = 0, dodge = 0, air = 0, cc = 0, combo = 0, logic = 0; };   // combo/logic optional (attack-state evidence only)
// Resolve the observed Player actor to ITS rows of the locomotion columns (data keys + offsets; strides 0x28/0x40/0xc/0x20).
inline bool resolve_player(uint64_t w, uint64_t actor, PlayerRows* out) {
    Env& e = env(); Loc l; Meta m; bool player = false, dead = false;
    if (!actor || !locate(w, actor, &l) || !read_meta(w, l.arch, &m) || !has_tag(m, kTagPlayer, &player) || !player || !has_tag(m, kTagDead, &dead) || dead) return false;
    uint32_t keys[kMetaMax], offs[kMetaMax];
    if (!read_u32s(m.keys, m.key_count, keys) || !read_u32s(m.offsets, m.key_count, offs)) return false;
    auto column = [&](uint32_t hash, size_t stride, uint64_t* row) {
        for (uint32_t i = 0; i < m.key_count; ++i) {
            if (keys[i] != hash) continue;
            const uint64_t base = offs[i] ? at(l.chunk, 0, 0, offs[i]) : 0;
            *row = base ? at(base, l.row, stride) : 0;
            return *row != 0;
        }
        return false;
    };
    PlayerRows r;
    if (!column(kColJump, 0x28, &r.jump) || !column(kColDodge, 0x40, &r.dodge) || !column(kColAirborne, 0xc, &r.air) || !column(kColCollisions, 0x20, &r.cc)) return false;
    (void)column(kColCombo, 0x48, &r.combo); (void)column(kColLogic, 0x810, &r.logic);       // optional
    (void)e; *out = r; return true;
}

// ---- ground / jump / dodge / float observation (naga #46662): READ-ONLY state samples + explicit scope failures ----
// Samples the PLAYER row (identity-bound, see observe_loco) from the existing jump-input hook: Jump row (+0x14 phase, +0x15 type,
// +0x20 index, +0x21, +0x22, raw 0x28 B), DodgeData (+0x30 used, +0x34 AIR used, raw 0x40 B), Airborne (+8 now, +9 previous, raw 0xc B),
// NextJumpRequest {counter,type} read AFTER the original, and the jump / dodge action flags (flag-A helper, RVAs 0x5ce6598 / 0x5ce65a0)
// of the CURRENT borrowed Player input row. One `S ...` text line per changed signature (+1 s heartbeat) through a bounded try-lock
// ring flushed by the worker; when the Player scope cannot be established a JSON `step_probe_scope` line names the reason (power-of-two
// counts) instead of silence. Nothing is written or called besides the pinned flag-A read (only on the identity-verified Player row).
using QpcFn = uint64_t (*)();
constexpr size_t kStateRing = 256; constexpr uint32_t kStateBudget = 32768, kStateHeartbeatMs = 1000;
enum Reason : uint8_t { kNoPlayerSource, kNoActorHandle, kNoWorld, kResolveFailed, kLocoObservationMissing, kLocoRowInvalid, kRowsUnreadable, kReasonCount };
inline const char* reason_name(uint8_t r) {
    static const char* const n[] = {"no_player_source", "no_actor_handle", "no_world", "resolve_failed", "loco_observation_missing", "loco_row_invalid", "rows_unreadable"};
    return r < kReasonCount ? n[r] : "?";
}
struct StateRec {
    uint32_t seq = 0, ms = 0, count = 0; uint64_t qpc = 0, jump_row = 0, dodge_row = 0, air_row = 0;
    uint8_t kind = 0, heartbeat = 0, reason = 0, jump_btn = 2, dodge_btn = 2, rows = 0;     // kind 0 sample, 1 scope failure; buttons 2 = unreadable; rows: 1 jump, 2 dodge, 4 air, 8 njr
    uint8_t jump[0x28] = {}, dodge[0x40] = {}, air[0xc] = {}, njr[2] = {};
    uint8_t owner = 0, rows2 = 0, jump2[0x28] = {}, dodge2[0x40] = {}, air2[0xc] = {};   // kind 2 (local loco capture): owner 0 UNATTRIBUTED / 1 verified Player row; *2 = POST-original copies
    uint32_t cc_pre = 0, cc_post = 0;
};
struct StateProbe {
    StateRec ring[kStateRing]; uint32_t head = 0, tail = 0; std::atomic<uint32_t> pushed{0}; uint64_t last_sig = 0; uint32_t last_ms = 0; bool have_sig = false;
    std::atomic<uint32_t> dropped{0}, fails[kReasonCount]{}; std::atomic_flag lock; QpcFn qpc = nullptr;
};
inline StateProbe& probe() { static StateProbe p; return p; }
inline void set_qpc_source(QpcFn f) { probe().qpc = f; }
inline void enable_dodge_flag_read(bool on) { env().dodge_flag_read = on; }   // opt-in only; default off (dodge state is still sampled through DodgeData +0x30/+0x34)
inline void probe_push(const StateRec& r) {
    StateProbe& p = probe();
    if (p.lock.test_and_set(std::memory_order_acquire)) { p.dropped.fetch_add(1, std::memory_order_relaxed); return; }
    if (p.pushed >= kStateBudget || p.head - p.tail >= kStateRing) p.dropped.fetch_add(1, std::memory_order_relaxed);
    else { p.ring[p.head % kStateRing] = r; ++p.head; ++p.pushed; }
    p.lock.clear(std::memory_order_release);
}
// An explicit, rate-limited reason instead of a silent skip.
inline void probe_fail(uint8_t reason) {
    StateProbe& p = probe();
    const uint32_t n = p.fails[reason].fetch_add(1, std::memory_order_relaxed) + 1;
    if ((n & (n - 1)) != 0 && n % 2048 != 0) return;
    StateRec r; r.kind = 1; r.reason = reason; r.count = n; r.qpc = p.qpc ? p.qpc() : 0;
    r.ms = combat_native::binding().clock ? combat_native::binding().clock() : 0;
    probe_push(r);
}
// Why is the Player scope unavailable? (called only when the locomotion observation has not bound a Player row yet)
inline void probe_diagnose() {
    Env& e = env(); Mail& m = mail();
    if (!e.player) return probe_fail(kNoPlayerSource);
    const uint64_t actor = e.player(), w = m.world.load(std::memory_order_relaxed);
    if (!actor) return probe_fail(kNoActorHandle);
    if (!w) return probe_fail(kNoWorld);
    PlayerRows pr;
    probe_fail(resolve_player(w, actor, &pr) ? kLocoObservationMissing : kResolveFailed);
}
inline bool read_action(uint64_t input_row, uint16_t action, bool* down) {
    Env& e = env();
    if (!e.pins_ok || action == 0xffff || !input_row || (action == e.dodge_action && !e.dodge_flag_read)) return false;
    *down = fn<uint8_t (*)(void*, uint16_t)>(0x22c77b0)(reinterpret_cast<void*>(static_cast<uintptr_t>(input_row)), action) != 0;
    return true;
}
inline uint64_t state_signature(const StateRec& r) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint8_t b) { h ^= b; h *= 1099511628211ull; };
    mix(r.jump_btn); mix(r.dodge_btn); mix(r.rows);
    if (r.rows & 1) { mix(r.jump[0x14]); mix(r.jump[0x15]); mix(r.jump[0x20]); mix(r.jump[0x21]); mix(r.jump[0x22]); }
    if (r.rows & 2) for (int i = 0x30; i < 0x38; ++i) mix(r.dodge[i]);
    if (r.rows & 4) { mix(r.air[8]); mix(r.air[9]); }
    if (r.rows & 8) { mix(r.njr[0]); mix(r.njr[1]); }
    return h;
}
// Post-original sample of the verified Player row.
inline void probe_sample(uint8_t jb, uint8_t db, uint64_t jump_row, uint64_t nj) {
    Env& e = env(); Mail& m = mail(); StateProbe& p = probe();
    StateRec r; r.jump_btn = jb; r.dodge_btn = db; r.jump_row = jump_row; r.dodge_row = m.dodge_row.load(std::memory_order_relaxed); r.air_row = m.air_row.load(std::memory_order_relaxed);
    r.qpc = p.qpc ? p.qpc() : 0; r.ms = combat_native::binding().clock ? combat_native::binding().clock() : 0;
    if (jump_row && copy_checked(e.copy, r.jump, jump_row, sizeof r.jump)) r.rows |= 1;
    if (r.dodge_row && copy_checked(e.copy, r.dodge, r.dodge_row, sizeof r.dodge)) r.rows |= 2;
    if (r.air_row && copy_checked(e.copy, r.air, r.air_row, sizeof r.air)) r.rows |= 4;
    if (nj && copy_checked(e.copy, r.njr, nj, 2)) r.rows |= 8;
    if (!(r.rows & 7)) return probe_fail(kRowsUnreadable);
    const uint64_t sig = state_signature(r);
    if (p.lock.test_and_set(std::memory_order_acquire)) { p.dropped.fetch_add(1, std::memory_order_relaxed); return; }
    const bool changed = !p.have_sig || p.last_sig != sig;
    if (changed || r.ms - p.last_ms >= kStateHeartbeatMs) {
        if (p.pushed >= kStateBudget || p.head - p.tail >= kStateRing) p.dropped.fetch_add(1, std::memory_order_relaxed);
        else {
            r.heartbeat = changed ? 0 : 1; r.seq = ++p.pushed; p.ring[p.head % kStateRing] = r; ++p.head;
            p.last_sig = sig; p.last_ms = r.ms; p.have_sig = true;
        }
    }
    p.lock.clear(std::memory_order_release);
}
// ---- fallback local-loco capture (naga #46818): the PLAYER-agnostic columns of the proven locomotion hook (row cols[0x12], DodgeData q0 /0x40,
// Jump q3 /0x28, Airborne q7 /0xc, CharacterCollisions q15 /0x20), copied with guarded reads before and after the original inside its loan.
// Rows are labelled OWNER UNKNOWN (own=0) unless the identity join succeeded (own=1); rows proven to belong to ANOTHER entity are skipped.
// No native call, no flag-A, no write: it is the diagnostic counterpart of H pl=0 / Y own=0, never a scope claim.
struct LocalSnap { uint64_t jump = 0; uint8_t rows = 0; uint32_t cc_count = 0; uint8_t jb[0x28] = {}, db[0x40] = {}, ab[0xc] = {}; };
inline bool local_capture(void** cols, LocalSnap* s) {
    Env& e = env(); uint8_t c[0x13 * 8];
    if (!e.copy || !copy_checked(e.copy, c, reinterpret_cast<uintptr_t>(cols), sizeof c)) return false;
    const uint64_t row = qword(c, 0x12 * 8);
    if (row >= combat_native::cols::kMaxRow) return false;
    s->jump = at(qword(c, 3 * 8), row, 0x28);
    const uint64_t dodge = at(qword(c, 0), row, 0x40), air = at(qword(c, 7 * 8), row, 0xc), cc = at(qword(c, 15 * 8), row, 0x20);
    if (s->jump && copy_checked(e.copy, s->jb, s->jump, sizeof s->jb)) s->rows |= 1;
    if (dodge && copy_checked(e.copy, s->db, dodge, sizeof s->db)) s->rows |= 2;
    if (air && copy_checked(e.copy, s->ab, air, sizeof s->ab)) s->rows |= 4;
    uint32_t n = 0;
    if (cc && copy_checked(e.copy, &n, cc + 8, 4) && n <= 4096) { s->cc_count = n; s->rows |= 8; }
    return s->rows != 0;
}
inline void local_mix(uint64_t& h, const LocalSnap& s) {
    auto mix = [&](uint8_t b) { h ^= b; h *= 1099511628211ull; };
    mix(s.rows);
    if (s.rows & 1) { mix(s.jb[0x14]); mix(s.jb[0x15]); mix(s.jb[0x20]); mix(s.jb[0x21]); mix(s.jb[0x22]); }
    if (s.rows & 2) for (int i = 0x30; i < 0x38; ++i) mix(s.db[i]);
    if (s.rows & 4) { mix(s.ab[8]); mix(s.ab[9]); }
    if (s.rows & 8) { mix(static_cast<uint8_t>(s.cc_count)); mix(static_cast<uint8_t>(s.cc_count >> 8)); }
}
struct LocalSeen { uint64_t row = 0, sig = 0; uint32_t ms = 0; bool used = false; };
inline void local_record(const LocalSnap& pre, const LocalSnap& post, uint8_t owner) {
    StateProbe& p = probe(); static LocalSeen seen[4];
    uint64_t sig = 1469598103934665603ull; local_mix(sig, pre); sig ^= 0x9e3779b97f4a7c15ull; local_mix(sig, post);
    StateRec r; r.kind = 2; r.owner = owner; r.jump_row = pre.jump; r.rows = pre.rows; r.rows2 = post.rows; r.cc_pre = pre.cc_count; r.cc_post = post.cc_count;
    std::memcpy(r.jump, pre.jb, sizeof r.jump); std::memcpy(r.dodge, pre.db, sizeof r.dodge); std::memcpy(r.air, pre.ab, sizeof r.air);
    std::memcpy(r.jump2, post.jb, sizeof r.jump2); std::memcpy(r.dodge2, post.db, sizeof r.dodge2); std::memcpy(r.air2, post.ab, sizeof r.air2);
    r.qpc = p.qpc ? p.qpc() : 0; r.ms = combat_native::binding().clock ? combat_native::binding().clock() : 0;
    if (p.lock.test_and_set(std::memory_order_acquire)) { p.dropped.fetch_add(1, std::memory_order_relaxed); return; }
    LocalSeen* slot = nullptr;
    for (LocalSeen& x : seen) if (x.used && x.row == pre.jump) slot = &x;
    if (!slot) for (LocalSeen& x : seen) if (!x.used) { slot = &x; break; }
    if (!slot) slot = &seen[pre.jump & 3];
    const bool changed = !slot->used || slot->row != pre.jump || slot->sig != sig;
    if (changed || r.ms - slot->ms >= kStateHeartbeatMs) {
        if (p.pushed >= kStateBudget || p.head - p.tail >= kStateRing) p.dropped.fetch_add(1, std::memory_order_relaxed);
        else {
            r.heartbeat = changed ? 0 : 1; r.seq = ++p.pushed; p.ring[p.head % kStateRing] = r; ++p.head;
            slot->used = true; slot->row = pre.jump; slot->sig = sig; slot->ms = r.ms;
        }
    }
    p.lock.clear(std::memory_order_release);
}
inline size_t state_hex(const uint8_t* b, size_t n, char* dst) { static const char d[] = "0123456789abcdef"; for (size_t i = 0; i < n; ++i) { dst[2 * i] = d[b[i] >> 4]; dst[2 * i + 1] = d[b[i] & 15]; } return 2 * n; }
// Worker side: `S ...` sample lines and JSON scope-failure lines; a busy lock skips the call.
inline unsigned flush_state(bool (*write)(const char*, size_t, void*), void* ctx, unsigned max_lines) {
    StateProbe& p = probe(); static StateRec batch[16]; static char line[1536];
    if (max_lines > 16) max_lines = 16;
    if (p.lock.test_and_set(std::memory_order_acquire)) return 0;
    unsigned n = 0;
    while (n < max_lines && p.tail != p.head) { batch[n++] = p.ring[p.tail % kStateRing]; ++p.tail; }
    p.lock.clear(std::memory_order_release);
    for (unsigned i = 0; i < n; ++i) {
        const StateRec& r = batch[i]; int o;
        if (r.kind == 1) {
            o = std::snprintf(line, sizeof line, "{\"event\":\"step_probe_scope\",\"reason\":\"%s\",\"count\":%u,\"qpc\":%llu,\"ms\":%u}\n", reason_name(r.reason), r.count,
                              static_cast<unsigned long long>(r.qpc), r.ms);
            if (o > 0 && !write(line, static_cast<size_t>(o), ctx)) break;
            continue;
        }
        if (r.kind == 2) {
            o = std::snprintf(line, sizeof line, "T %u qpc=%llu ms=%u hb=%u own=%u row=%llx cc=%u/%u rows=%u/%u pre_air=%u/%u post_air=%u/%u pre_j=%02x/%02x/%02x/%02x/%02x post_j=%02x/%02x/%02x/%02x/%02x pre_d=%u/%u post_d=%u/%u PJ:",
                r.seq, static_cast<unsigned long long>(r.qpc), r.ms, r.heartbeat, r.owner, static_cast<unsigned long long>(r.jump_row), r.cc_pre, r.cc_post, r.rows, r.rows2,
                r.air[8], r.air[9], r.air2[8], r.air2[9], r.jump[0x14], r.jump[0x15], r.jump[0x20], r.jump[0x21], r.jump[0x22], r.jump2[0x14], r.jump2[0x15], r.jump2[0x20], r.jump2[0x21], r.jump2[0x22],
                r.dodge[0x30], r.dodge[0x34], r.dodge2[0x30], r.dodge2[0x34]);
            if (o <= 0) continue;
            size_t k = static_cast<size_t>(o);
            k += state_hex(r.jump, sizeof r.jump, line + k);
            k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, " PD:")); k += state_hex(r.dodge, sizeof r.dodge, line + k);
            k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, " PA:")); k += state_hex(r.air, sizeof r.air, line + k);
            k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, " QJ:")); k += state_hex(r.jump2, sizeof r.jump2, line + k);
            k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, " QD:")); k += state_hex(r.dodge2, sizeof r.dodge2, line + k);
            k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, " QA:")); k += state_hex(r.air2, sizeof r.air2, line + k);
            line[k++] = '\n'; line[k] = 0;
            if (!write(line, k, ctx)) break;
            continue;
        }
        o = std::snprintf(line, sizeof line, "S %u qpc=%llu ms=%u hb=%u jb=%u db=%u rows=%u air=%u/%u j14=%02x j15=%02x j20=%02x j21=%02x j22=%02x d30=%u d34=%u njr=%02x/%02x J:",
            r.seq, static_cast<unsigned long long>(r.qpc), r.ms, r.heartbeat, r.jump_btn, r.dodge_btn, r.rows, r.air[8], r.air[9], r.jump[0x14], r.jump[0x15], r.jump[0x20], r.jump[0x21],
            r.jump[0x22], static_cast<uint32_t>(r.dodge[0x30]) | static_cast<uint32_t>(r.dodge[0x31]) << 8 | static_cast<uint32_t>(r.dodge[0x32]) << 16 | static_cast<uint32_t>(r.dodge[0x33]) << 24,
            static_cast<uint32_t>(r.dodge[0x34]) | static_cast<uint32_t>(r.dodge[0x35]) << 8 | static_cast<uint32_t>(r.dodge[0x36]) << 16 | static_cast<uint32_t>(r.dodge[0x37]) << 24, r.njr[0], r.njr[1]);
        if (o <= 0) continue;
        size_t k = static_cast<size_t>(o);
        k += state_hex(r.jump, sizeof r.jump, line + k);
        k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, " D:")); k += state_hex(r.dodge, sizeof r.dodge, line + k);
        k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, " A:")); k += state_hex(r.air, sizeof r.air, line + k);
        line[k++] = '\n'; line[k] = 0;
        if (!write(line, k, ctx)) break;
    }
    const uint32_t lost = p.dropped.exchange(0, std::memory_order_relaxed);
    if (lost) { const int o = std::snprintf(line, sizeof line, "{\"event\":\"step_probe_dropped\",\"count\":%u}\n", lost); if (o > 0) (void)write(line, static_cast<size_t>(o), ctx); }
    return n;
}

// Read only the column needed for attribution; locomotion columns remain required for step actuation.
inline bool resolve_player_column(uint64_t w,uint64_t actor,uint32_t hash,size_t stride,uint64_t* out) {
    if (!out) return false;
    *out=0; Env& e=env(); Loc first{},last{}; Meta before{},after{};
    bool player=false,dead=false;
    if (!e.copy || !actor || !stride || stride>0x810 || !locate(w,actor,&first) ||
        !read_meta(w,first.arch,&before) || !has_tag(before,kTagPlayer,&player) || !player ||
        !has_tag(before,kTagDead,&dead) || dead) return false;
    uint32_t keys[kMetaMax]{},offsets[kMetaMax]{};
    if (!read_u32s(before.keys,before.key_count,keys) ||
        !read_u32s(before.offsets,before.key_count,offsets)) return false;
    uint64_t resolved=0;
    for (uint32_t i=0;i<before.key_count;++i) {
        if (keys[i]!=hash) continue;
        const uint64_t base=offsets[i] ? at(first.chunk,0,0,offsets[i]) : 0;
        resolved=base ? at(base,first.row,stride) : 0; break;
    }
    uint8_t readable=0;
    if (!resolved || !copy_checked(e.copy,&readable,resolved,1) || !locate(w,actor,&last) ||
        first.chunk!=last.chunk || first.row!=last.row || first.arch!=last.arch ||
        !read_meta(w,last.arch,&after) || before.tags!=after.tags || before.keys!=after.keys ||
        before.offsets!=after.offsets || before.tag_count!=after.tag_count || before.key_count!=after.key_count)
        return false;
    *out=resolved; return true;
}

// ---- proven write sets through guarded read-modify-write (only changed bytes are written) ----
inline bool apply_row(uint64_t row, size_t row_size, void (*mutate)(uint8_t*)) {
    Env& e = env();
    if (!e.write || !e.copy || !row || row_size > 0x80) return false;
    uint8_t before[0x80], after[0x80];
    if (!copy_checked(e.copy, before, row, row_size)) return false;
    std::memcpy(after, before, row_size);
    mutate(after);
    return e.write(reinterpret_cast<void*>(static_cast<uintptr_t>(row)),after,row_size);
}
inline void mut_jump(uint8_t* r) { combat_mod::write_sets::jump_counter_reset(r); }
inline void mut_dodge(uint8_t* r) { combat_mod::write_sets::air_dodge_reset(r, true); }

// ---- hook 1: locomotion state body (read-only observation; forwards both arguments unchanged) ----
inline void observe_loco(void** cols, int* verdict = nullptr) {   // *verdict: 0 unknown, 1 verified Player row, 2 another entity's row
    Env& e = env(); Mail& m = mail(); auto& reg = feature_status::registry();
    if (verdict) *verdict = 0;
    const uint64_t w = m.world.load(std::memory_order_relaxed), actor = e.player ? e.player() : 0;
    if (!w || !actor) return probe_fail(!actor ? kNoActorHandle : kNoWorld);     // scope unknown: record nothing but say why
    uint8_t c[0x13 * 8];
    if (!copy_checked(e.copy, c, reinterpret_cast<uintptr_t>(cols), sizeof c)) { m.rows_invalid.fetch_add(1, std::memory_order_relaxed); return; }
    const uint64_t row = qword(c, 0x12 * 8);
    if (row >= combat_native::cols::kMaxRow) { m.rows_invalid.fetch_add(1, std::memory_order_relaxed); return; }
    const uint64_t jump = at(qword(c, 3 * 8), row, 0x28);
    PlayerRows pr;
    if (!jump || !resolve_player(w, actor, &pr)) { m.rows_invalid.fetch_add(1, std::memory_order_relaxed); return probe_fail(kResolveFailed); }
    if (jump != pr.jump) { m.not_player.fetch_add(1, std::memory_order_relaxed); if (verdict) *verdict = 2; return; }      // another entity's row
    if (at(qword(c, 7 * 8), row, 0xc) != pr.air || at(qword(c, 15 * 8), row, 0x20) != pr.cc || at(qword(c, 0), row, 0x40) != pr.dodge) { m.rows_invalid.fetch_add(1, std::memory_order_relaxed); return; }
    uint8_t a = 0;
    if (!copy_checked(e.copy, &a, pr.air + 8, 1)) { m.rows_invalid.fetch_add(1, std::memory_order_relaxed); return; }
    m.loco_updates.fetch_add(1, std::memory_order_relaxed);
    m.actor.store(actor, std::memory_order_relaxed); m.jump_row.store(pr.jump, std::memory_order_relaxed); m.dodge_row.store(pr.dodge, std::memory_order_relaxed); m.air_row.store(pr.air, std::memory_order_relaxed);
    m.airborne.store(a ? 1u : 0u, std::memory_order_relaxed);
    if (verdict) *verdict = 1;
    m.contact_handle.store(0, std::memory_order_relaxed);                       // the latest observation replaces the previous one
    uint8_t ccrow[0x20];
    if (!a || !copy_checked(e.copy, ccrow, pr.cc, sizeof ccrow)) return;
    uint32_t count = 0; std::memcpy(&count, ccrow + 8, 4);
    const uint64_t ptr = qword(ccrow, 0);
    if (count == 0 || count > 4096 || !ptr) return;
    const uint32_t take = count < 16 ? count : 16;
    uint8_t raw[16 * 12];
    if (!copy_checked(e.copy, raw, ptr, take * 12)) return;
    for (uint32_t i = 0; i < take; ++i) {
        uint64_t h; std::memcpy(&h, raw + i * 12, 8);
        if (static_cast<uint32_t>(h) == 0xffffffffu || h == 0 || !is_enemy(w, h)) continue;
        // CB53: these are melee target relations, never body-contact authority.
        // Leave contact_handle cleared until an actual contact source binds it.
        char d[96]; std::snprintf(d, sizeof d, "melee target relation kind=%u count=%u; not body contact", raw[i * 12 + 8], count);
        reg.event(Feature::EnemyStep, Event::Status, w, h, actor, kRvaLoco, d);
        return;
    }
}
inline void loco_detour(void** cols, void* x) {
    auto& b = combat_native::binding();
    const auto incoming = b.get_last_error ? b.get_last_error() : 0;
    const LocoFn orig = env().loco_orig;
    if (!orig) return;
    int verdict = 0; LocalSnap pre; bool have_pre = false;
    if (env().pins_ok) { have_pre = local_capture(cols, &pre); observe_loco(cols, &verdict); }
    if (b.set_last_error) b.set_last_error(incoming);
    orig(cols, x);
    const auto forwarded = b.get_last_error ? b.get_last_error() : 0;
    if (have_pre && verdict != 2) { LocalSnap post; if (local_capture(cols, &post)) local_record(pre, post, verdict == 1 ? 1 : 0); }   // own=0 = UNATTRIBUTED
    if (b.set_last_error) b.set_last_error(forwarded);
}

// ---- hook 2: jump input body ----
inline bool jump_down(uint64_t input_row, bool* down) {
    Env& e = env();
    if (!e.pins_ok || !e.have_jump_action || !input_row) return false;
    *down = fn<uint8_t (*)(void*, uint16_t)>(0x22c77b0)(reinterpret_cast<void*>(static_cast<uintptr_t>(input_row)), e.jump_action) != 0;
    return true;
}

// ---- jump-gate evidence (cb34/cb35): READ-ONLY copies of the native fact gate FUN_142939e10(list p2, factMap p3, category) ----
// p2 = {u64 records*, u32 count}, 0x60-byte records: +5 category byte, +0x50/+0x58 128-bit fact key. p3+0x18 is an abseil-style swiss table
// (ctrl* +0x18, slots* +0x20, mask +0x30; 0x50-byte slots {u128 key, ... byte value at +0x28, byte flag at +0x30 (0 = valid)}), probed exactly
// like FUN_141d9c850: hash = hi64+lo64 of (k0^k1)*0xde5fb9d2630458e9; start = hash>>7; control byte compare = hash&0x7f; 0x80 = empty.
// Nothing is written, nothing is injected. At most kGateBudget records are logged over a session, only on a FRESH press of the PLAYER row,
// and only when the (attack state, fact values) signature changed.
constexpr uint32_t kGateRecs = 32, kGateScan = 256, kGateBudget = 256;
struct GateRec { uint8_t head[0x10]; uint8_t cat; uint8_t key[16]; uint8_t found, flag, value; uint8_t slot[0x50]; };
struct GateSnap {
    uint64_t p2 = 0, p3 = 0; uint32_t total = 0, shown = 0; int emul = -1;
    uint8_t combo[0x30] = {}, s0[0x28] = {}, s4[0x28] = {}; bool have_combo = false, have_logic = false;
    GateRec rec[kGateRecs];
};
inline bool fact_find(uint64_t p3, const uint8_t key[16], GateRec* r) {
    Env& e = env(); uint64_t ctrl = 0, slots = 0, mask = 0, k0, k1;
    r->found = 0; r->flag = 0xff; r->value = 0; std::memset(r->slot, 0, sizeof r->slot);
    if (!copy_checked(e.copy, &ctrl, at(p3, 0, 0, 0x18), 8) || !copy_checked(e.copy, &slots, at(p3, 0, 0, 0x20), 8) || !copy_checked(e.copy, &mask, at(p3, 0, 0, 0x30), 8)) return false;
    if (!ctrl || !slots || mask == 0 || mask > (1ull << 24) || ((mask + 1) & mask) != 0) return true;            // empty/unplausible table: not found
    std::memcpy(&k0, key, 8); std::memcpy(&k1, key + 8, 8);
    const unsigned __int128 prod = static_cast<unsigned __int128>(k0 ^ k1) * 0xde5fb9d2630458e9ull;
    const uint64_t h = static_cast<uint64_t>(prod >> 64) + static_cast<uint64_t>(prod);
    const uint8_t h2 = static_cast<uint8_t>(h & 0x7f);
    uint64_t pos = h >> 7, off = 0;
    for (uint32_t probe = 0; probe < 64 && off <= mask + 16; ++probe) {
        pos &= mask;
        uint8_t g[16];
        if (!copy_checked(e.copy, g, at(ctrl, pos, 1), sizeof g)) return false;
        bool empty = false;
        for (uint32_t i = 0; i < 16; ++i) {
            if (g[i] == 0x80) empty = true;
            if (g[i] != h2) continue;
            const uint64_t idx = (pos + i) & mask;
            uint8_t slot[0x50];
            if (!copy_checked(e.copy, slot, at(slots, idx, 0x50), sizeof slot)) return false;
            if (std::memcmp(slot, key, 16) == 0) { r->found = 1; r->flag = slot[0x30]; r->value = slot[0x28]; std::memcpy(r->slot, slot, sizeof slot); return true; }
        }
        if (empty) return true;                                                                              // an empty control byte ends the probe: absent
        off += 16; pos += off;
    }
    return true;
}
inline bool gate_capture(uint64_t p2, uint64_t p3, const PlayerRows& pr, GateSnap* g) {
    Env& e = env(); uint64_t list = 0; uint32_t count = 0;
    g->p2 = p2; g->p3 = p3;
    if (!p2 || !p3 || !copy_checked(e.copy, &list, at(p2, 0, 0), 8) || !copy_checked(e.copy, &count, at(p2, 0, 0, 8), 4) || !list || count > kGateScan) return false;
    g->total = count; g->shown = 0; g->emul = 1;
    bool decided = false;
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t rec[0x60];
        if (!copy_checked(e.copy, rec, at(list, i, 0x60), sizeof rec)) return false;
        if (i < kGateRecs) {
            GateRec& r = g->rec[g->shown++]; std::memcpy(r.head, rec, 0x10); r.cat = rec[5]; std::memcpy(r.key, rec + 0x50, 16);
            if (!fact_find(p3, r.key, &r)) return false;
        }
        if (!decided && rec[5] == 1) {                                                                       // the native gate decides on the FIRST category-1 record
            GateRec t; if (!fact_find(p3, rec + 0x50, &t)) return false;
            g->emul = (t.found && t.flag == 0) ? t.value : 1; decided = true;
        }
    }
    g->have_combo = pr.combo && copy_checked(e.copy, g->combo, pr.combo, 0x30);
    g->have_logic = pr.logic && copy_checked(e.copy, g->s0, pr.logic, sizeof g->s0) && copy_checked(e.copy, g->s4, pr.logic + 4 * 0x28, sizeof g->s4);
    return true;
}
inline size_t hex_to(const uint8_t* p, size_t n, char* dst) { static const char d[] = "0123456789abcdef"; for (size_t i = 0; i < n; ++i) { dst[2 * i] = d[p[i] >> 4]; dst[2 * i + 1] = d[p[i] & 15]; } return 2 * n; }
inline uint64_t gate_signature(const GateSnap& g) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint8_t b) { h ^= b; h *= 1099511628211ull; };
    for (uint32_t i = 0; i < g.shown; ++i) { mix(g.rec[i].cat); for (uint8_t b : g.rec[i].key) mix(b); mix(g.rec[i].found); mix(g.rec[i].flag); mix(g.rec[i].value); }
    for (int i = 0; i < 4; ++i) mix(g.have_combo ? g.combo[0x20 + i] : 0);           // current node
    for (int i = 0; i < 4; ++i) mix(g.have_combo ? g.combo[0x2c + i] : 0);           // phase
    mix(g.have_logic ? g.s0[0x1c] : 0xff); mix(g.have_logic ? g.s4[0x1c] : 0xff);     // slot 0 / slot 4 state flags
    return h;
}
inline std::atomic_flag& gate_lock() { static std::atomic_flag f; return f; }
inline std::atomic<uint32_t>& gate_left() { static std::atomic<uint32_t> n{kGateBudget}; return n; }
inline std::atomic<uint32_t>& gate_seq() { static std::atomic<uint32_t> n{0}; return n; }
// Logs one line (needs queue acceptance, otherwise the signature is not consumed and a later press retries). `res` = NextJumpRequest after the
// original: {counter, type}; type 3 = the native request was refused.
inline void gate_log(const GateSnap& g, uint64_t w, uint64_t actor, const uint8_t res[2], bool res_ok) {
    Env& e = env();
    if (!e.accept || g.shown == 0 || gate_left().load(std::memory_order_relaxed) < g.shown) return;
    static uint64_t sigs[8]; static uint32_t nsig = 0;
    const uint64_t sig = gate_signature(g);
    for (uint32_t i = 0; i < (nsig < 8 ? nsig : 8); ++i) if (sigs[i] == sig) return;
    static char line[16 * 1024];
    int n = std::snprintf(line, sizeof line, "J %u w=%llx actor=%llx p2=%llx p3=%llx total=%u shown=%u emul=%d res=%s%02x/%02x combo=", gate_seq().fetch_add(1) + 1,
                          static_cast<unsigned long long>(w), static_cast<unsigned long long>(actor), static_cast<unsigned long long>(g.p2),
                          static_cast<unsigned long long>(g.p3), g.total, g.shown, g.emul, res_ok ? "" : "?", res[0], res[1]);
    if (n < 0) return;
    size_t o = static_cast<size_t>(n);
    auto put = [&](const char* tag, const uint8_t* p, size_t len, bool ok) {
        o += static_cast<size_t>(std::snprintf(line + o, sizeof line - o, "%s", tag));
        if (ok) o += hex_to(p, len, line + o); else line[o++] = '-';
    };
    put("", g.combo, 0x30, g.have_combo); put(" s0=", g.s0, sizeof g.s0, g.have_logic); put(" s4=", g.s4, sizeof g.s4, g.have_logic);
    o += static_cast<size_t>(std::snprintf(line + o, sizeof line - o, " R:"));
    for (uint32_t i = 0; i < g.shown && o + 400 < sizeof line; ++i) {
        const GateRec& r = g.rec[i];
        o += static_cast<size_t>(std::snprintf(line + o, sizeof line - o, "%s%u,%u,", i ? ";" : "", i, r.cat));
        o += hex_to(r.key, 16, line + o);
        o += static_cast<size_t>(std::snprintf(line + o, sizeof line - o, ",%u,%02x,%02x,", r.found, r.flag, r.value));
        o += hex_to(r.head, 0x10, line + o); line[o++] = ',';
        o += hex_to(r.slot, sizeof r.slot, line + o);
    }
    line[o++] = '\n'; line[o] = 0;
    if (!e.accept(line, o)) return;
    sigs[nsig++ % 8] = sig;
    gate_left().fetch_sub(g.shown, std::memory_order_relaxed);
}

inline std::atomic<bool> mutation_fault{false};
inline void jump_detour(void** cols, void* p2, void* p3, float* dt) {
    const auto incoming=combat_native::binding().get_last_error ? combat_native::binding().get_last_error() : 0;
    Env& e = env(); Mail& m = mail(); auto& reg = feature_status::registry();
    const JumpFn orig = e.jump_orig;
    if (!orig) return;
    static GateSnap snap; bool have_snap = false, locked = false;
    bool armed = false, applied = false, sample = false; uint64_t nj = 0, handle = 0, w = 0, actor = 0, sample_row = 0; uint8_t jb = 2, db = 2;
    uint64_t reset_jump=0,reset_dodge=0,reset_air_attacks=0;
    uint32_t before_air_attacks=0;
    uint8_t before_jump[combat_mod::write_sets::kJumpRow]{},before_dodge[combat_mod::write_sets::kDodgeRow]{};
    const auto rollback=[&]() {
        const bool j=reset_jump && e.write(reinterpret_cast<void*>(static_cast<uintptr_t>(reset_jump)),before_jump,sizeof(before_jump));
        const bool d=reset_dodge && e.write(reinterpret_cast<void*>(static_cast<uintptr_t>(reset_dodge)),before_dodge,sizeof(before_dodge));
        const bool a=reset_air_attacks && e.write(reinterpret_cast<void*>(static_cast<uintptr_t>(reset_air_attacks+combat_mod::write_sets::kAirAttacksUsed)),&before_air_attacks,sizeof before_air_attacks);
        if (!j || !d || !a) {
            mutation_fault.store(true);
            proof().contact=false;
            reg.set_state(Feature::EnemyStep,State::Disabled,"resource reset rollback failed");
            reg.event(Feature::EnemyStep,Event::Exception,w,handle,reset_jump,kRvaJump,"resource reset rollback failed");
        }
    };
    if (e.pins_ok) {
        uint8_t c[9 * 8];
        if (copy_checked(e.copy, c, reinterpret_cast<uintptr_t>(cols), sizeof c)) {
            const uint64_t row = qword(c, 8 * 8);
            const bool ok = row < combat_native::cols::kMaxRow;
            const uint64_t jump = ok ? at(qword(c, 3 * 8), row, 0x28) : 0;
            const uint64_t input = ok ? at(qword(c, 1 * 8), row, 0x40) : 0;
            nj = ok ? at(qword(c, 4 * 8), row, 2) : 0;
            bool down = false;
            if (jump && !m.jump_row.load(std::memory_order_relaxed)) probe_diagnose();                 // Player row not bound yet: say why
            if (jump && jump == m.jump_row.load(std::memory_order_relaxed) && jump_down(input, &down)) {      // only the observed Player row
                bool ddown = false; jb = down ? 1 : 0; db = read_action(input, e.dodge_action, &ddown) ? (ddown ? 1 : 0) : 2; sample = true; sample_row = jump;
                const uint32_t prev = m.prev_down.exchange(down ? 1u : 0u, std::memory_order_relaxed);
                const bool fresh = down && !prev;                                                        // a held jump never repeats
                w = m.world.load(std::memory_order_relaxed); actor = m.actor.load(std::memory_order_relaxed);
                if (fresh && e.accept && gate_left().load(std::memory_order_relaxed) != 0 && !gate_lock().test_and_set(std::memory_order_acquire)) {
                    locked = true;                                                                       // try once; busy = skip this press
                    PlayerRows pr;
                    if (w && resolve_player(w, actor, &pr) && pr.jump == jump) have_snap = gate_capture(reinterpret_cast<uint64_t>(p2), reinterpret_cast<uint64_t>(p3), pr, &snap);
                }
                handle = 0;
                PlayerRows current{}; uint8_t airborne=0;
                const bool current_player=fresh && reg.active(Feature::EnemyStep) && w && actor && e.player && e.player()==actor &&
                    resolve_player(w,actor,&current) && current.jump==jump &&
                    copy_checked(e.copy,&airborne,current.air+8,1);
                const auto reader=e.contact_now.load();
                const bool actual_contact=current_player && airborne!=0 && reader && reader(w,actor,&handle);
                armed = !mutation_fault.load() && reg.active(Feature::EnemyStep) && fresh && current_player && airborne != 0 &&
                    actual_contact && handle != 0 && handle!=actor && proof().contact && proof().cancel;
                if (armed) {
                    uint64_t air_attacks=0;
                    armed=e.write && e.can_write &&
                        resolve_player_column(w,actor,kColAirAttacks,combat_mod::write_sets::kAirAttacksRow,&air_attacks) &&
                        e.can_write(reinterpret_cast<void*>(static_cast<uintptr_t>(air_attacks+combat_mod::write_sets::kAirAttacksUsed)),sizeof before_air_attacks) &&
                        e.can_write(reinterpret_cast<void*>(static_cast<uintptr_t>(jump)),sizeof(before_jump)) &&
                        e.can_write(reinterpret_cast<void*>(static_cast<uintptr_t>(current.dodge)),sizeof(before_dodge)) &&
                        e.can_write(reinterpret_cast<void*>(static_cast<uintptr_t>(nj)),2) &&
                        copy_checked(e.copy,before_jump,jump,sizeof(before_jump)) &&
                        copy_checked(e.copy,before_dodge,current.dodge,sizeof(before_dodge)) &&
                        copy_checked(e.copy,&before_air_attacks,air_attacks+combat_mod::write_sets::kAirAttacksUsed,sizeof before_air_attacks);
                    if (armed) reset_air_attacks=air_attacks;
                    else reg.event(Feature::EnemyStep,Event::Reject,w,handle,jump,kRvaJump,"entity contact; landing resource reset preflight refused");
                }
                if (armed) {
                    reset_jump=jump; reset_dodge=current.dodge;
                    reg.event(Feature::EnemyStep, Event::Attempt, w, handle, jump, kRvaJump, "fresh jump while airborne on current entity collider contact");
                    applied = apply_row(jump, combat_mod::write_sets::kJumpRow, mut_jump);
                    if (applied) applied=apply_row(current.dodge, combat_mod::write_sets::kDodgeRow, mut_dodge);
                    if (applied) {
                        uint8_t after[combat_mod::write_sets::kAirAttacksRow]{};
                        combat_mod::write_sets::air_attack_reset(after);
                        applied=e.write(reinterpret_cast<void*>(static_cast<uintptr_t>(reset_air_attacks+combat_mod::write_sets::kAirAttacksUsed)),after+combat_mod::write_sets::kAirAttacksUsed,sizeof before_air_attacks);
                    }
                    if (!applied) {
                        rollback(); armed=false;
                        reg.event(Feature::EnemyStep, Event::Exception, w, handle, jump, kRvaJump, "resource reset failed; restored prior rows");
                    }
                }
            }
        }
    }
    if (combat_native::binding().set_last_error) combat_native::binding().set_last_error(incoming);
    orig(cols, p2, p3, dt);                                                                    // native logic decides/starts the jump
    const auto forwarded=combat_native::binding().get_last_error ? combat_native::binding().get_last_error() : 0;
    if (sample) probe_sample(jb, db, sample_row, nj);
    if (locked) {
        if (have_snap) { uint8_t res[2] = {0xff, 0xff}; const bool ok = nj && copy_checked(e.copy, res, nj, 2); gate_log(snap, w, actor, res, ok); }
        gate_lock().clear(std::memory_order_release);
    }
    if (armed && applied) {
        uint8_t original_request[2]={0xff,0xff};
        const bool have_request=nj && copy_checked(e.copy,original_request,nj,sizeof(original_request));
        // A reset index alone makes handle_input choose airborne request {1,1}.
        // The accepted stomp must consume the first jump, leaving the native
        // unlocked capacity minus one. Preserve the native refusal; never
        // turn a denied request into a jump.
        const uint8_t first_jump[2]={0,0};
        if (have_request && original_request[1] != 3 &&
            e.write(reinterpret_cast<void*>(static_cast<uintptr_t>(nj)),first_jump,sizeof(first_jump))) {
            m.steps_applied.fetch_add(1, std::memory_order_relaxed);
            reg.event(Feature::EnemyStep, Event::Applied, w, handle, nj, kRvaJump, "jump, float, dash and air-attack usage restored; first native jump requested; native jump cancel retained");
        } else {
            if (have_request && original_request[1]!=3 &&
                !e.write(reinterpret_cast<void*>(static_cast<uintptr_t>(nj)),original_request,sizeof(original_request))) {
                proof().contact=false;
                mutation_fault.store(true);
                reg.set_state(Feature::EnemyStep,State::Disabled,"jump request rollback failed");
                reg.event(Feature::EnemyStep,Event::Exception,w,handle,nj,kRvaJump,"jump request rollback failed");
            }
            rollback();
            reg.event(Feature::EnemyStep, Event::Reject, w, handle, nj, kRvaJump, "resource reset attempted; native jump refused or request write failed");
        }
    }
    if (combat_native::binding().set_last_error) combat_native::binding().set_last_error(forwarded);
}

inline void set_originals(LocoFn loco, JumpFn jump) { env().loco_orig = loco; env().jump_orig = jump; }
inline void set_hooked(bool loco, bool jump) { env().loco_hooked = loco; env().jump_hooked = jump; }
inline void set_player_source(PlayerFn fn_) { env().player = fn_; }
inline void set_raw_accept(bool (*accept)(const char*, size_t)) { env().accept = accept; }

// Publishes the status from the real prerequisites. Active only when every prerequisite and proof holds; otherwise Unbound with the list.
inline void refresh_status() {
    feature_status::registry().set_state(Feature::EnemyStep,State::Disabled,"removed at user request");
}

// Lock and directional attacks share this reader; they do not need step writes or hooks.
inline bool init_reader(uint64_t base, CopyFn cp) {
    Env& e=env();
    e.base=base; e.copy=cp; e.write=nullptr;
    return base && cp;
}

inline bool init(uint64_t base, CopyFn cp, WriteFn wr, feature_status::LogFn sink) {
    Env& e = env(); auto& reg = feature_status::registry();
    reg.set_sink(sink);
    e.base = base; e.copy = cp; e.write = wr;
    static const char* const names[] = {"loco_state", "jump_input", "input_flag_a"};
    const char* bad = nullptr;
    e.pins_ok = native_pins::verify_set(cp, base, names, sizeof names / sizeof names[0], &bad);
    uint16_t act = 0xffff;                                                       // DAT_145ceb488 = jump action index (cb7)
    e.have_jump_action = e.pins_ok && copy_checked(cp, &act, at(base, 0, 0, 0x5ce6598), 2) && act != 0xffff;
    e.jump_action = act;
    uint16_t dact = 0xffff;                                                      // DAT_145ceb490 = dodge/dash action index (cb9)
    e.have_dodge_action = e.pins_ok && copy_checked(cp, &dact, at(base, 0, 0, 0x5ce65a0), 2) && dact != 0xffff;
    e.dodge_action = dact;
    if (!e.pins_ok) {
        char why[96]; std::snprintf(why, sizeof why, "pin_mismatch:%s", bad ? bad : "?");
        reg.set_state(Feature::EnemyStep, State::Disabled, why);
        return false;
    }
    refresh_status();
    return true;
}
}  // namespace step_binding
