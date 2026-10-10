// Native binding layer for combat_mod.h: column-struct observers + copied-row mailbox + chooser readers.
// PASSIVE and TELEMETRY-ONLY BY DEFAULT: nothing here mutates the game, installs a hook, or runs unless root's
// detours call the observe_* entry points, and NO direction sample can be produced until the integrator installs
// evidence-backed scope sources. Every native read goes through an injected CopyFn (host tests: plain copy over fake
// memory; the Windows build must install a guarded copy, e.g. VirtualQuery-checked) and only COPIES bytes.
// Evidence: evidence/re/combat-contracts/cb13-column-struct-layouts.md (static, disassembly-derived; offsets are
// candidates until a runtime packet confirms them). Review CB13 (naga #14097, #14110) rules enforced here:
//  - a sample needs lock-on and movement snapshots that carry the SAME trusted Scope{world, frame, actor} (all
//    nonzero), equal to the CURRENT chooser scope; no stamp is ever derived from addresses or time;
//  - only POST-original captures can ever be sampled (a pre-original capture pairs the current target handle with the
//    previous update's LockStatus/Movement output); a post capture revalidates the scope and the row identity before
//    AND after copying, and preserves the original's LastError (injected get/set, required for post capture);
//  - the former freshness-by-clock and A/B/A row heuristics are telemetry only.
// Not provided yet (fail-closed => finishers stay unhandled): the scope sources themselves, finisher node lookup and
// native legality readers.
#pragma once
#include "combat_mod.h"

