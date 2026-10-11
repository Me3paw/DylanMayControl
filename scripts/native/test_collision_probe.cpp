// Host-side proof of src/native/collision_probe.h over FAKE memory (no game, no engine reads).
// build+run: scripts/native/test_combat_logic.sh
#include "../../src/native/collision_probe.h"
#include <array>
#include <vector>

using namespace collision_probe;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
static bool g_ok = true;
static uintptr_t g_fail_lo = 0, g_fail_hi = 0;
static bool copy_fn(void* dst, const void* src, size_t n) {
    if (!g_ok) return false;
    const uintptr_t a = reinterpret_cast<uintptr_t>(src);
    if (g_fail_hi > g_fail_lo && a < g_fail_hi && a + n > g_fail_lo) return false;
    std::memcpy(dst, src, n);
    return true;
}
static uint32_t g_now = 100;
static uint32_t tid_fn() { return 7; }
static uint32_t clock_fn() { return g_now; }
static uint64_t addr(const void* p) { return reinterpret_cast<uintptr_t>(p); }

struct Fake {
    static constexpr uint64_t kRow = 2;
    std::vector<uint8_t> ctl = std::vector<uint8_t>(5 * 0x20), cc = std::vector<uint8_t>(5 * 0x20), tr = std::vector<uint8_t>(5 * 0x40);
    std::array<uint8_t, 0x100> container{};
    std::vector<uint8_t> entries = std::vector<uint8_t>(64 * 12);
    std::array<uint64_t, 0x1d> cols{};
    explicit Fake(uint64_t row = kRow) {
        cols[0] = addr(ctl.data()); cols[3] = addr(cc.data()); cols[9] = addr(tr.data()); cols[0x1c] = row;
        const uint64_t c = addr(container.data());
        std::memcpy(ctl.data() + row * 0x20 + 8, &c, 8);
        const uint64_t proxy = 0x1111, owner = 0xABCD0001; const uint32_t flag = 5;
        std::memcpy(container.data() + 0x10, &proxy, 8); std::memcpy(container.data() + 0x18, &owner, 8); std::memcpy(container.data() + 0x20, &flag, 4);
        for (int i = 0; i < 0x40; ++i) tr[row * 0x40 + i] = static_cast<uint8_t>(i + 1);
        set_list({});
        cc[row * 0x20 + 0x19] = 1;
    }
    void set_list(const std::vector<std::pair<uint64_t, uint8_t>>& l) {
        const uint64_t p = addr(entries.data()); const uint32_t n = static_cast<uint32_t>(l.size());
        std::memcpy(cc.data() + cols[0x1c] * 0x20, &p, 8); std::memcpy(cc.data() + cols[0x1c] * 0x20 + 8, &n, 4);
        for (size_t i = 0; i < l.size() && i < 64; ++i) { std::memcpy(entries.data() + i * 12, &l[i].first, 8); entries[i * 12 + 8] = l[i].second; }
    }
};

