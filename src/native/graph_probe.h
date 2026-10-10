// PASSIVE combo-graph evidence probe (cb15): copies, around the chooser FUN_14235e1e0, the neighbourhood the chooser
// looks at (current node's connections, each target node, its attack-table entry + condition/post/hash lists, its form
// node and form definition) into bounded records. Only COPIES bytes through the injected CopyFn; never mutates the game,
// never chooses a node, never feeds combat_mod. Offline analysis (scripts/re/graph_probe_analyze.py) builds the graph and
// a node table so finisher node ids per weapon can be identified by human demonstration (the content archives are
// compressed; names live there, not in the exe).
// Layout (cb1/cb12, static candidates): node 0x38 B {+0 connections ptr, +8 count u32, +0x10 id, +0x18 stateAllow u64,
// +0x20 type, +0x24 form-node index, +0x2c attack-table index}; connections {u32 target, u32 arg}; combo cfg {+8 defs ptr
// (0x140 B each: +0 id, +0x70 category), +0x28 attack table ptr (0x118 B each: +0x90/+0x98 requirement ids ptr/count,
// +0xa0/+0xa8 post ids ptr/count, +0xb8/+0xc0 animation-parameter hash ptr/count)}; ComboData row C {+0 node array ptr,
// +0x20 current node}.
#pragma once
#include "scope_probe.h"