namespace combat_native {
using namespace combat_mod;

using CopyFn = bool (*)(void* dst, const void* src, size_t n);
using ClockFn = uint32_t (*)();

// Trusted identity of one update of one actor in one world. All three must be nonzero and come from proven sources.
struct Scope {
    uint64_t world = 0, frame = 0, actor = 0;
    bool complete() const { return world != 0 && frame != 0 && actor != 0; }
    bool operator==(const Scope& o) const { return world == o.world && frame == o.frame && actor == o.actor; }
};
enum class Phase : uint8_t { Pre, Post };   // relative to the ORIGINAL body (pre = previous update's outputs)
// Observer scope source: stamps for the (column struct, SysCtx) of the body being observed. Evidence-backed only.
using ScopeFn = bool (*)(const void* column_struct, const void* ctx, Scope* out);
using ChooserScopeFn = bool (*)(const ChooserArgs& args, Scope* out);   // the CURRENT chooser's scope, same sources
using GetErrFn = uint32_t (*)();
using SetErrFn = void (*)(uint32_t);

// Plausible user-mode pointer test (cheap pre-filter only; the injected CopyFn is the real guard).
inline bool plausible(uint64_t p, size_t n) {
    return p >= 0x10000 && p <= 0x00007fffffffffffull && n != 0 && p + n > p && p + n <= 0x00007fffffffffffull;
}
// Overflow-checked address arithmetic for pointers read from native memory (review CB29): `base + index*stride + offset`.
// Returns 0 (never a plausible address) when base is null/implausible or ANY step overflows or leaves the user-mode range, so a
// wrapped sum can never land on a "plausible" low address. copy_checked() still range-checks the final span.
constexpr uint64_t kUserMax = 0x00007fffffffffffull;
inline uint64_t at(uint64_t base, uint64_t index, uint64_t stride, uint64_t offset = 0) {
    if (base < 0x10000 || base > kUserMax) return 0;
    if (stride != 0 && index > (kUserMax - base) / stride) return 0;        // index*stride would pass the user-mode limit (also covers u64 overflow)
    const uint64_t scaled = index * stride;
    if (offset > kUserMax - base - scaled) return 0;
    return base + scaled + offset;
}
inline bool copy_checked(CopyFn cp, void* dst, uint64_t src, size_t n) {
    return cp && plausible(src, n) && cp(dst, reinterpret_cast<const void*>(static_cast<uintptr_t>(src)), n);
}

// ---- column structs (cb13): a per-entity body receives `cols` = array of column base pointers + a row index ----
namespace cols {
constexpr size_t kMaxRow = 1u << 20;             // sanity bound on the row index
// FUN_1427e6cb0 camera_lock_on::on_update (rcx = column struct; 0x50 bytes read)
constexpr size_t kLockOnSize = 0x50;
constexpr size_t kLockOnSelected = 0x08;         // q1: CameraSelectedTarget, stride 0x10, u64 entity handle
constexpr size_t kLockOnAim = 0x10;              // q2: AimData, stride 0x40 (cb13 ASM); q4 is Input
constexpr size_t kLockOnStatus = 0x30;           // q6: LockStatus, stride 0xf0
constexpr size_t kLockOnRow = 0x48;              // row index (u64)
// FUN_14228ec70 player_input::update_wanted_movement_input (rcx = column struct; 0x28 bytes read)
constexpr size_t kMoveSize = 0x28;
constexpr size_t kMoveMovement = 0x00;           // q0: Movement, stride 0x80
constexpr size_t kMoveAim = 0x10;                // q2: AimData, stride 0x40
constexpr size_t kMoveRow = 0x20;                // row index (u64)
}  // namespace cols

inline uint64_t qword(const uint8_t* base, size_t off) { uint64_t v; std::memcpy(&v, base + off, 8); return v; }

// Row addresses derived from the column struct (identity only; re-derived to detect a moved/stale row).
struct LockOnRows { uint64_t status, aim, selected; };
struct MoveRows { uint64_t movement, aim; };
inline bool lockon_rows(const void* column_struct, CopyFn cp, LockOnRows* out) {
    uint8_t c[cols::kLockOnSize];
    if (!copy_checked(cp, c, reinterpret_cast<uintptr_t>(column_struct), sizeof c)) return false;
    const uint64_t row = qword(c, cols::kLockOnRow);
    const uint64_t status = qword(c, cols::kLockOnStatus), aim = qword(c, cols::kLockOnAim), sel = qword(c, cols::kLockOnSelected);
    if (row >= cols::kMaxRow || !status || !aim || !sel) return false;
    const LockOnRows r = {at(status, row, 0xf0), at(aim, row, 0x40), at(sel, row, 0x10)};
    if (!r.status || !r.aim || !r.selected) return false;                      // a wrapped/implausible sum is never attributed to a row
    *out = r;
    return true;
}
inline bool movement_rows(const void* column_struct, CopyFn cp, MoveRows* out) {
    uint8_t c[cols::kMoveSize];
    if (!copy_checked(cp, c, reinterpret_cast<uintptr_t>(column_struct), sizeof c)) return false;
    const uint64_t row = qword(c, cols::kMoveRow), mv = qword(c, cols::kMoveMovement), aim = qword(c, cols::kMoveAim);
    if (row >= cols::kMaxRow || !mv || !aim) return false;
    const MoveRows r = {at(mv, row, 0x80), at(aim, row, 0x40)};
    if (!r.movement || !r.aim) return false;
    *out = r;
    return true;
}

struct LockOnCopy { uint8_t lock[rows::kMinLock]; uint8_t aim[rows::kMinAim]; uint64_t target; uint64_t row_id; uint64_t aim_id; };
struct MoveCopy { uint8_t move[rows::kMinMovement]; uint64_t row_id; uint64_t aim_id; };

inline bool capture_lockon(const void* column_struct, CopyFn cp, LockOnCopy* out) {
    LockOnRows r{};
    if (!lockon_rows(column_struct, cp, &r)) return false;
    LockOnCopy t{};
    if (!copy_checked(cp, t.lock, r.status, sizeof t.lock) || !copy_checked(cp, t.aim, r.aim, sizeof t.aim) ||
        !copy_checked(cp, &t.target, r.selected, sizeof t.target)) return false;
    if (static_cast<uint32_t>(t.target) == 0xffffffffu) t.target = 0;   // index -1 = no target (world generation check is root's)
    t.row_id = r.status;
    t.aim_id = r.aim;
    *out = t;
    return true;
}
inline bool capture_movement(const void* column_struct, CopyFn cp, MoveCopy* out) {
    MoveRows r{};
    if (!movement_rows(column_struct, cp, &r)) return false;
    MoveCopy t{};
    if (!copy_checked(cp, t.move, r.movement, sizeof t.move)) return false;
    t.row_id = r.movement;
    t.aim_id = r.aim;
    *out = t;
    return true;
}

struct Counters {   // telemetry only; never an authority
    uint32_t lock_posts = 0, move_posts = 0, pre_posts = 0, no_scope_source = 0, no_error_guard = 0, scope_rejected = 0,
             capture_failed = 0, row_alternations = 0, samples_ok = 0, samples_refused = 0, invalidations = 0;
};

// Latest lock-on + movement copies with the trusted stamps they were captured under.
class Mailbox {
public:
    // `scope` is the trusted stamp (zero/incomplete when unknown); `phase` relative to the original body.
    void post_lockon(const LockOnCopy& c, const Scope& scope, Phase phase) {
        lock();
        if (have_lock_ && lock_.row_id != c.row_id) ++counters_.row_alternations;
        lock_ = c; lock_scope_ = scope; lock_phase_ = phase; have_lock_ = true; ++counters_.lock_posts;
        if (phase == Phase::Pre) ++counters_.pre_posts;
        unlock();
    }
    void post_movement(const MoveCopy& c, const Scope& scope, Phase phase) {
        lock();
        if (have_move_ && move_.row_id != c.row_id) ++counters_.row_alternations;
        move_ = c; move_scope_ = scope; move_phase_ = phase; have_move_ = true; ++counters_.move_posts;
        if (phase == Phase::Pre) ++counters_.pre_posts;
        unlock();
    }
    // A sample needs: a complete CURRENT chooser scope; both snapshots POST-original, complete, and exactly equal to it;
    // the same AimData row in both bodies (necessary, never sufficient); then valid rows.
    bool sample(const Scope& chooser_scope, uint32_t now_ms, DirectionSample* out) {
        lock();
        const bool ok = qualified(chooser_scope) &&
                        rows::direction_sample(lock_.lock, move_.move, lock_.aim, lock_.target, now_ms, out);
        ++(ok ? counters_.samples_ok : counters_.samples_refused);
        unlock();
        return ok;
    }
    // Every rejected/failed capture of a stream invalidates that stream's previous snapshot (a stale pair must not survive).
    void invalidate_lockon() { lock(); have_lock_ = false; ++counters_.invalidations; unlock(); }
    void invalidate_movement() { lock(); have_move_ = false; ++counters_.invalidations; unlock(); }
    void note_no_scope_source() { lock(); ++counters_.no_scope_source; unlock(); }
    void note_no_error_guard() { lock(); ++counters_.no_error_guard; unlock(); }
    void note_scope_rejected() { lock(); ++counters_.scope_rejected; unlock(); }
    void note_capture_failed() { lock(); ++counters_.capture_failed; unlock(); }
    Counters counters() { lock(); const Counters c = counters_; unlock(); return c; }
    void reset() { lock(); have_lock_ = have_move_ = false; lock_scope_ = move_scope_ = Scope{}; counters_ = Counters{}; unlock(); }
private:
    bool qualified(const Scope& s) const {
        return have_lock_ && have_move_ && s.complete() && lock_phase_ == Phase::Post && move_phase_ == Phase::Post &&
               lock_scope_.complete() && move_scope_.complete() && lock_scope_ == s && move_scope_ == s &&
               lock_.aim_id == move_.aim_id;
    }
    void lock() { while (flag_.test_and_set(std::memory_order_acquire)) {} }
    void unlock() { flag_.clear(std::memory_order_release); }
    std::atomic_flag flag_;
    LockOnCopy lock_{}; MoveCopy move_{};
    Scope lock_scope_{}, move_scope_{};
    Phase lock_phase_ = Phase::Pre, move_phase_ = Phase::Pre;
    bool have_lock_ = false, have_move_ = false;
    Counters counters_{};
};

// ---- finisher node table + native legality (cb17; prepared, EMPTY/UNSET by default => fail-closed) ----
// Graph layout (cb1/cb15, static candidates): node 0x38 B {+0 connections ptr, +8 count, +0x10 id, +0x18 stateAllow,
// +0x20 type, +0x24 form-node index, +0x2c attack-table index}; connection {u32 target, u32 arg}; ComboData C {+0 node
// array}; cfg {+8 defs (0x140 B: +0 id, +0x70 category), +0x28 attack table (0x118 B: +0x90/+0x98 requirement ids,
// +0xa0/+0xa8 post ids, +0xb1 flag)}; Loadout = int array indexed by def category.
namespace graph {
constexpr size_t kNode = 0x38, kEntry = 0x118, kDef = 0x140, kMaxConn = 32, kMaxIndex = 4096, kMaxCategory = 4, kMaxList = 64;   // Loadout row = 0x10 B = 4 ints: category must be < 4 (a larger index would read the NEXT entity's row)
constexpr uint32_t kNone = 0xffffffffu;
inline uint32_t u32(const uint8_t* p, size_t off) { uint32_t v; std::memcpy(&v, p + off, 4); return v; }
inline uint64_t u64(const uint8_t* p, size_t off) { uint64_t v; std::memcpy(&v, p + off, 8); return v; }
struct Conn { uint32_t target, arg; };
// Copies the current node's connections (the chooser iterates exactly these, in order).
inline bool connections(CopyFn cp, const ChooserArgs& a, Conn* out, size_t* n) {
    uint8_t head[0x10];
    if (!copy_checked(cp, head, reinterpret_cast<uintptr_t>(a.node), sizeof head)) return false;
    const uint32_t count = u32(head, 8);
    if (count == 0 || count > kMaxConn) return false;
    if (!copy_checked(cp, out, u64(head, 0), count * sizeof(Conn))) return false;
    *n = count;
    return true;
}
inline bool node_at(CopyFn cp, uint64_t nodes, uint32_t idx, uint8_t* out) {
    return nodes && idx < kMaxIndex && copy_checked(cp, out, at(nodes, idx, kNode), kNode);
}
// Form definition id + category of a node (via its form node), or false.
inline bool form_def(CopyFn cp, uint64_t nodes, uint64_t defs, const uint8_t* node, uint32_t* id, uint32_t* category) {
    const uint32_t form = u32(node, 0x24);
    if (form == kNone) return false;
    uint8_t f[kNode], d[kDef];
    if (!node_at(cp, nodes, form, f)) return false;
    const uint32_t di = u32(f, 0x2c);
    if (!defs || di >= kMaxIndex || !copy_checked(cp, d, at(defs, di, kDef), sizeof d)) return false;
    *id = u32(d, 0);
    *category = u32(d, 0x70);
    return true;
}
inline bool form_def_id(CopyFn cp, uint64_t nodes, uint64_t defs, const uint8_t* node, uint32_t* id) {
    uint32_t category = 0;
    return form_def(cp, nodes, defs, node, id, &category);
}
}  // namespace graph

// node_index = position in the shared node array C[0]; node_id = node +0x10 (content id) and form_def_id validate it at read time.
struct FinisherEntry { uint32_t form_def_id; Finisher finisher; uint32_t node_id; uint32_t node_index;
    uint32_t air_node_id=graph::kNone,air_node_index=graph::kNone; };
// Filled ONLY from evidence (graph dump bound to names). Empty by default.
class NodeTable {
public:
    static constexpr size_t kCap = 48;
    // Rejects: Finisher::None, unset ids, duplicates of (form_def_id, finisher), a full table.
    bool add(const FinisherEntry& e) {
        if (e.finisher == Finisher::None || e.form_def_id == 0 || e.form_def_id == graph::kNone || e.node_id == graph::kNone || e.node_index >= graph::kMaxIndex || n_ >= kCap) return false;
        if ((e.air_node_id==graph::kNone)!=(e.air_node_index==graph::kNone) ||
            (e.air_node_index!=graph::kNone && e.air_node_index>=graph::kMaxIndex)) return false;
        for (size_t i = 0; i < n_; ++i) if (e_[i].form_def_id == e.form_def_id && e_[i].finisher == e.finisher) return false;
        e_[n_++] = e;
        return true;
    }
    void clear() { n_ = 0; }
    size_t size() const { return n_; }
    const FinisherEntry* at(size_t i) const { return i < n_ ? &e_[i] : nullptr; }
    const FinisherEntry* find(uint32_t form_def, Finisher f) const {
        for (size_t i = 0; i < n_; ++i) if (e_[i].form_def_id == form_def && e_[i].finisher == f) return &e_[i];
        return nullptr;
    }
private:
    FinisherEntry e_[kCap] = {};
    size_t n_ = 0;
};

// Native sub-steps of FUN_14235dee0, each wrapping the REAL native function with the chooser's real arguments
// (installed by the integrator only after the signature pins exist; nullptr => legality unavailable => fail-closed).
struct NativeOps {
    bool (*unlock_gate)(uint32_t node_id, void* unlock_data, void* graph) = nullptr;                    // FUN_142339010
    bool (*state_blocked)(void* node, uint64_t mask, void* state_set, void* conditions) = nullptr;       // FUN_14235d740 != 0
    bool (*condition)(uint32_t id, void* node, void* conditions) = nullptr;                             // FUN_14235c420(id,0,1,node,cond)
    bool (*input_ok)(void* node, void* combo_data, void* combo_cfg) = nullptr;                          // FUN_14235d2b0(...)[1]
    bool (*post_ok)(void* node, void* conditions, void* combo_cfg) = nullptr;                           // FUN_14235d5f0(1,node,cond,cfg)
    bool (*anim_index)(void* anim_out, uint32_t hash, uint16_t* index) = nullptr;                        // FUN_142dfecb0 (hash -> output index, 0xffff = none)
    bool complete() const { return unlock_gate && state_blocked && condition && input_ok && post_ok && anim_index; }
};
// The requirement id that encodes the combo-count/step requirement. UNMAPPED: unset => no bypass is ever applied.
// cb31: there is NO global combo-count requirement id (condition ids are per-node slot indices; the chain prefix is graph position).
// `set == false` therefore means EXACT native legality (no requirement skipped); a skip is only for explicit experiments.
struct SkipRequirement { bool set = false; uint32_t id = 0; };


// Chooser phase-2 window (FUN_14235e1e0, cb1 section 1/3): when the caller reports phase 2 (the attack animation is no
// longer in progress), a node is eligible ONLY if at least one animation-parameter hash of its attack entry
// (+0xb8 list, +0xc0 count) resolves through the animation-output map and the parameter value is > 0.0. Outside phase 2 the
// chooser calls legality directly (no window test). Unreadable/unbound => closed (fail closed).
inline bool window_open(const ChooserArgs& a, uint32_t node_idx, CopyFn cp, const NativeOps& ops) {
    if (!a.phase_is_2) return true;
    if (!ops.anim_index || !a.anim_out) return false;
    uint8_t c[rows::kMinCombo], cfg[0x30], node[graph::kNode], e[graph::kEntry];
    if (!copy_checked(cp, c, reinterpret_cast<uintptr_t>(a.combo_data), sizeof c) || !copy_checked(cp, cfg, reinterpret_cast<uintptr_t>(a.combo_cfg), sizeof cfg)) return false;
    const uint64_t nodes = graph::u64(c, 0), table = graph::u64(cfg, 0x28);
    if (!graph::node_at(cp, nodes, node_idx, node)) return false;
    const uint32_t eidx = graph::u32(node, 0x2c);
    if (!table || eidx >= graph::kMaxIndex || !copy_checked(cp, e, at(table, eidx, graph::kEntry), sizeof e)) return false;
    const uint32_t count = graph::u32(e, 0xc0);
    uint32_t hashes[graph::kMaxList];
    if (count == 0 || count > graph::kMaxList || !copy_checked(cp, hashes, graph::u64(e, 0xb8), count * 4)) return false;
    uint16_t n = 0;
    const uint64_t out = reinterpret_cast<uintptr_t>(a.anim_out);
    if (!copy_checked(cp, &n, out, 2)) return false;
    uint64_t values = 0;                                                  // float array pointer at out + n*16 + 8 (u16 index arithmetic of the decompile)
    if (!copy_checked(cp, &values, at(out, n, 16, 8), 8)) return false;
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t index = 0xffff;
        if (!ops.anim_index(a.anim_out, hashes[i], &index) || index == 0xffff) continue;
        float v = 0.0f;
        if (copy_checked(cp, &v, at(values, index, 4), 4) && v > 0.0f) return true;
    }
    return false;
}

