// Host-side proof of src/native/graph_probe.h over FAKE graph memory (no game, no engine reads).
// build+run: scripts/native/test_combat_logic.sh
#include "../../src/native/graph_probe.h"
#include <vector>

using namespace graph_probe;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
static bool g_ok = true;
static bool copy_fn(void* dst, const void* src, size_t n) { if (!g_ok) return false; std::memcpy(dst, src, n); return true; }
static void put32(uint8_t* p, size_t off, uint32_t v) { std::memcpy(p + off, &v, 4); }
static void put64(uint8_t* p, size_t off, uint64_t v) { std::memcpy(p + off, &v, 8); }
static uint64_t addr(const void* p) { return reinterpret_cast<uintptr_t>(p); }

struct Graph {
    std::vector<uint8_t> nodes = std::vector<uint8_t>(16 * kNode), table = std::vector<uint8_t>(8 * kEntry), defs = std::vector<uint8_t>(4 * kDef);
    std::vector<uint32_t> conns, req = {7, 8}, post = {3}, hashes = {0xdeadbeef, 0x1234};
    uint8_t cur_node[kNode] = {}, combo[kCombo] = {}, cfg[0xa0] = {};
    std::vector<uint32_t> cands = {5, 7, 6, 9};
    Graph() {
        // current node: 3 connections -> nodes 5, 6, 7
        conns = {5, 0, 6, 1, 7, 2};
        put64(cur_node, 0, addr(conns.data())); put32(cur_node, 8, 3);
        for (uint32_t i = 5; i <= 7; ++i) {
            uint8_t* n = nodes.data() + i * kNode;
            put32(n, 0x10, 100 + i); put32(n, 0x20, 1); put32(n, 0x24, 4); put32(n, 0x2c, i - 5);   // form node 4, attack entry i-5
        }
        put32(nodes.data() + 4 * kNode, 0x2c, 2);                                                   // form node 4 -> def 2
        uint8_t* e = table.data();                                                                  // entry 0 has lists
        put64(e, 0x90, addr(req.data())); put32(e, 0x98, 2); put64(e, 0xa0, addr(post.data())); put32(e, 0xa8, 1);
        put64(e, 0xb8, addr(hashes.data())); put32(e, 0xc0, 2); e[0xb1] = 1;
        put32(defs.data() + 2 * kDef, 0, 4242); put32(defs.data() + 2 * kDef, 0x70, 6);
        put64(combo, 0, addr(nodes.data())); put32(combo, 0x20, 4); put64(combo, 0x10, addr(cands.data())); put32(combo, 0x18, 4);
        put32(nodes.data() + 4 * kNode, 0x24, 2);   // current node 4: +0x24 (K) = 2 (form-node use in other nodes aside)
        put64(cfg, 8, addr(defs.data())); put64(cfg, 0x28, addr(table.data()));
    }
    combat_mod::ChooserArgs args() {
        combat_mod::ChooserArgs a{};
        a.node = cur_node; a.combo_data = combo; a.combo_cfg = cfg; a.phase_is_2 = 1;
        return a;
    }
};

// address-wrap regressions (review CB29)
struct Range { uint64_t lo, hi; };
static std::vector<Range> g_ranges;
static int g_low_calls = 0;
static bool permissive_copy(void* dst, const void* src, size_t n) {
    const uint64_t a = reinterpret_cast<uintptr_t>(src);
    for (const Range& r : g_ranges) if (a >= r.lo && a + n <= r.hi) { std::memcpy(dst, src, n); return true; }
    if (a < 0x1000000ull || a > combat_native::kUserMax) ++g_low_calls;
    std::memset(dst, 0, n);
    return true;
}
static void map_range(const void* p, size_t n) { g_ranges.push_back({reinterpret_cast<uintptr_t>(p), reinterpret_cast<uintptr_t>(p) + n}); }