// address-wrap regressions (review CB29): permissive copier that succeeds for ANY address, flags low/out-of-range ones
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
    // ---- capture ----
    {   Fake f; Record r;
        f.set_list({{0xE1, 2}, {0xE2, 7}, {0xE3, 0}});
        CHECK(capture(copy_fn, f.cols.data(), kPre, 1, 7, 100, &r));
        CHECK(r.valid == 31 && r.row == 2 && r.count == 3 && r.copied == 3 && !r.truncated && !r.corrupt);
        CHECK(r.e[0].handle == 0xE1 && r.e[0].kind == 2 && r.e[1].handle == 0xE2 && r.e[1].kind == 7 && r.e[2].handle == 0xE3);
        CHECK(r.owner == 0xABCD0001 && r.proxy == 0x1111 && r.owner_flag == 5 && r.container == addr(f.container.data()));
        CHECK(r.flags[1] == 1 && r.transform[0] == 1 && r.transform[0x3f] == 0x40);
        CHECK(r.cc_row == addr(f.cc.data()) + 2 * 0x20 && r.phase == kPre && r.seq == 1 && r.tid == 7 && r.now_ms == 100);
        f.set_list({}); CHECK(capture(copy_fn, f.cols.data(), kPost, 2, 7, 116, &r) && r.count == 0 && r.copied == 0 && (r.valid & 4) && r.phase == kPost); }
    // ---- address-wrap regressions: a wrapped sum must never be attributed to a row nor reach the copier ----
    {   Fake f; Record r;
        g_ranges.clear(); g_low_calls = 0; map_range(f.cols.data(), f.cols.size() * 8);
        f.cols[3] = ~0ull - 0x1f; f.cols[0x1c] = 0x801;                                                // the reviewed case: UINT64_MAX-0x1f + 0x801*0x20 wraps to 0x10000 (plausible)
        map_range(f.ctl.data(), f.ctl.size());
        CHECK(capture(permissive_copy, f.cols.data(), kPre, 1, 0, 0, &r));
        CHECK(!(r.valid & 2) && r.corrupt == 1 && r.cc_row == 0 && r.count == 0 && r.copied == 0 && g_low_calls == 0);
        g_low_calls = 0; Fake g; map_range(g.cols.data(), g.cols.size() * 8); map_range(g.cc.data(), g.cc.size()); map_range(g.entries.data(), g.entries.size());
        g.cols[0] = ~0ull - 0x1f; g.cols[0x1c] = 0x800;                                                // controller column: base + row*0x20 + 8 wraps
        CHECK(capture(permissive_copy, g.cols.data(), kPre, 1, 0, 0, &r));
        CHECK(r.container == 0 && !(r.valid & 8) && r.owner == 0 && r.corrupt == 1 && g_low_calls == 0);
        g_low_calls = 0; Fake h; map_range(h.cols.data(), h.cols.size() * 8); map_range(h.cc.data(), h.cc.size()); map_range(h.ctl.data(), h.ctl.size());
        const uint64_t evil_container = ~0ull - 0xf;                                                    // container + 0x10 wraps to 0
        std::memcpy(h.ctl.data() + Fake::kRow * 0x20 + 8, &evil_container, 8);
        CHECK(capture(permissive_copy, h.cols.data(), kPre, 1, 0, 0, &r));
        CHECK(r.container == evil_container && !(r.valid & 8) && r.owner == 0 && r.corrupt == 1 && g_low_calls == 0);   // owner NOT attributed from a wrapped block
        g_low_calls = 0; Fake k; map_range(k.cols.data(), k.cols.size() * 8); map_range(k.cc.data(), k.cc.size());
        k.cols[9] = ~0ull - 0x3f; k.cols[0x1c] = 0x401;                                                 // transform base + row*0x40 wraps to 0x10000
        CHECK(capture(permissive_copy, k.cols.data(), kPre, 1, 0, 0, &r));
        CHECK(!(r.valid & 16) && r.corrupt == 1 && g_low_calls == 0);
        g_low_calls = 0; Fake m; map_range(m.cols.data(), m.cols.size() * 8); map_range(m.cc.data(), m.cc.size()); map_range(m.ctl.data(), m.ctl.size());
        map_range(m.container.data(), m.container.size()); map_range(m.tr.data(), m.tr.size()); map_range(m.entries.data(), m.entries.size());
        m.set_list({{0xE1, 2}});
        CHECK(capture(permissive_copy, m.cols.data(), kPre, 1, 0, 0, &r) && r.valid == 31 && r.owner == 0xABCD0001 && r.corrupt == 0 && g_low_calls == 0);   // control: the same mapped fake still captures normally
        const uint64_t top_base = combat_native::kUserMax - 0x20; m.cols[3] = top_base; m.cols[0x1c] = 0;                      // boundary: highest valid base (row 0) is accepted
        g_ranges.clear(); map_range(m.cols.data(), m.cols.size() * 8);
        CHECK(capture(permissive_copy, m.cols.data(), kPre, 1, 0, 0, &r) && r.corrupt == 0 && r.cc_row == top_base); }
    // ---- truncation / corruption / invalid blocks (explicit, never silent) ----
    {   Fake f; Record r; std::vector<std::pair<uint64_t, uint8_t>> big; for (uint64_t i = 1; i <= 40; ++i) big.push_back({i, static_cast<uint8_t>(i)});
        f.set_list(big); CHECK(capture(copy_fn, f.cols.data(), kPre, 1, 0, 0, &r) && r.count == 40 && r.copied == 16 && r.truncated == 1 && r.e[15].handle == 16);
        const uint32_t huge = 100000; std::memcpy(f.cc.data() + 2 * 0x20 + 8, &huge, 4);
        CHECK(capture(copy_fn, f.cols.data(), kPre, 1, 0, 0, &r) && r.corrupt == 1 && r.copied == 0 && !(r.valid & 4));      // corrupt count never read through
        f.cols[0x1c] = kMaxRow; CHECK(capture(copy_fn, f.cols.data(), kPre, 1, 0, 0, &r) && r.corrupt == 1 && !(r.valid & 2)); f.cols[0x1c] = 2;
        CHECK(!capture(copy_fn, nullptr, kPre, 1, 0, 0, &r));                                                                // no cols
        g_ok = false; CHECK(!capture(copy_fn, f.cols.data(), kPre, 1, 0, 0, &r)); g_ok = true; }
    {   Fake f; Record r; f.set_list({{0xE1, 1}});
        const uintptr_t ent = addr(f.entries.data()); g_fail_lo = ent; g_fail_hi = ent + 12;                                 // entries unreadable
        CHECK(capture(copy_fn, f.cols.data(), kPre, 1, 0, 0, &r) && r.count == 1 && r.copied == 0 && !(r.valid & 4) && (r.valid & 2)); g_fail_lo = g_fail_hi = 0;
        const uintptr_t ct = addr(f.container.data()); g_fail_lo = ct; g_fail_hi = ct + 0x100;                               // container unreadable
        CHECK(capture(copy_fn, f.cols.data(), kPre, 1, 0, 0, &r) && !(r.valid & 8) && r.owner == 0 && r.container != 0 && (r.valid & 4)); g_fail_lo = g_fail_hi = 0;
        f.cols[9] = 0; CHECK(capture(copy_fn, f.cols.data(), kPre, 1, 0, 0, &r) && !(r.valid & 16)); f.cols[9] = addr(f.tr.data());
        std::memset(f.ctl.data(), 0, f.ctl.size()); CHECK(capture(copy_fn, f.cols.data(), kPre, 1, 0, 0, &r) && r.container == 0 && !(r.valid & 8)); }
    // ---- probe filtering, ring, text round trip through the analyzer ----
    {   static Probe p; Fake f; Config cfg; p.set_env(copy_fn, tid_fn, clock_fn); p.configure(cfg);
        for (int upd = 0; upd < 5; ++upd) {
            g_now = 100 + upd * 16;
            std::vector<std::pair<uint64_t, uint8_t>> l;
            if (upd < 4) l.push_back({0xE1, 2});
            if (upd == 2) l.push_back({0xE2, 1});
            f.set_list(l); p.observe(f.cols.data(), kPre);
            f.set_list({}); p.observe(f.cols.data(), kPost);                                                                // the core consumed the list
        }
        // default config logs only rows with entries: 4 pre records, no empty post records, 6 skipped
        CHECK(p.size() == 4 && p.skipped() == 6 && p.failed() == 0);
        Record r; CHECK(p.at(0, &r) && r.phase == kPre && r.count == 1 && p.at(3, &r) && r.count == 1 && !p.at(4, &r));
        Config c2; c2.log_empty = true; p.configure(c2); g_now = 300; f.set_list({}); p.observe(f.cols.data(), kPost); CHECK(p.size() == 5);   // empty rows logged when requested
        Config c3; c3.watch_owner = 0xABCD0001; p.configure(c3); p.observe(f.cols.data(), kPre); CHECK(p.size() == 6);                         // watched owner always logged
        c3.watch_owner = 0x42; p.configure(c3); p.observe(f.cols.data(), kPre); CHECK(p.size() == 6);                                        // other owner: skipped
        FILE* fl = std::fopen(".work/native-build/collision_probe_sample.log", "w"); CHECK(fl != nullptr);
        static char line[kLineMax];
        for (size_t i = 0; i < p.size(); ++i) { Record x; CHECK(p.at(i, &x)); const size_t n = format_line(x, line, sizeof line); CHECK(n > 0 && line[0] == 'K' && line[n - 1] == '\n'); if (fl) std::fwrite(line, 1, n, fl); }
        if (fl) std::fclose(fl);
        char tiny[32]; Record x; p.at(0, &x); CHECK(format_line(x, tiny, sizeof tiny) == 0); }
    {   static Probe q; Fake f; Config c; c.log_empty = true; q.set_env(copy_fn, tid_fn, clock_fn); q.configure(c);   // ring wrap and bounded memory
        for (int i = 0; i < 2000; ++i) q.observe(f.cols.data(), kPre);
        CHECK(q.size() == Probe::kCapacity && q.overwritten() == 2000 - Probe::kCapacity);
        Record r; CHECK(q.at(0, &r) && r.seq == 2000 - Probe::kCapacity + 1); }
    {   static Probe z; Fake f; CHECK(true); z.observe(f.cols.data(), kPre); CHECK(z.size() == 0);                  // no env installed: nothing happens, nothing crashes
        z.set_env(copy_fn, nullptr, nullptr); Config c; c.log_empty = true; z.configure(c); z.observe(nullptr, kPre); CHECK(z.failed() == 1 && z.size() == 0); }
    std::printf(failures ? "collision probe: %d FAILURE(S)\n" : "collision probe: all checks passed\n", failures);
    return failures ? 1 : 0;
}