// Native node legality (cb15). Callers supply the proven connection arg, or
// arg0 for a first-entry shortcut after an actual native attack acceptance.
// No unlock or listed requirement is skipped by the executable shortcut.
// Arg2 alone admits the +b1 entry gate; direct finisher entry uses that route.
inline bool legal_node(const ChooserArgs& a, uint32_t node_idx, CopyFn cp, const NativeOps& ops, const SkipRequirement& skip, uint32_t conn_arg) {
    // skip id 7 is UNSUPPORTED: id 7 doubles as the implicit trailing NEGATIVE guard of the native function (cb15 section 2).
    if ((skip.set && skip.id == 7) || !ops.complete()) return false;
    uint8_t c[rows::kMinCombo], cfg[0x30], node[graph::kNode];
    if (!copy_checked(cp, c, reinterpret_cast<uintptr_t>(a.combo_data), sizeof c) || !copy_checked(cp, cfg, reinterpret_cast<uintptr_t>(a.combo_cfg), sizeof cfg)) return false;
    const uint64_t nodes = graph::u64(c, 0), defs = graph::u64(cfg, 0x08), table = graph::u64(cfg, 0x28);
    if (!graph::node_at(cp, nodes, node_idx, node)) return false;
    const uint64_t node_addr = at(nodes, node_idx, graph::kNode);
    if (!node_addr) return false;
    void* const native_node = reinterpret_cast<void*>(static_cast<uintptr_t>(node_addr));
    // (1) form / Loadout category match
    const uint32_t form = graph::u32(node, 0x24);
    if (form != graph::kNone) {
        uint8_t f[graph::kNode], d[graph::kDef];
        if (!graph::node_at(cp, nodes, form, f)) return false;
        const uint32_t di = graph::u32(f, 0x2c);
        if (!defs || di >= graph::kMaxIndex || !copy_checked(cp, d, at(defs, di, graph::kDef), sizeof d)) return false;
        const uint32_t id = graph::u32(d, 0), cat = graph::u32(d, 0x70);
        uint32_t equipped = 0;
        if (id == 0 || cat >= graph::kMaxCategory || !copy_checked(cp, &equipped, at(reinterpret_cast<uintptr_t>(a.loadout), cat, 4), 4) || equipped != id) return false;
    }
    if (graph::u32(node, 0x20) != 1) return true;                       // non-attack node types are legal
    // (2) attack node: unlock gate, state mask, requirement list, input check, post list
    if (!ops.unlock_gate(graph::u32(node, 0x10), a.unlock_data, a.graph)) return false;
    if (ops.state_blocked(native_node, ~graph::u64(node, 0x18) & 0x401be3fd23f8ull, a.state_set, a.conditions)) return false;
    const uint32_t eidx = graph::u32(node, 0x2c);
    uint8_t e[graph::kEntry];
    if (!table || eidx >= graph::kMaxIndex || !copy_checked(cp, e, at(table, eidx, graph::kEntry), sizeof e)) return false;
    if (e[0xb1] != 0 && conn_arg != 2) return false;                   // gated entry only passes in phase 2 (native: byte != 0 && arg != 2 => illegal)
    uint32_t req[graph::kMaxList], post_count = graph::u32(e, 0xa8);
    const uint32_t rc = graph::u32(e, 0x98);
    if (rc > graph::kMaxList || post_count > graph::kMaxList) return false;
    if (rc && !copy_checked(cp, req, graph::u64(e, 0x90), rc * 4)) return false;
    uint32_t last = 0;
    for (uint32_t i = 0; i < rc; ++i) {
        last = req[i];
        if (!(skip.set && last == skip.id) && !ops.condition(last, native_node, a.conditions)) return false;     // listed ids must hold
    }
    if (rc == 0 || last != 7) {                                         // native: the trailing id-7 test must be FALSE
        if (ops.condition(7, native_node, a.conditions)) return false;
    }
    const bool input = ops.input_ok(native_node, a.combo_data, a.combo_cfg);
    if (post_count == 0) return input;
    const bool post = ops.post_ok(native_node, a.conditions, a.combo_cfg);
    if (node[0x31] == 0) return post;                                    // flag clear: the post result alone decides
    return post && input;
}