namespace graph_probe {
using combat_native::CopyFn;
using combat_native::copy_checked;
using combat_native::qword;

constexpr size_t kCandMax = 64, kMaxConn = 8, kList = 16, kNode = 0x38, kEntry = 0x118, kDef = 0x140, kCombo = 0x48;
constexpr uint32_t kMaxIndex = 4096, kNone = 0xffffffffu;

struct Slot {
    uint32_t target = kNone, arg = 0, form = kNone, entry_idx = kNone, def_idx = kNone;
    uint8_t valid = 0;   // bit0 target node, bit1 entry, bit2 req list, bit3 post list, bit4 hash list, bit5 form node, bit6 def
    uint8_t node[kNode] = {}, entry[kEntry] = {}, form_node[kNode] = {}, def[kDef] = {};
    uint32_t req[kList] = {}, post[kList] = {}, hash[kList] = {};
    uint32_t n_req = 0, n_post = 0, n_hash = 0;
};
struct Record {
    uint32_t seq = 0, ret = kNone, cur = kNone, n_conn = 0;
    uint8_t phase = 0, phase_is_2 = 0, valid = 0;   // valid bit0 ComboData, bit1 node header, bit2 connections, bit3 cfg
    uint64_t node_ptr = 0, combo_ptr = 0, cfg_ptr = 0, nodes_base = 0;
    uint32_t n_cand = 0;                            // true count C+0x18; cand = first <=kCandMax entries of the u32 list at C+0x10
    uint32_t cand[kCandMax] = {};
    uint8_t cur_node[kNode] = {};                   // the current node (C+0x20) raw, when valid (valid bit4)
    uint8_t combo[kCombo] = {}, node_head[0x10] = {}, cfg_head[0x30] = {};
    uint32_t conn[kMaxConn * 2] = {};
    Slot slot[kMaxConn];
};

inline uint32_t u32(const uint8_t* p, size_t off) { uint32_t v; std::memcpy(&v, p + off, 4); return v; }

// Copies up to kList u32 from a native list (ptr, count); returns the number copied (count clamped).
inline bool copy_list(CopyFn cp, uint64_t ptr, uint32_t count, uint32_t* dst, uint32_t* n) {
    *n = 0;
    if (count == 0) return true;
    const uint32_t take = count < kList ? count : static_cast<uint32_t>(kList);
    if (!copy_checked(cp, dst, ptr, take * 4)) return false;
    *n = take;
    return true;
}

inline bool node_at_base(CopyFn cp, uint64_t base, uint32_t idx, uint8_t* out) {
    return base && idx < kMaxIndex && copy_checked(cp, out, combat_native::at(base, idx, kNode), kNode);
}

inline bool capture(CopyFn cp, const combat_mod::ChooserArgs& a, uint32_t ret, combat_native::Phase phase, uint32_t seq, Record* out) {
    Record r;
    r.seq = seq; r.ret = ret; r.phase = static_cast<uint8_t>(phase); r.phase_is_2 = a.phase_is_2;
    r.node_ptr = reinterpret_cast<uintptr_t>(a.node); r.combo_ptr = reinterpret_cast<uintptr_t>(a.combo_data);
    r.cfg_ptr = reinterpret_cast<uintptr_t>(a.combo_cfg);
    if (!copy_checked(cp, r.combo, r.combo_ptr, kCombo)) return false;
    r.valid |= 1;
    r.nodes_base = qword(r.combo, 0); r.cur = u32(r.combo, 0x20);
    // native candidate list (graph body, cb18): u32 node indices at C+0x10, count at C+0x18
    r.n_cand = u32(r.combo, 0x18);
    { const uint32_t take = r.n_cand < kCandMax ? r.n_cand : static_cast<uint32_t>(kCandMax);
      if (take && !copy_checked(cp, r.cand, qword(r.combo, 0x10), take * 4)) r.n_cand = 0; }
    if (r.cur != kNone && node_at_base(cp, r.nodes_base, r.cur, r.cur_node)) r.valid |= 16;
    if (copy_checked(cp, r.cfg_head, r.cfg_ptr, sizeof r.cfg_head)) r.valid |= 8;
    const uint64_t defs = (r.valid & 8) ? qword(r.cfg_head, 0x08) : 0, table = (r.valid & 8) ? qword(r.cfg_head, 0x28) : 0;
    if (!copy_checked(cp, r.node_head, r.node_ptr, sizeof r.node_head)) { *out = r; return true; }
    r.valid |= 2;
    const uint64_t conns = qword(r.node_head, 0);
    uint32_t n = u32(r.node_head, 8);
    r.n_conn = n;
    if (n > kMaxConn) n = static_cast<uint32_t>(kMaxConn);
    if (n && copy_checked(cp, r.conn, conns, n * 8)) r.valid |= 4; else n = 0;
    for (uint32_t i = 0; i < n; ++i) {
        Slot& s = r.slot[i];
        s.target = r.conn[2 * i]; s.arg = r.conn[2 * i + 1];
        if (s.target >= kMaxIndex || !r.nodes_base) continue;
        if (!copy_checked(cp, s.node, combat_native::at(r.nodes_base, s.target, kNode), kNode)) continue;
        s.valid |= 1;
        s.entry_idx = u32(s.node, 0x2c); s.form = u32(s.node, 0x24);
        if (table && s.entry_idx < kMaxIndex && copy_checked(cp, s.entry, combat_native::at(table, s.entry_idx, kEntry), kEntry)) {
            s.valid |= 2;
            if (copy_list(cp, qword(s.entry, 0x90), u32(s.entry, 0x98), s.req, &s.n_req)) s.valid |= 4;
            if (copy_list(cp, qword(s.entry, 0xa0), u32(s.entry, 0xa8), s.post, &s.n_post)) s.valid |= 8;
            if (copy_list(cp, qword(s.entry, 0xb8), u32(s.entry, 0xc0), s.hash, &s.n_hash)) s.valid |= 16;
            s.n_req = u32(s.entry, 0x98); s.n_post = u32(s.entry, 0xa8); s.n_hash = u32(s.entry, 0xc0);   // true counts (lists clamped)
        }
        if (s.form < kMaxIndex && copy_checked(cp, s.form_node, combat_native::at(r.nodes_base, s.form, kNode), kNode)) {
            s.valid |= 32;
            s.def_idx = u32(s.form_node, 0x2c);
            if (defs && s.def_idx < kMaxIndex && copy_checked(cp, s.def, combat_native::at(defs, s.def_idx, kDef), kDef)) s.valid |= 64;
        }
    }
    *out = r;
    return true;
}

// Dedupe: a record is worth logging only when (nodes_base, cur, node_ptr, ret, phase) has not been logged recently.
class Dedupe {
public:
    bool fresh(const Record& r) {
        if (seen(r)) return false;
        mark(r);
        return true;
    }
    // Two-step form: `seen` does not insert; `mark` is called only after the sink ACCEPTED the line, so a dropped line is retried.
    bool seen(const Record& r) const {
        const uint64_t key = key_of(r);
        for (size_t i = 0; i < kN; ++i) if (seen_[i] == key) return true;
        return false;
    }
    void mark(const Record& r) { seen_[next_++ % kN] = key_of(r); }
private:
    static uint64_t key_of(const Record& r) {
        return r.nodes_base * 1000003ull ^ (static_cast<uint64_t>(r.cur) << 20) ^ (r.node_ptr << 3) ^ (static_cast<uint64_t>(r.ret) << 40) ^ r.phase;
    }
    static constexpr size_t kN = 64;
    uint64_t seen_[kN] = {};
    size_t next_ = 0;
};

inline size_t hex(const uint8_t* p, size_t n, char* dst) { return scope_probe::hex(p, n, dst); }

// One text line per record; returns length or 0 if the buffer is too small (need >= kLineMax).
constexpr size_t kLineMax = 24 * 1024;
inline size_t format_line(const Record& r, char* dst, size_t cap) {
    if (cap < kLineMax) return 0;
    int n = std::snprintf(dst, cap, "G %u %u %u %u %u %u %u %llx %llx %llx %llx C:", r.seq, r.phase, r.ret, r.phase_is_2, r.cur, r.n_conn,
                          r.valid, static_cast<unsigned long long>(r.node_ptr), static_cast<unsigned long long>(r.combo_ptr),
                          static_cast<unsigned long long>(r.cfg_ptr), static_cast<unsigned long long>(r.nodes_base));
    if (n < 0) return 0;
    size_t o = static_cast<size_t>(n);
    o += hex(r.combo, kCombo, dst + o);
    dst[o++] = ' '; dst[o++] = 'L'; dst[o++] = ':';
    o += hex(reinterpret_cast<const uint8_t*>(r.cand), (r.n_cand < kCandMax ? r.n_cand : kCandMax) * 4, dst + o);
    dst[o++] = ' '; dst[o++] = 'K'; dst[o++] = ':'; o += hex(r.cur_node, kNode, dst + o);
    const size_t slots = r.n_conn < kMaxConn ? r.n_conn : kMaxConn;
    for (size_t i = 0; i < slots; ++i) {
        const Slot& s = r.slot[i];
        n = std::snprintf(dst + o, cap - o, " S%zu %u %u %u %u %u %u %u %u %u T:", i, s.target, s.arg, s.valid, s.form, s.entry_idx, s.def_idx,
                          s.n_req, s.n_post, s.n_hash);
        if (n < 0) return 0;
        o += static_cast<size_t>(n);
        o += hex(s.node, kNode, dst + o);
        dst[o++] = ' '; dst[o++] = 'A'; dst[o++] = ':'; o += hex(s.entry, kEntry, dst + o);
        dst[o++] = ' '; dst[o++] = 'F'; dst[o++] = ':'; o += hex(s.form_node, kNode, dst + o);
        dst[o++] = ' '; dst[o++] = 'D'; dst[o++] = ':'; o += hex(s.def, kDef, dst + o);
        dst[o++] = ' '; dst[o++] = 'R'; dst[o++] = ':'; o += hex(reinterpret_cast<const uint8_t*>(s.req), s.n_req < kList ? s.n_req * 4 : kList * 4, dst + o);
        dst[o++] = ' '; dst[o++] = 'P'; dst[o++] = ':'; o += hex(reinterpret_cast<const uint8_t*>(s.post), s.n_post < kList ? s.n_post * 4 : kList * 4, dst + o);
        dst[o++] = ' '; dst[o++] = 'H'; dst[o++] = ':'; o += hex(reinterpret_cast<const uint8_t*>(s.hash), s.n_hash < kList ? s.n_hash * 4 : kList * 4, dst + o);
    }
    dst[o++] = '\n'; dst[o] = 0;
    return o;
}

}  // namespace graph_probe