int main() {
    using combat_native::Phase;
    static char line[kLineMax];
    {   Graph g; Record r;
        CHECK(capture(copy_fn, g.args(), 6, Phase::Post, 1, &r));
        CHECK(r.n_cand == 4 && r.cand[0] == 5 && r.cand[1] == 7 && r.cand[3] == 9 && (r.valid & 16) && u32(r.cur_node, 0x24) == 2);   // candidate list + current node captured
        CHECK(r.valid == 31 && r.cur == 4 && r.n_conn == 3 && r.ret == 6 && r.phase == 1 && r.phase_is_2 == 1);
        CHECK(r.slot[0].target == 5 && r.slot[1].target == 6 && r.slot[2].target == 7 && r.slot[1].arg == 1);
        CHECK(u32(r.slot[0].node, 0x10) == 105 && r.slot[0].entry_idx == 0 && r.slot[0].form == 4 && r.slot[0].def_idx == 2);
        CHECK(r.slot[0].valid == 127);                                                   // every block copied for slot 0
        CHECK(r.slot[0].n_req == 2 && r.slot[0].req[0] == 7 && r.slot[0].req[1] == 8 && r.slot[0].n_post == 1 && r.slot[0].post[0] == 3);
        CHECK(r.slot[0].n_hash == 2 && r.slot[0].hash[0] == 0xdeadbeef);
        CHECK(u32(r.slot[0].def, 0) == 4242 && u32(r.slot[0].def, 0x70) == 6);
        CHECK(r.slot[1].entry_idx == 1 && (r.slot[1].valid & 2) && !(r.slot[1].valid & 4 && r.slot[1].n_req));   // entry without lists
        const size_t n = format_line(r, line, sizeof line);
        CHECK(n > 0 && line[0] == 'G' && line[n - 1] == '\n');
        FILE* f = std::fopen(".work/native-build/graph_probe_sample.log", "w"); CHECK(f != nullptr);
        if (f) { std::fwrite(line, 1, n, f); std::fclose(f); }
        Dedupe d; CHECK(d.fresh(r) && !d.fresh(r));
        Record r2 = r; r2.ret = 7; CHECK(d.fresh(r2)); }
    {   Graph g; Record r;                                                                   // wrapped node/table/def bases never reach the copier
        g_ranges.clear(); g_low_calls = 0;
        map_range(g.combo, sizeof g.combo); map_range(g.cfg, sizeof g.cfg); map_range(g.cur_node, sizeof g.cur_node); map_range(g.conns.data(), g.conns.size() * 4);
        map_range(g.cands.data(), g.cands.size() * 4);
        put64(g.combo, 0, ~0ull - 0x37);                                                     // nodes base = 2^64-0x38: target*0x38-0x38 lands on a plausible low address
        g.conns = {0x5b0, 0, 0x5b1, 0, 0x5b2, 0}; map_range(g.conns.data(), g.conns.size() * 4);
        put64(g.cur_node, 0, addr(g.conns.data())); put32(g.cur_node, 8, 3);
        CHECK(capture(permissive_copy, g.args(), 0, combat_native::Phase::Pre, 1, &r));
        CHECK(!(r.slot[0].valid & 1) && !(r.slot[1].valid & 1) && !(r.slot[2].valid & 1) && g_low_calls == 0);
        Graph h; g_low_calls = 0; g_ranges.clear();
        map_range(h.combo, sizeof h.combo); map_range(h.cfg, sizeof h.cfg); map_range(h.cur_node, sizeof h.cur_node); map_range(h.conns.data(), h.conns.size() * 4);
        map_range(h.cands.data(), h.cands.size() * 4); map_range(h.nodes.data(), h.nodes.size()); map_range(h.req.data(), 8); map_range(h.post.data(), 4); map_range(h.hashes.data(), 8);
        put64(h.cfg, 0x28, ~0ull - 0x117); h.conns = {5, 0, 6, 0, 7, 0}; map_range(h.conns.data(), h.conns.size() * 4);   // attack table base 2^64-0x118 with entry idx 0x...: entry_idx*0x118 wraps
        put64(h.cur_node, 0, addr(h.conns.data())); put32(h.cur_node, 8, 3);
        put32(h.nodes.data() + 5 * kNode, 0x2c, 0x1ff);                                       // entry index 0x1ff: 2^64-0x118 + 0x1ff*0x118 = 0x1de.. wraps to a plausible 0x?; must be refused
        CHECK(capture(permissive_copy, h.args(), 0, combat_native::Phase::Pre, 1, &r) && !(r.slot[0].valid & 2) && g_low_calls == 0); }
    {   Graph g; Record r;                                                                   // failures
        g_ok = false; CHECK(!capture(copy_fn, g.args(), 0, Phase::Pre, 1, &r)); g_ok = true;
        combat_mod::ChooserArgs none{}; CHECK(!capture(copy_fn, none, 0, Phase::Pre, 1, &r));
        put32(g.cur_node, 8, 100); CHECK(capture(copy_fn, g.args(), 0, Phase::Pre, 1, &r) && r.n_conn == 100 && r.slot[7].target != kNone && r.slot[7].target < kMaxIndex);   // clamped to 8 slots
        put32(g.cur_node, 8, 3); g.conns[2] = 99999; CHECK(capture(copy_fn, g.args(), 0, Phase::Pre, 1, &r) && !(r.slot[1].valid & 1) && (r.slot[0].valid & 1));  // out-of-range target
        g.conns[2] = 6; put64(g.cfg, 0x28, 8); CHECK(capture(copy_fn, g.args(), 0, Phase::Pre, 1, &r) && !(r.slot[0].valid & 2));                                 // unreadable attack table
        put64(g.cfg, 0x28, addr(g.table.data())); put64(g.table.data(), 0x90, 8); CHECK(capture(copy_fn, g.args(), 0, Phase::Pre, 1, &r) && !(r.slot[0].valid & 4)); // unreadable list
        put64(g.cur_node, 0, 8); CHECK(capture(copy_fn, g.args(), 0, Phase::Pre, 1, &r) && !(r.valid & 4) && r.slot[0].target == kNone);                          // unreadable connections
        char tiny[64]; CHECK(format_line(r, tiny, sizeof tiny) == 0); }
    std::printf(failures ? "graph probe: %d FAILURE(S)\n" : "graph probe: all checks passed\n", failures);
    return failures ? 1 : 0;
}