// Ordinary native graph legality retains the parent connection contract.
inline bool legal_without_requirement(const ChooserArgs& a, uint32_t node_idx, CopyFn cp, const NativeOps& ops, const SkipRequirement& skip) {
    graph::Conn conns[graph::kMaxConn]; size_t n = 0;
    if (!graph::connections(cp, a, conns, &n)) return false;
    for (size_t i = 0; i < n; ++i)
        if (conns[i].target == node_idx) return legal_node(a, node_idx, cp, ops, skip, conns[i].arg);
    return false;
}

// Direct finisher entry skips the combo prefix and its +b1 arrival gate.
// Arg2 is the native supported entry route; all own-node unlock, state,
// input, requirement and animation-window checks still run separately.
inline bool shortcut_entry_arg(const ChooserArgs& a,uint32_t target,CopyFn cp,uint32_t& arg) {
    uint8_t c[rows::kMinCombo]{},cfg[0x30]{},node[graph::kNode]{},entry[graph::kEntry]{};
    if (!copy_checked(cp,c,reinterpret_cast<uintptr_t>(a.combo_data),sizeof c) ||
        !copy_checked(cp,cfg,reinterpret_cast<uintptr_t>(a.combo_cfg),sizeof cfg)) return false;
    const auto nodes=graph::u64(c,0),table=graph::u64(cfg,0x28);
    if (!graph::node_at(cp,nodes,target,node) || !copy_checked(cp,entry,at(table,graph::u32(node,0x2c),graph::kEntry),sizeof entry)) return false;
    arg=entry[0xb1] ? 2u : 0u;
    return true;
}

// ---- process-wide binding (set once by the integrator before any detour runs) ----
struct Binding {
    CopyFn copy = nullptr; ClockFn clock = nullptr;
    ScopeFn observer_scope = nullptr;          // evidence-backed stamps for the observed body; nullptr => telemetry only
    ChooserScopeFn chooser_scope = nullptr;    // the current chooser's stamps; nullptr => no sample
    GetErrFn get_last_error = nullptr; SetErrFn set_last_error = nullptr;   // required for POST-original capture
    NodeTable table;                          // evidence-filled finisher nodes; empty => no finisher
    NativeOps ops;                            // real native legality sub-steps; unset => no legality
    SkipRequirement skip;                     // unset => EXACT native legality (no bypass)
    Mailbox mailbox;
};
inline Binding& binding() { static Binding b; return b; }

namespace detail {
// One observed body: PRE captures are telemetry only (stamped Pre); POST captures revalidate scope + row identity
// before and after the copy and preserve the original's LastError. Never throws, never mutates the game.
template <class Rows, class Copy, class Capture, class RowsFn, class Post, class Invalidate>
inline void observe(const void* column_struct, const void* ctx, Phase phase, Capture capture, RowsFn rows_of, Post post,
                    Invalidate invalidate) {
    Binding& b = binding();
    Scope scope{};
    if (phase == Phase::Post) {
        if (!b.observer_scope) { invalidate(); b.mailbox.note_no_scope_source(); return; }
        if (!b.get_last_error || !b.set_last_error) { invalidate(); b.mailbox.note_no_error_guard(); return; }
        const uint32_t err = b.get_last_error();
        Rows r1{}, r2{};
        Scope s1{}, s2{};
        Copy c{};
        bool ok = b.observer_scope(column_struct, ctx, &s1) && s1.complete() && rows_of(column_struct, b.copy, &r1) &&
                  capture(column_struct, b.copy, &c) && b.observer_scope(column_struct, ctx, &s2) && s1 == s2 &&
                  rows_of(column_struct, b.copy, &r2) && std::memcmp(&r1, &r2, sizeof r1) == 0;   // everything revalidated LAST
        b.set_last_error(err);
        if (!ok) { invalidate(); b.mailbox.note_scope_rejected(); return; }
        post(c, s1, Phase::Post);
        return;
    }
    Copy c{};
    if (!capture(column_struct, b.copy, &c)) { invalidate(); b.mailbox.note_capture_failed(); return; }
    if (b.observer_scope) (void)b.observer_scope(column_struct, ctx, &scope);   // informational only; Pre is never sampled
    post(c, scope, Phase::Pre);
}
}  // namespace detail

// Observer entry points for the passive detours (Pre: before forwarding; Post: after the original returns, with all
// arguments the original received). They never throw/mutate; without scope sources nothing becomes sampleable.
inline void observe_lockon(const void* column_struct, const void* ctx, Phase phase) {
    Binding& b = binding();
    detail::observe<LockOnRows, LockOnCopy>(column_struct, ctx, phase, capture_lockon, lockon_rows,
        [&b](const LockOnCopy& c, const Scope& s, Phase p) { b.mailbox.post_lockon(c, s, p); },
        [&b] { b.mailbox.invalidate_lockon(); });
}
inline void observe_movement(const void* column_struct, const void* ctx, Phase phase) {
    Binding& b = binding();
    detail::observe<MoveRows, MoveCopy>(column_struct, ctx, phase, capture_movement, movement_rows,
        [&b](const MoveCopy& c, const Scope& s, Phase p) { b.mailbox.post_movement(c, s, p); },
        [&b] { b.mailbox.invalidate_movement(); });
}

// Chooser readers over the by-value ChooserArgs snapshot. finisher_node / legal_without_step_requirement are
// intentionally NOT provided (need content node ids + native legality proof): Module stays fail-closed.
inline bool read_direction(const ChooserArgs& a, DirectionSample* s) {
    Binding& b = binding();
    Scope current{};
    if (!b.clock || !b.chooser_scope || !b.chooser_scope(a, &current)) return false;
    return b.mailbox.sample(current, b.clock(), s);
}
inline bool read_attack(const ChooserArgs& a, Attack* out) {
    Binding& b = binding();
    uint8_t c[rows::kMinCombo];
    if (!copy_checked(b.copy, c, reinterpret_cast<uintptr_t>(a.combo_data), sizeof c)) return false;
    *out = rows::attack_from_combo(c);
    return true;
}
// Finisher node lookup (cb18): the evidence entry's node index is VALIDATED against the live shared node array (content
// id and equipped-form definition id must match what the evidence recorded); it is NOT limited to the current node's
// connections, because the native graph body feeds the chooser candidate nodes from several sources (cb18) and the
// chooser's return value is only a node index. Fails closed on an empty table, no entry, unreadable graph or any mismatch.
// Keep native continuation order, excluding a restart at the first entry.
inline bool finisher_followups(const ChooserArgs& a,uint32_t form,uint32_t first,CopyFn cp,graph::Conn* out,size_t& count,uint32_t alternate_first=graph::kNone) {
    count=0;uint8_t combo[rows::kMinCombo]{},cfg[0x30]{},node[graph::kNode]{};graph::Conn edges[graph::kMaxConn]{};size_t n=0;
    if(!out || !copy_checked(cp,combo,reinterpret_cast<uintptr_t>(a.combo_data),sizeof combo) ||
        !copy_checked(cp,cfg,reinterpret_cast<uintptr_t>(a.combo_cfg),sizeof cfg) || !graph::connections(cp,a,edges,&n)) return false;
    for(size_t i=0;i<n;++i) {
        if(edges[i].target==first || edges[i].target==alternate_first) continue;
        uint32_t id=0,category=0;
        if(!graph::node_at(cp,graph::u64(combo,0),edges[i].target,node)) return false;
        if(graph::u32(node,0x24)==graph::kNone) continue;
        if(!graph::form_def(cp,graph::u64(combo,0),graph::u64(cfg,8),node,&id,&category)) return false;
        if(id==form && category==2) out[count++]=edges[i];
    }
    return true;
}

inline bool find_finisher_node(const ChooserArgs& a, Finisher f, uint32_t* idx, uint32_t* form_id, bool equipped_only, bool airborne=false) {
    Binding& b = binding();
    if (b.table.size() == 0 || f == Finisher::None) return false;
    uint8_t c[rows::kMinCombo], cfg[0x30], node[graph::kNode];
    if (!copy_checked(b.copy, c, reinterpret_cast<uintptr_t>(a.combo_data), sizeof c) ||
        !copy_checked(b.copy, cfg, reinterpret_cast<uintptr_t>(a.combo_cfg), sizeof cfg)) return false;
    const uint64_t nodes = graph::u64(c, 0), defs = graph::u64(cfg, 0x08);
    // Validate content identity independently of selection when looking up
    // an acquired form for a queued native selection request.
    const FinisherEntry* hit = nullptr;
    for (size_t i = 0; i < b.table.size(); ++i) {
        const FinisherEntry* e = b.table.at(i);
        if (e->finisher != f) continue;
        const auto node_index=airborne?e->air_node_index:e->node_index;
        const auto node_id=airborne?e->air_node_id:e->node_id;
        if(node_index==graph::kNone || node_id==graph::kNone)return false;
        uint32_t form_def = 0, category = 0;
        // An UNREADABLE candidate is unknown, not a non-match: it could be a second validating entry, so refuse outright.
        if (!graph::node_at(b.copy, nodes, node_index, node)) return false;
        if (graph::u32(node, 0x10) != node_id) continue;                        // readable and different content: a real non-match
        if(airborne){
            uint8_t entry[graph::kEntry]{};
            if(!(graph::u64(node,0x18)&8) || graph::u32(node,0x2c)>=graph::kMaxIndex || !copy_checked(b.copy,entry,
                at(graph::u64(cfg,0x28),graph::u32(node,0x2c),graph::kEntry),sizeof entry) ||
                graph::u32(entry,0x28)!=1)return false; // Authored Air AttackInfo, not a ground-gate bypass.
        }
        if (!graph::form_def(b.copy, nodes, defs, node, &form_def, &category)) {
            if (graph::u32(node, 0x24) == graph::kNone) continue;                  // readable node that simply has no form: a real non-match
            return false;                                                          // form node / definition unreadable: unknown
        }
        if (form_def != e->form_def_id) continue;
        // Only the EQUIPPED weapon counts (live Loadout[category]); other weapons' entries are real non-matches, decided
        // BEFORE ambiguity accounting. category >= 4 would read the next entity's Loadout row: never equipped.
        if (category >= graph::kMaxCategory) continue;
        uint32_t equipped = 0;
        if (!copy_checked(b.copy, &equipped, at(reinterpret_cast<uintptr_t>(a.loadout), category, 4), 4)) return false;   // unknown
        if (equipped_only && equipped != form_def) continue;
        if (hit) return false;   // multiple matching entries are ambiguous
        hit = e;
    }
    if (!hit) return false;
    *idx = airborne?hit->air_node_index:hit->node_index;
    if (form_id) *form_id = hit->form_def_id;
    return true;
}
inline bool read_finisher_node(const ChooserArgs& a, Finisher f, uint32_t* idx) {
    return find_finisher_node(a, f, idx, nullptr, true);
}
inline bool read_legal(const ChooserArgs& a, uint32_t node_idx) {
    Binding& b = binding();
    return window_open(a, node_idx, b.copy, b.ops) && legal_without_requirement(a, node_idx, b.copy, b.ops, b.skip);
}
// All four readers. Module still chooses nothing until the table, the native ops and the skip id are installed.
inline Readers complete_readers() {
    Readers r;
    r.direction_sample = read_direction;
    r.attack_press = read_attack;
    r.finisher_node = read_finisher_node;
    r.legal_without_step_requirement = read_legal;
    return r;
}
inline Readers partial_readers() {
    Readers r;
    r.direction_sample = read_direction;
    r.attack_press = read_attack;
    return r;
}

}  // namespace combat_native
