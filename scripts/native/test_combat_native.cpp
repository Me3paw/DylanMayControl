// Host-side proof of src/native/combat_native.h (column-struct capture, scope-qualified mailbox, chooser readers) over
// FAKE memory. build+run: scripts/native/test_combat_logic.sh (no game, no engine reads)
#include "../../src/native/scope_probe.h"
#include "../../src/native/facing_movement.h"
#include "../../src/native/finisher_test_mode.h"
#include <array>
#include <vector>

using namespace combat_native;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static bool g_copy_ok = true;
static uintptr_t g_fail_lo = 0, g_fail_hi = 0;   // selective failure: any read overlapping [lo, hi) fails
static bool copy_fn(void* dst, const void* src, size_t n) {
    if (!g_copy_ok) return false;
    const uintptr_t a = reinterpret_cast<uintptr_t>(src);
    if (g_fail_hi > g_fail_lo && a < g_fail_hi && a + n > g_fail_lo) return false;
    std::memcpy(dst, src, n);
    return true;
}
static uint32_t g_now = 1000;
static uint32_t clock_fn() { return g_now; }
static uint32_t g_err = 0;
static uint32_t get_err() { return g_err; }
static void set_err(uint32_t v) { g_err = v; }

static void put3(uint8_t* p, size_t off, float a, float b, float c) { const float v[3] = {a, b, c}; std::memcpy(p + off, v, sizeof v); }

// Fake ECS columns: 3 rows each; the player is row `kRow`.
constexpr uint64_t kRow = 1;
struct Fake {
    std::vector<uint8_t> status = std::vector<uint8_t>(3 * 0xf0), aim = std::vector<uint8_t>(3 * 0x40),
                         sel = std::vector<uint8_t>(3 * 0x10), movement = std::vector<uint8_t>(3 * 0x80);
    std::array<uint64_t, 10> lockcols{};   // 0x50 bytes
    std::array<uint64_t, 5> movecols{};    // 0x28 bytes
    explicit Fake(uint64_t row = kRow) {
        lockcols[1] = reinterpret_cast<uintptr_t>(sel.data());     // +0x08
        lockcols[2] = reinterpret_cast<uintptr_t>(aim.data());     // +0x10, native AimData column
        lockcols[6] = reinterpret_cast<uintptr_t>(status.data());  // +0x30
        lockcols[9] = row;                                         // +0x48
        movecols[0] = reinterpret_cast<uintptr_t>(movement.data());// +0x00
        movecols[2] = reinterpret_cast<uintptr_t>(aim.data());     // +0x10
        movecols[4] = row;                                         // +0x20
        uint8_t* s = status.data() + row * 0xf0; s[0] = 1; s[0xa0] = 1; put3(s, 0x90, 10, 0, 0);
        const uint64_t handle = (7ull << 32) | 5; std::memcpy(sel.data() + row * 0x10, &handle, 8);
        uint8_t* m = movement.data() + row * 0x80; put3(m, 0, -1, 0, 0); const float mag = 1.0f; std::memcpy(m + 0x24, &mag, 4);
        // decoy rows must never be read
        status[0] = 0xEE; aim[0] = 0xEE; sel[0] = 0xEE; movement[0] = 0xEE;
    }
};

// Fake trusted scope sources: the "SysCtx" is {world, frame, actor}; the chooser scope is a global.
struct FakeCtx { uint64_t world, frame, actor; };
static int g_scope_calls = 0, g_mutate_on_call = 0;   // g_mutate_on_call: call number at which the fake world changes the lock row
static std::array<uint64_t, 10>* g_mutate_cols = nullptr;
static bool scope_fn(const void*, const void* ctx, Scope* out) {
    ++g_scope_calls;
    if (g_mutate_cols && g_scope_calls == g_mutate_on_call) (*g_mutate_cols)[9] = 2;   // the row moved while we copied
    if (!ctx) return false;
    const FakeCtx* c = static_cast<const FakeCtx*>(ctx);
    *out = {c->world, c->frame, c->actor};
    return true;
}
static Scope g_chooser{};
static bool chooser_scope_fn(const ChooserArgs&, Scope* out) { *out = g_chooser; return true; }

static void fresh_binding() {
    Binding& b = binding();
    b.copy = copy_fn; b.clock = clock_fn; b.observer_scope = scope_fn; b.chooser_scope = chooser_scope_fn;
    b.get_last_error = get_err; b.set_last_error = set_err; b.mailbox.reset();
    b.table.clear(); b.ops = NativeOps{}; b.skip = SkipRequirement{};
    g_scope_calls = 0; g_mutate_on_call = 0; g_mutate_cols = nullptr; g_err = 0; g_chooser = {};
}
static bool read(DirectionSample* s) { ChooserArgs a{}; return read_direction(a, s); }


// ---- fake combo graph for the finisher node table + legality tests ----
static void p32(uint8_t* p, size_t off, uint32_t v) { std::memcpy(p + off, &v, 4); }
static void p64(uint8_t* p, size_t off, uint64_t v) { std::memcpy(p + off, &v, 8); }
struct FG {
    std::vector<uint8_t> nodes = std::vector<uint8_t>(16 * 0x38), table = std::vector<uint8_t>(4 * 0x118), defs = std::vector<uint8_t>(4 * 0x140);
    std::vector<uint32_t> conns = {5, 0, 6, 2, 7, 1}, req = {5, 3}, post = {9};
    std::array<uint32_t, 64> loadout{};
    uint8_t cur[0x38] = {}, combo[0x48] = {}, cfg[0xa0] = {};
    std::vector<uint32_t> hashes = {1, 2};
    std::vector<float> anim_values = std::vector<float>(4, 0.0f);
    uint8_t anim_out[64] = {};
    int dummy_cond = 0, dummy_state = 0, dummy_unlock = 0, dummy_graph = 0;
    FG() {
        p64(cur, 0, reinterpret_cast<uintptr_t>(conns.data())); p32(cur, 8, 3);
        for (uint32_t i = 5; i <= 7; ++i) {
            uint8_t* n = nodes.data() + i * 0x38;
            p32(n, 0x10, 100 + i); p32(n, 0x20, 1); p32(n, 0x24, 4); p32(n, 0x2c, 0);      // attack node, form node 4, attack entry 0
            n[0x31] = 0;
        }
        p32(nodes.data() + 4 * 0x38, 0x2c, 2);                                              // form node 4 -> def 2
        p32(defs.data() + 2 * 0x140, 0, 4242); p32(defs.data() + 2 * 0x140, 0x70, 3);       // def id 4242, category 3 (Loadout row = 4 ints)
        loadout[3] = 4242;
        uint8_t* e = table.data();
        p64(e, 0x90, reinterpret_cast<uintptr_t>(req.data())); p32(e, 0x98, 2); p64(e, 0xa0, reinterpret_cast<uintptr_t>(post.data())); p32(e, 0xa8, 0);
        e[0xb1] = 0;
        p64(e, 0xb8, reinterpret_cast<uintptr_t>(hashes.data())); p32(e, 0xc0, 2);
        anim_out[0] = 1; p64(anim_out, 1 * 16 + 8, reinterpret_cast<uintptr_t>(anim_values.data()));   // n = 1 => value pointer at +24
        p64(combo, 0, reinterpret_cast<uintptr_t>(nodes.data())); p32(combo,8,16); p32(combo, 0x20, 4);
        p64(nodes.data()+4*0x38,0,reinterpret_cast<uintptr_t>(conns.data())); p32(nodes.data()+4*0x38,8,3);
        p64(cfg, 8, reinterpret_cast<uintptr_t>(defs.data())); p64(cfg, 0x28, reinterpret_cast<uintptr_t>(table.data()));
    }
    ChooserArgs args() {
        ChooserArgs a{};
        a.node = cur; a.combo_data = combo; a.combo_cfg = cfg; a.loadout = loadout.data(); a.conditions = &dummy_cond;
        a.state_set = &dummy_state; a.unlock_data = &dummy_unlock; a.graph = &dummy_graph; a.anim_out = anim_out;
        return a;
    }
};
// fake native sub-steps
static bool g_gate = true, g_blocked = false, g_input = true, g_post = true;
static std::array<bool, 16> g_cond{};
static uint32_t g_seen = 0;   // bitmask of condition ids the native condition op was asked about
static bool op_gate(uint32_t, void*, void*) { return g_gate; }
static bool op_blocked(void*, uint64_t, void*, void*) { return g_blocked; }
static bool op_cond(uint32_t id, void*, void*) { if (id < 32) g_seen |= 1u << id; return id < g_cond.size() && g_cond[id]; }
static bool op_input(void*, void*, void*) { return g_input; }
static bool op_post(void*, void*, void*) { return g_post; }
static std::array<uint16_t, 8> g_anim_map{};   // hash (1..7) -> output index; 0xffff = unresolved
static bool op_anim(void*, uint32_t hash, uint16_t* idx) { *idx = hash < g_anim_map.size() ? g_anim_map[hash] : 0xffff; return true; }
static NativeOps fake_ops() { NativeOps o; o.unlock_gate = op_gate; o.state_blocked = op_blocked; o.condition = op_cond; o.input_ok = op_input; o.post_ok = op_post; o.anim_index = op_anim; return o; }
static bool test_dir_reader(const ChooserArgs&, DirectionSample* s) { *s = {1000, true, 1.0f, -1.0f, 77}; return true; }   // Back
static bool test_atk_reader(const ChooserArgs&, Attack* a) { *a = Attack::Primary; return true; }


// ---- address-wrap regressions (review CB29): a permissive copier that SUCCEEDS for any address and records suspicious ones ----
struct Range { uint64_t lo, hi; };
static std::vector<Range> g_ranges;
static int g_low_calls = 0, g_calls = 0;
static bool permissive_copy(void* dst, const void* src, size_t n) {
    ++g_calls;
    const uint64_t a = reinterpret_cast<uintptr_t>(src);
    for (const Range& r : g_ranges) if (a >= r.lo && a + n <= r.hi) { std::memcpy(dst, src, n); return true; }
    if (a < 0x1000000ull || a > combat_native::kUserMax) ++g_low_calls;       // a wrapped or out-of-range address reached the copier
    std::memset(dst, 0, n);
    return true;                                                                // would "succeed": only the arithmetic can stop it
}
static void map_range(const void* p, size_t n) { g_ranges.push_back({reinterpret_cast<uintptr_t>(p), reinterpret_cast<uintptr_t>(p) + n}); }

int main() {
    {
        FG graph;
        p32(graph.defs.data()+2*0x140,0x70,2);graph.loadout[2]=4242;
        auto args=graph.args();graph::Conn result[graph::kMaxConn]{};size_t count=0;
        CHECK(finisher_followups(args,4242,5,copy_fn,result,count));
        CHECK(count==2 && result[0].target==6 && result[1].target==7);
        CHECK(result[0].arg==2 && result[1].arg==1);
        CHECK(finisher_followups(args,4242,5,copy_fn,result,count,6));
        CHECK(count==1 && result[0].target==7); // Neither grounded nor airborne first entry can restart a follow-up chain.
        p32(graph.nodes.data()+7*0x38,0x24,graph::kNone);
        CHECK(finisher_followups(args,4242,5,copy_fn,result,count) && count==1);
        g_copy_ok=false;CHECK(!finisher_followups(args,4242,5,copy_fn,result,count));g_copy_ok=true;
        graph.loadout[2]=0;g_cond.fill(false);g_cond[5]=true;g_cond[3]=true;
        CHECK(!legal_node(args,6,copy_fn,fake_ops(),SkipRequirement{},2));
        auto local=args;auto loadout=graph.loadout;loadout[2]=4242;local.loadout=loadout.data();
        CHECK(legal_node(local,6,copy_fn,fake_ops(),SkipRequirement{},2));
        CHECK(graph.loadout[2]==0);
        g_gate=false;CHECK(!legal_node(local,6,copy_fn,fake_ops(),SkipRequirement{},2));g_gate=true;

    }

    {
        facing_movement::Vec3 reversed{};
        CHECK(facing_movement::reverse_movement({1,0,0},{0,1,0},reversed) && reversed.x<-0.99f);
        CHECK(facing_movement::reverse_movement({1,5,-1},{0,1,0},reversed) && reversed.x<0 && reversed.z>0 && reversed.y==0);
        CHECK(!facing_movement::reverse_movement({0,1,0},{0,1,0},reversed));
        CHECK(finisher_test_mode::known(2848499412u) && finisher_test_mode::known(4201352106u));
        CHECK(!finisher_test_mode::known(698021297u) && !finisher_test_mode::known(444122270u));
    }
    // ---- capture: copies exactly the player row ----
    {   Fake f; LockOnCopy c{}; MoveCopy m{};
        CHECK(capture_lockon(f.lockcols.data(), copy_fn, &c));
        CHECK(c.lock[0] == 1 && c.lock[0xa0] == 1 && c.target == ((7ull << 32) | 5));
        CHECK(c.row_id == reinterpret_cast<uintptr_t>(f.status.data()) + kRow * 0xf0);
        CHECK(c.aim_id == reinterpret_cast<uintptr_t>(f.aim.data()) + kRow * 0x40);
        CHECK(capture_movement(f.movecols.data(), copy_fn, &m));
        float x; std::memcpy(&x, m.move, 4); CHECK(x == -1.0f);
        CHECK(m.row_id == reinterpret_cast<uintptr_t>(f.movement.data()) + kRow * 0x80 && m.aim_id == c.aim_id); }
    // ---- capture fails closed ----
    {   Fake f; LockOnCopy c{}; MoveCopy m{};
        CHECK(!capture_lockon(nullptr, copy_fn, &c) && !capture_movement(nullptr, copy_fn, &m));
        CHECK(!capture_lockon(f.lockcols.data(), nullptr, &c));                         // no copier installed
        g_copy_ok = false; CHECK(!capture_lockon(f.lockcols.data(), copy_fn, &c) && !capture_movement(f.movecols.data(), copy_fn, &m)); g_copy_ok = true;
        f.lockcols[9] = cols::kMaxRow; CHECK(!capture_lockon(f.lockcols.data(), copy_fn, &c));         // absurd row index
        f.lockcols[9] = kRow; f.lockcols[6] = 0; CHECK(!capture_lockon(f.lockcols.data(), copy_fn, &c)); // null column
        f.lockcols[6] = reinterpret_cast<uintptr_t>(f.status.data()); f.lockcols[2] = 8; CHECK(!capture_lockon(f.lockcols.data(), copy_fn, &c)); // implausible pointer
        f.movecols[0] = 0; CHECK(!capture_movement(f.movecols.data(), copy_fn, &m));
        Fake g; const uint64_t none = 0xffffffffull; std::memcpy(g.sel.data() + kRow * 0x10, &none, 8);
        CHECK(capture_lockon(g.lockcols.data(), copy_fn, &c) && c.target == 0); }               // index -1 => no target

    const FakeCtx ctxA{1, 100, 11}, ctxB{1, 100, 22}, ctxW{2, 100, 11}, ctxF{1, 106, 11}, ctxZero{1, 0, 11};
    const Scope scopeA{1, 100, 11};
    DirectionSample s{};

    // ---- telemetry-only by default: no scope source / no error guard => nothing ever samples ----
    {   fresh_binding(); Fake f; Binding& b = binding();
        b.observer_scope = nullptr;
        observe_lockon(f.lockcols.data(), &ctxA, Phase::Post); observe_movement(f.movecols.data(), &ctxA, Phase::Post);
        g_chooser = scopeA; CHECK(!read(&s));
        CHECK(b.mailbox.counters().no_scope_source == 2 && b.mailbox.counters().lock_posts == 0);
        b.observer_scope = scope_fn; b.set_last_error = nullptr;
        observe_lockon(f.lockcols.data(), &ctxA, Phase::Post); observe_movement(f.movecols.data(), &ctxA, Phase::Post);
        CHECK(!read(&s) && b.mailbox.counters().no_error_guard == 2);
        b.set_last_error = set_err; b.chooser_scope = nullptr;                          // no chooser scope source
        observe_lockon(f.lockcols.data(), &ctxA, Phase::Post); observe_movement(f.movecols.data(), &ctxA, Phase::Post);
        CHECK(!read(&s));
        b.chooser_scope = chooser_scope_fn; b.clock = nullptr; CHECK(!read(&s)); }      // no clock
    // ---- happy path: POST captures under one scope equal to the current chooser scope ----
    {   fresh_binding(); Fake f; g_chooser = scopeA; g_err = 1234;
        observe_lockon(f.lockcols.data(), &ctxA, Phase::Post); observe_movement(f.movecols.data(), &ctxA, Phase::Post);
        CHECK(read(&s) && s.locked && s.stick_magnitude == 1.0f && s.target_alignment < -0.99f && s.target_session == ((7ull << 32) | 5));
        CHECK(g_err == 1234);                                                           // original's LastError preserved
        CHECK(binding().mailbox.counters().samples_ok == 1); }
    // ---- PRE captures are telemetry only, even with matching stamps ----
    {   fresh_binding(); Fake f; g_chooser = scopeA;
        observe_lockon(f.lockcols.data(), &ctxA, Phase::Pre); observe_movement(f.movecols.data(), &ctxA, Phase::Pre);
        CHECK(!read(&s) && binding().mailbox.counters().pre_posts == 2 && binding().mailbox.counters().lock_posts == 1);
        observe_lockon(f.lockcols.data(), &ctxA, Phase::Post);                           // mixed phases: lock Post, movement Pre
        CHECK(!read(&s));
        observe_movement(f.movecols.data(), &ctxA, Phase::Post); CHECK(read(&s)); }
    // ---- cross-actor / cross-world / cross-frame / zero stamps / chooser mismatch ----
    {   fresh_binding(); Fake f; g_chooser = scopeA;
        observe_lockon(f.lockcols.data(), &ctxA, Phase::Post); observe_movement(f.movecols.data(), &ctxB, Phase::Post);
        CHECK(!read(&s));                                                               // stable lock actor A + movement actor B
        observe_movement(f.movecols.data(), &ctxW, Phase::Post); CHECK(!read(&s));      // same actor, different world
        observe_movement(f.movecols.data(), &ctxF, Phase::Post); CHECK(!read(&s));      // same actor, 6 frames later (60 Hz)
        observe_movement(f.movecols.data(), &ctxZero, Phase::Post); CHECK(!read(&s));   // zero frame stamp: rejected before it is posted
        CHECK(binding().mailbox.counters().scope_rejected >= 1);
        observe_movement(f.movecols.data(), &ctxA, Phase::Post); CHECK(read(&s));       // restored: qualifies again
        g_chooser = {1, 101, 11}; CHECK(!read(&s));                                      // chooser is in a later frame
        g_chooser = {1, 100, 22}; CHECK(!read(&s));                                      // chooser belongs to another actor
        g_chooser = {2, 100, 11}; CHECK(!read(&s));                                      // chooser in another world
        g_chooser = {0, 100, 11}; CHECK(!read(&s));                                      // incomplete chooser scope
        g_chooser = scopeA; CHECK(read(&s)); }
    // ---- one side only ----
    {   fresh_binding(); Fake f; g_chooser = scopeA;
        observe_lockon(f.lockcols.data(), &ctxA, Phase::Post); CHECK(!read(&s));        // movement missing
        binding().mailbox.reset(); observe_movement(f.movecols.data(), &ctxA, Phase::Post); CHECK(!read(&s)); } // lock missing
    // ---- target switch coherence ----
    {   fresh_binding(); Fake f; g_chooser = scopeA;
        observe_lockon(f.lockcols.data(), &ctxA, Phase::Post); observe_movement(f.movecols.data(), &ctxA, Phase::Post);
        CHECK(read(&s) && s.target_session == ((7ull << 32) | 5));
        // frame 101: the target switches; the new handle is visible but LockStatus still holds the OLD position (pre-original)
        const uint64_t h2 = (9ull << 32) | 6; std::memcpy(f.sel.data() + kRow * 0x10, &h2, 8);
        const FakeCtx next{1, 101, 11}; g_chooser = {1, 101, 11};
        observe_lockon(f.lockcols.data(), &next, Phase::Pre); observe_movement(f.movecols.data(), &next, Phase::Pre);
        CHECK(!read(&s));                                                               // pre-original: new handle + previous position never sample
        observe_lockon(f.lockcols.data(), &next, Phase::Post);                           // lock updated in frame 101, movement still frame 100
        CHECK(!read(&s));
        observe_movement(f.movecols.data(), &next, Phase::Post);
        CHECK(read(&s) && s.target_session == h2); }                                    // coherent only when both are post-original of the same frame
    // ---- scope or row changing around the post copy ----
    {   fresh_binding(); Fake f; g_chooser = scopeA;
        g_mutate_cols = &f.lockcols; g_mutate_on_call = 2;                               // the row moves between the two scope validations
        observe_lockon(f.lockcols.data(), &ctxA, Phase::Post); g_mutate_cols = nullptr; f.lockcols[9] = kRow;
        observe_movement(f.movecols.data(), &ctxA, Phase::Post);
        CHECK(!read(&s) && binding().mailbox.counters().lock_posts == 0 && binding().mailbox.counters().scope_rejected == 1); }
    {   fresh_binding(); Fake f; g_chooser = scopeA; g_copy_ok = false;                 // copy failure after a valid scope: nothing posted
        observe_lockon(f.lockcols.data(), &ctxA, Phase::Post); g_copy_ok = true;
        CHECK(binding().mailbox.counters().lock_posts == 0 && binding().mailbox.counters().scope_rejected == 1); }
    {   fresh_binding(); Fake f; observe_lockon(f.lockcols.data(), nullptr, Phase::Post);  // scope source fails (no ctx)
        CHECK(binding().mailbox.counters().lock_posts == 0 && binding().mailbox.counters().scope_rejected == 1); }
    // ---- primed-valid -> failed capture in the SAME scope must invalidate that stream (naga #14173) ----
    for (int stream = 0; stream < 2; ++stream) {                                        // 0 = lock-on, 1 = movement
        for (int cause = 0; cause < 6; ++cause) {
            fresh_binding(); Fake f; g_chooser = scopeA; Binding& b = binding();
            observe_lockon(f.lockcols.data(), &ctxA, Phase::Post); observe_movement(f.movecols.data(), &ctxA, Phase::Post);
            if (!read(&s)) { CHECK(false && "primed pair must sample"); continue; }
            const void* ctx = &ctxA; g_mutate_cols = nullptr;
            switch (cause) {
                case 0: g_copy_ok = false; break;                                        // copy failure
                case 1: ctx = &ctxZero; break;                                           // incomplete stamp
                case 2: b.observer_scope = nullptr; break;                               // scope source missing
                case 3: b.set_last_error = nullptr; break;                               // error guard missing
                case 4: ctx = nullptr; break;                                            // scope source fails
                case 5: if (stream == 0) { g_mutate_cols = &f.lockcols; g_mutate_on_call = g_scope_calls + 2; } else { ctx = &ctxZero; } break;   // row moves during copy (lock) / stamp (movement)
            }
            if (stream == 0) observe_lockon(f.lockcols.data(), ctx, Phase::Post); else observe_movement(f.movecols.data(), ctx, Phase::Post);
            g_copy_ok = true; g_mutate_cols = nullptr; f.lockcols[9] = kRow;
            b.observer_scope = scope_fn; b.set_last_error = set_err;
            CHECK(!read(&s));                                                            // old data in the same world/frame/actor must NOT survive
            CHECK(binding().mailbox.counters().invalidations >= 1);
            // recovery: a fresh qualified pair samples again
            observe_lockon(f.lockcols.data(), &ctxA, Phase::Post); observe_movement(f.movecols.data(), &ctxA, Phase::Post); CHECK(read(&s));
        }
    }
    // ---- failed PRE capture also invalidates (copy failure) ----
    {   fresh_binding(); Fake f; g_chooser = scopeA;
        observe_lockon(f.lockcols.data(), &ctxA, Phase::Post); observe_movement(f.movecols.data(), &ctxA, Phase::Post); CHECK(read(&s));
        g_copy_ok = false; observe_movement(f.movecols.data(), &ctxA, Phase::Pre); g_copy_ok = true; CHECK(!read(&s)); }
    // ---- LastError is restored even when the post capture is rejected ----
    {   fresh_binding(); Fake f; g_err = 77; observe_lockon(f.lockcols.data(), &ctxZero, Phase::Post); CHECK(g_err == 77); }
    // ---- different AimData rows in the two bodies: necessary condition fails ----
    {   fresh_binding(); Fake f; Fake other; g_chooser = scopeA;
        observe_lockon(f.lockcols.data(), &ctxA, Phase::Post); observe_movement(other.movecols.data(), &ctxA, Phase::Post);
        CHECK(!read(&s)); }
    // ---- not locked / invalid rows still refuse (no invented sample) ----
    {   fresh_binding(); Fake f; g_chooser = scopeA; f.status[kRow * 0xf0] = 0;
        observe_lockon(f.lockcols.data(), &ctxA, Phase::Post); observe_movement(f.movecols.data(), &ctxA, Phase::Post);
        CHECK(!read(&s)); }
    // ---- attack reader + Module stays fail-closed with the partial readers ----
    {   fresh_binding(); Fake f; ChooserArgs args{}; Attack a = Attack::None;
        std::array<uint8_t, rows::kMinCombo> combo{}; args.combo_data = combo.data();
        CHECK(read_attack(args, &a) && a == Attack::None);
        combo[0x28] = 1; CHECK(read_attack(args, &a) && a == Attack::Primary);
        combo[0x28] = 0; combo[0x2a] = 1; CHECK(read_attack(args, &a) && a == Attack::Secondary);
        args.combo_data = nullptr; CHECK(!read_attack(args, &a));
        g_chooser = scopeA; Module m; m.set_readers(partial_readers()); m.enable_finishers(true); uint32_t node = 0;
        combo[0x28] = 1; args.combo_data = combo.data();
        for (uint32_t t = 4000; t < 4200; t += 16) {
            observe_lockon(f.lockcols.data(), &ctxA, Phase::Post); observe_movement(f.movecols.data(), &ctxA, Phase::Post);
            m.on_chooser(args, t, &node);
        }
        CHECK(!m.on_chooser(args, 4200, &node)); }                                      // node lookup + legality not provided


    // ---- NodeTable: only evidence-shaped entries ----
    {   NodeTable t;
        CHECK(!t.add({4242, Finisher::None, 105, 5}) && !t.add({0, Finisher::WideEdge, 105, 5}) && !t.add({4242, Finisher::WideEdge, 0xffffffffu, 5}) && !t.add({4242, Finisher::WideEdge, 105, 5000}));
        CHECK(t.add({4242, Finisher::WideEdge, 105, 5}) && !t.add({4242, Finisher::WideEdge, 106, 6}) && t.add({4243, Finisher::WideEdge, 106, 6}));
        CHECK(t.size() == 2 && t.find(4242, Finisher::WideEdge)->node_id == 105 && !t.find(4242, Finisher::SpinEdge));
        for (uint32_t i = 0; i < 100; ++i) t.add({5000 + i, Finisher::SpinEdge, i, i});
        CHECK(t.size() == NodeTable::kCap); t.clear(); CHECK(t.size() == 0); }
    // ---- finisher node lookup (cb18): the evidence entry's node index is validated against the live shared node array ----
    {   fresh_binding();FG g;auto a=g.args();auto& b=binding();uint32_t idx=99,form=0;
        CHECK(b.table.add({4242,Finisher::UppercutSurge,105,5,106,6}));
        p64(g.nodes.data()+6*graph::kNode,0x18,0x18);p32(g.table.data(),0x28,1);
        CHECK(find_finisher_node(a,Finisher::UppercutSurge,&idx,&form,false,true) && idx==6 && form==4242);
        CHECK(find_finisher_node(a,Finisher::UppercutSurge,&idx,&form,false,false) && idx==5);
        p32(g.nodes.data()+6*graph::kNode,0x10,999);CHECK(!find_finisher_node(a,Finisher::UppercutSurge,&idx,&form,false,true));p32(g.nodes.data()+6*graph::kNode,0x10,106);
        p64(g.nodes.data()+6*graph::kNode,0x18,0);CHECK(!find_finisher_node(a,Finisher::UppercutSurge,&idx,&form,false,true));p64(g.nodes.data()+6*graph::kNode,0x18,0x18);
        p32(g.table.data(),0x28,0);CHECK(!find_finisher_node(a,Finisher::UppercutSurge,&idx,&form,false,true));p32(g.table.data(),0x28,1);
        g.loadout[3]=0;CHECK(!find_finisher_node(a,Finisher::UppercutSurge,&idx,&form,true,true));CHECK(find_finisher_node(a,Finisher::UppercutSurge,&idx,&form,false,true));
        g_cond.fill(false);g_cond[1]=true;g_cond[3]=true;g_gate=false;g_input=true;g_blocked=false;
        g.loadout[3]=4242;CHECK(!legal_node(a,6,copy_fn,fake_ops(),SkipRequirement{},0)); // Air variant never bypasses unlock.
        g_gate=true;g_cond[3]=false;CHECK(!legal_node(a,6,copy_fn,fake_ops(),SkipRequirement{},0));g_cond[3]=true;
    }
    {   fresh_binding(); FG g; ChooserArgs a = g.args(); uint32_t idx = 99; Binding& b = binding();
        CHECK(!read_finisher_node(a, Finisher::UppercutSurge, &idx));                            // empty table
        CHECK(b.table.add({4242, Finisher::UppercutSurge, 105, 5}));
        CHECK(read_finisher_node(a, Finisher::UppercutSurge, &idx) && idx == 5);
        CHECK(!read_finisher_node(a, Finisher::WideEdge, &idx));                                 // no entry for that finisher
        CHECK(!read_finisher_node(a, Finisher::None, &idx));
        // NOT limited to the current node's connections: node 8 is not a connection of the current node but validates
        p32(g.nodes.data() + 8 * 0x38, 0x10, 108); p32(g.nodes.data() + 8 * 0x38, 0x24, 4); p32(g.nodes.data() + 8 * 0x38, 0x20, 1);
        b.table.clear(); b.table.add({4242, Finisher::WideEdge, 108, 8}); CHECK(read_finisher_node(a, Finisher::WideEdge, &idx) && idx == 8);
        b.table.clear(); b.table.add({4243, Finisher::UppercutSurge, 105, 5}); CHECK(!read_finisher_node(a, Finisher::UppercutSurge, &idx));   // other weapon's form def
        b.table.clear(); b.table.add({4242, Finisher::UppercutSurge, 777, 5}); CHECK(!read_finisher_node(a, Finisher::UppercutSurge, &idx));   // content id differs: stale evidence
        b.table.clear(); b.table.add({4242, Finisher::UppercutSurge, 105, 6}); CHECK(!read_finisher_node(a, Finisher::UppercutSurge, &idx));   // index points at another node
        b.table.clear(); b.table.add({4242, Finisher::UppercutSurge, 105, 5});
        p32(g.nodes.data() + 5 * 0x38, 0x24, 0xffffffffu); CHECK(!read_finisher_node(a, Finisher::UppercutSurge, &idx));                       // node without a form: no def id
        p32(g.nodes.data() + 5 * 0x38, 0x24, 4);
        b.table.add({4244, Finisher::UppercutSurge, 105, 5});                                    // a second weapon's entry cannot validate here (def id differs)
        CHECK(read_finisher_node(a, Finisher::UppercutSurge, &idx) && idx == 5);
        g.defs[2 * 0x140] = 0;   // def 2 id now 4242 -> second table entry would also need 4244: keep unambiguous
        g_copy_ok = false; CHECK(!read_finisher_node(a, Finisher::UppercutSurge, &idx)); g_copy_ok = true;
        b.table.clear(); b.table.add({4242, Finisher::UppercutSurge, 105, 5}); b.table.add({4242, Finisher::UppercutSurge, 105, 5});
        CHECK(b.table.size() == 1);                                                              // duplicates never reach the lookup
        a.combo_data = nullptr; CHECK(!read_finisher_node(a, Finisher::UppercutSurge, &idx));
        // guarded-copy regression (naga #14649): an UNREADABLE second candidate must not be skipped as a non-match
        {   FG h; ChooserArgs ha = h.args(); Binding& bb = binding(); bb.table.clear();
            p32(h.nodes.data() + 6 * 0x38, 0x10, 105); p32(h.nodes.data() + 6 * 0x38, 0x24, 4);                       // node 6 duplicates node 5 (id 105, form 4)
            bb.table.add({4242, Finisher::UppercutSurge, 105, 5}); bb.table.add({4242, Finisher::UppercutSurge, 105, 6});   // one entry: duplicates are refused by add()
            CHECK(bb.table.size() == 1);
            bb.table.clear(); bb.table.add({4242, Finisher::UppercutSurge, 105, 5}); bb.table.add({4243, Finisher::UppercutSurge, 105, 6});
            CHECK(bb.table.size() == 2);                                                                              // different weapons' entries (form defs differ)
            uint32_t hi = 99;
            CHECK(read_finisher_node(ha, Finisher::UppercutSurge, &hi) && hi == 5);                                   // node 6's def (4242) != 4243: real non-match
            const uintptr_t n6 = reinterpret_cast<uintptr_t>(h.nodes.data()) + 6 * 0x38;
            g_fail_lo = n6; g_fail_hi = n6 + 0x38;                                                                    // the second candidate cannot be read
            CHECK(!read_finisher_node(ha, Finisher::UppercutSurge, &hi));                                             // unknown, NOT skipped: refuse
            g_fail_lo = g_fail_hi = 0;
            const uintptr_t n4 = reinterpret_cast<uintptr_t>(h.nodes.data()) + 4 * 0x38;                              // form node 4 unreadable: def lookup unknown
            g_fail_lo = n4; g_fail_hi = n4 + 0x38; CHECK(!read_finisher_node(ha, Finisher::UppercutSurge, &hi)); g_fail_lo = g_fail_hi = 0;
            const uintptr_t d2 = reinterpret_cast<uintptr_t>(h.defs.data()) + 2 * 0x140;                              // definition 2 unreadable
            g_fail_lo = d2; g_fail_hi = d2 + 0x140; CHECK(!read_finisher_node(ha, Finisher::UppercutSurge, &hi)); g_fail_lo = g_fail_hi = 0;
            p32(h.nodes.data() + 6 * 0x38, 0x10, 999);                                                                // readable node 6 with a different id: a real non-match
            g_fail_lo = n6 + 0x30; g_fail_hi = n6 + 0x38;                                                             // (a read of its tail would fail, but the node is not read there)
            CHECK(read_finisher_node(ha, Finisher::UppercutSurge, &hi) == false);                                     // whole-node read overlaps the failing tail: unknown
            g_fail_lo = g_fail_hi = 0; CHECK(read_finisher_node(ha, Finisher::UppercutSurge, &hi) && hi == 5); }
        // equipped-weapon filter (naga #14716): both weapons' entries validate against the shared graph; only the equipped one counts
        {   FG h; ChooserArgs ha = h.args(); Binding& bb = binding(); uint32_t hi = 99;
            p32(h.nodes.data() + 9 * 0x38, 0x2c, 3);                                                              // form node 9 -> def 3
            p32(h.defs.data() + 3 * 0x140, 0, 4243); p32(h.defs.data() + 3 * 0x140, 0x70, 2);                      // def 3: id 4243, category 2
            p32(h.nodes.data() + 6 * 0x38, 0x24, 9);                                                              // node 6 (id 106) belongs to the second weapon
            bb.table.clear(); bb.table.add({4242, Finisher::UppercutSurge, 105, 5}); bb.table.add({4243, Finisher::UppercutSurge, 106, 6});
            h.loadout.fill(0); h.loadout[3] = 4242;
            CHECK(read_finisher_node(ha, Finisher::UppercutSurge, &hi) && hi == 5);                                // weapon A equipped
            h.loadout[3] = 0; h.loadout[2] = 4243;
            CHECK(read_finisher_node(ha, Finisher::UppercutSurge, &hi) && hi == 6);                                // switching the Loadout flips the choice
            h.loadout[2] = 0; CHECK(!read_finisher_node(ha, Finisher::UppercutSurge, &hi));                         // neither equipped
            h.loadout[3] = 4242; h.loadout[2] = 4243; CHECK(!read_finisher_node(ha, Finisher::UppercutSurge, &hi)); // both equipped: ambiguous
            h.loadout[2] = 0;
            ha.loadout = nullptr; CHECK(!read_finisher_node(ha, Finisher::UppercutSurge, &hi)); ha.loadout = h.loadout.data();   // no Loadout row
            const uintptr_t lo = reinterpret_cast<uintptr_t>(h.loadout.data());
            g_fail_lo = lo; g_fail_hi = lo + 16; CHECK(!read_finisher_node(ha, Finisher::UppercutSurge, &hi)); g_fail_lo = g_fail_hi = 0;   // Loadout unreadable
            p32(h.defs.data() + 2 * 0x140, 0x70, 4);                                                              // category 4 is never equipped (would read the next row)
            h.loadout[3] = 4242; h.loadout[4 % 4] = 0; CHECK(!read_finisher_node(ha, Finisher::UppercutSurge, &hi));
            p32(h.defs.data() + 2 * 0x140, 0x70, 3);
            CHECK(read_finisher_node(ha, Finisher::UppercutSurge, &hi) && hi == 5);
            bb.table.clear(); }
    }
    // ---- native legality (re-implementation of FUN_14235dee0 with ONLY the skip id met) ----
    {   fresh_binding(); FG g; ChooserArgs a = g.args(); Binding& b = binding();
        b.ops = fake_ops(); b.skip = {true, 5};
        g_gate = true; g_blocked = false; g_input = true; g_post = true; g_cond.fill(false); g_cond[3] = true;      // 5 unmet but skipped; 3 met; 7 false (ok)
        g_seen = 0; CHECK(read_legal(a, 5));
        CHECK(!(g_seen & (1u << 5)) && (g_seen & (1u << 3)) && (g_seen & (1u << 7)));              // the skipped id is never evaluated; the others are
        b.skip = {false, 5}; CHECK(!read_legal(a, 5));                                            // skip id unset => never a bypass
        b.skip = {true, 3}; CHECK(!read_legal(a, 5));                                             // wrong skip id: requirement 5 genuinely unmet
        b.skip = {true, 5}; g_cond[3] = false; CHECK(!read_legal(a, 5)); g_cond[3] = true;       // another requirement unmet
        g_cond[7] = true; CHECK(!read_legal(a, 5)); g_cond[7] = false;                            // trailing id-7 test must be FALSE
        NativeOps part = fake_ops(); part.post_ok = nullptr; b.ops = part; CHECK(!read_legal(a, 5)); b.ops = fake_ops();   // missing native op
        g_gate = false; CHECK(!read_legal(a, 5)); g_gate = true;
        g_blocked = true; CHECK(!read_legal(a, 5)); g_blocked = false;
        CHECK(!read_legal(a, 9));                                                                 // node 9 is an all-zero node: no valid form/def => illegal
        p32(g.nodes.data() + 8 * 0x38, 0x10, 108); p32(g.nodes.data() + 8 * 0x38, 0x20, 1); p32(g.nodes.data() + 8 * 0x38, 0x24, 4);
        CHECK(!read_legal(a, 8));                                                                 // not a connection of a.node: refused (its own dee0 cannot replace the parent gate)
        { FG h; ChooserArgs ha = h.args(); ha.node = nullptr; CHECK(!read_legal(ha, 5)); }       // unreadable connection list: refused
        g.loadout[3] = 1; CHECK(!read_legal(a, 5)); g.loadout[3] = 4242;                         // different weapon equipped
        p32(g.defs.data() + 2 * 0x140, 0x70, 4); CHECK(!read_legal(a, 5)); p32(g.defs.data() + 2 * 0x140, 0x70, 3);   // category 4 would read the next entity's Loadout row: refused
        p32(g.nodes.data() + 5 * 0x38, 0x20, 2); CHECK(read_legal(a, 5)); p32(g.nodes.data() + 5 * 0x38, 0x20, 1);      // non-attack node types are legal
        g.table[0xb1] = 1; CHECK(!read_legal(a, 5)); CHECK(read_legal(a, 6));
        uint32_t shortcut_arg=0;
        CHECK(shortcut_entry_arg(a,5,copy_fn,shortcut_arg) && shortcut_arg==2);
        CHECK(shortcut_entry_arg(a,6,copy_fn,shortcut_arg) && shortcut_arg==2);
        CHECK(!legal_node(a,6,copy_fn,b.ops,SkipRequirement{},shortcut_arg)); // Unmet requirement remains enforced.
        g_cond[5]=true;
        CHECK(legal_node(a,6,copy_fn,b.ops,SkipRequirement{},shortcut_arg));
        g_gate=false; CHECK(!legal_node(a,6,copy_fn,b.ops,SkipRequirement{},shortcut_arg)); g_gate=true;
        g_cond[5]=false;
        g.table[0xb1] = 0; CHECK(shortcut_entry_arg(a,5,copy_fn,shortcut_arg) && shortcut_arg==0);
        // list ending in id 7: that id must HOLD and no trailing test happens
        g.req = {3, 7}; p64(g.table.data(), 0x90, reinterpret_cast<uintptr_t>(g.req.data()));
        g_cond[7] = false; CHECK(!read_legal(a, 5)); g_cond[7] = true; CHECK(read_legal(a, 5)); g_cond[7] = false;
        g.req = {}; p32(g.table.data(), 0x98, 0); CHECK(read_legal(a, 5)); g_cond[7] = true; CHECK(!read_legal(a, 5)); g_cond[7] = false;   // empty list: only the id-7 test
        // skip id 7 is unsupported (implicit trailing negative guard must stay): fails closed, never suppresses the guard
        g.req = {3}; p32(g.table.data(), 0x98, 1); p64(g.table.data(), 0x90, reinterpret_cast<uintptr_t>(g.req.data()));
        g_cond[7] = true; b.skip = {true, 7}; CHECK(!read_legal(a, 5)); b.skip = {true, 5}; CHECK(!read_legal(a, 5));   // cond7 true + 7 absent from the list: native rejects
        g_cond[7] = false; b.skip = {true, 7}; CHECK(!read_legal(a, 5)); b.skip = {true, 5}; CHECK(read_legal(a, 5));
        g.req = {7, 3}; p32(g.table.data(), 0x98, 2); p64(g.table.data(), 0x90, reinterpret_cast<uintptr_t>(g.req.data()));    // non-terminal 7: trailing guard still evaluated
        g_cond[7] = true; CHECK(!read_legal(a, 5)); g_cond[7] = false; CHECK(!read_legal(a, 5));                               // contradictory => always illegal, as native
        g.req = {5, 3}; p32(g.table.data(), 0x98, 2); p64(g.table.data(), 0x90, reinterpret_cast<uintptr_t>(g.req.data()));
        // post list + flag truth table
        g.req = {5, 3}; p32(g.table.data(), 0x98, 2); p64(g.table.data(), 0x90, reinterpret_cast<uintptr_t>(g.req.data())); p32(g.table.data(), 0xa8, 1);
        g_input = false; g_post = true; g.nodes[5 * 0x38 + 0x31] = 0; CHECK(read_legal(a, 5));    // flag clear: post alone decides
        g.nodes[5 * 0x38 + 0x31] = 1; CHECK(!read_legal(a, 5)); g_input = true; CHECK(read_legal(a, 5));   // flag set: post AND input
        g_post = false; CHECK(!read_legal(a, 5));
        p32(g.table.data(), 0xa8, 0); g_input = false; CHECK(!read_legal(a, 5)); g_input = true; CHECK(read_legal(a, 5));   // no post list: input decides
        p32(g.table.data(), 0x98, 65); CHECK(!read_legal(a, 5)); }                                 // absurd list count
    // ---- chooser phase-2 window (naga #14600): animation-parameter hash list must resolve to a value > 0 ----
    {   fresh_binding(); FG g; ChooserArgs a = g.args(); Binding& b = binding();
        b.ops = fake_ops(); b.skip = {true, 5};
        g_gate = true; g_blocked = false; g_input = true; g_post = true; g_cond.fill(false); g_cond[3] = true; g_anim_map.fill(0xffff);
        CHECK(read_legal(a, 5));                                                                  // phase_is_2 == 0: no window test
        a.phase_is_2 = 1;
        CHECK(!read_legal(a, 5));                                                                 // phase 2 and nothing resolves: window CLOSED
        g_anim_map[1] = 0; g.anim_values[0] = 0.0f; CHECK(!read_legal(a, 5));                     // resolves but value is 0.0
        g.anim_values[0] = -1.0f; CHECK(!read_legal(a, 5));
        g.anim_values[0] = 0.5f; CHECK(read_legal(a, 5));                                         // first hash open
        g_anim_map[1] = 0xffff; g_anim_map[2] = 1; g.anim_values[1] = 2.0f; CHECK(read_legal(a, 5));   // any one hash suffices
        g_anim_map[2] = 3; g.anim_values[3] = 1.0f; CHECK(read_legal(a, 5));                      // another output index
        p32(g.table.data(), 0xc0, 0); CHECK(!read_legal(a, 5)); p32(g.table.data(), 0xc0, 2);     // empty hash list: native loop finds nothing => closed
        p32(g.table.data(), 0xc0, 65); CHECK(!read_legal(a, 5)); p32(g.table.data(), 0xc0, 2);    // absurd count
        NativeOps no = fake_ops(); no.anim_index = nullptr; b.ops = no; CHECK(!read_legal(a, 5)); b.ops = fake_ops();   // unbound lookup op
        a.anim_out = nullptr; CHECK(!read_legal(a, 5)); a.anim_out = g.anim_out;
        g_copy_ok = false; CHECK(!read_legal(a, 5)); g_copy_ok = true;
        g.anim_out[0] = 0; CHECK(!read_legal(a, 5)); g.anim_out[0] = 1; }                         // value pointer read from the wrong slot (n=0): unreadable/zero => closed
    // ---- Module with all four readers: chooses only with table + ops + skip installed ----
    {   fresh_binding(); FG g; ChooserArgs a = g.args(); Binding& b = binding(); uint32_t node = 0;
        g_gate = true; g_blocked = false; g_input = true; g_post = true; g_cond.fill(false); g_cond[3] = true;
        Readers r = complete_readers(); r.direction_sample = test_dir_reader; r.attack_press = test_atk_reader;
        Module m; m.set_readers(r); m.enable_finishers(true);
        auto drive = [&](uint32_t t0) { bool h = false; for (uint32_t t = t0; t < t0 + 160; t += 16) h = m.on_chooser(a, t, &node); return h; };
        CHECK(!drive(0));                                                                          // nothing installed
        b.table.add({4242, Finisher::UppercutSurge, 105, 5}); CHECK(!drive(200));                    // table only: no native ops / skip id
        b.ops = fake_ops(); CHECK(!drive(400));                                                    // skip id still unset
        b.skip = {true, 5}; CHECK(drive(600) && node == 5);                                        // Back + Primary -> Uppercut Surge node
        g_gate = false; CHECK(!drive(800)); g_gate = true;                                         // native legality veto
        b.table.clear(); CHECK(!drive(1000)); }

    // ---- checked address arithmetic ----
    {   using combat_native::at; using combat_native::kUserMax;
        CHECK(at(0x100000, 3, 0x20) == 0x100060 && at(0x100000, 0, 0, 0x10) == 0x100010 && at(0x100000, 2, 8, 4) == 0x100014);
        CHECK(at(0, 1, 1) == 0 && at(0xffff, 0, 0) == 0 && at(0x10000, 0, 0) == 0x10000);                       // null / low base
        CHECK(at(kUserMax + 1, 0, 0) == 0 && at(~0ull, 0, 0) == 0 && at(~0ull - 0x1f, 0x801, 0x20) == 0);      // above user mode; the reviewed wrap case
        CHECK(at(0x100000, ~0ull, 2) == 0 && at(0x100000, 0x8000000000000000ull, 2) == 0);                      // index*stride overflow
        CHECK(at(0x100000, 0, 0, ~0ull) == 0 && at(kUserMax - 8, 0, 0, 8) == kUserMax && at(kUserMax - 8, 0, 0, 9) == 0);   // offset overflow and the exact boundary
        CHECK(at(kUserMax - 0x1f, 0, 0x20) == kUserMax - 0x1f && at(kUserMax - 0x1f, 1, 0x20) == 0);             // highest valid base still accepted
        CHECK(!combat_native::copy_checked(permissive_copy, &g_calls, at(~0ull - 0x1f, 0x801, 0x20), 8)); }
    // ---- row readers / graph helpers refuse wrapped sums without calling the copier ----
    {   g_ranges.clear(); g_low_calls = g_calls = 0;
        Fake f; map_range(f.lockcols.data(), 0x50); map_range(f.movecols.data(), 0x28);
        f.lockcols[6] = ~0ull - 0xef; f.lockcols[9] = 0x113;                                                   // LockStatus base 2^64-0xf0, row 0x113 -> wraps to 0x100e0 (plausible)
        LockOnCopy lc{}; CHECK(!capture_lockon(f.lockcols.data(), permissive_copy, &lc) && g_low_calls == 0);
        f.lockcols[6] = reinterpret_cast<uintptr_t>(f.status.data()); f.lockcols[9] = kRow; f.lockcols[2] = ~0ull - 0x3f; f.lockcols[9] = 0x401;   // AimData wrap
        CHECK(!capture_lockon(f.lockcols.data(), permissive_copy, &lc) && g_low_calls == 0);
        f.movecols[0] = ~0ull - 0x7f; f.movecols[4] = 0x201; MoveCopy mc{};                                     // Movement base 2^64-0x80, row 0x201 -> 0x10000
        CHECK(!capture_movement(f.movecols.data(), permissive_copy, &mc) && g_low_calls == 0);
        uint8_t nb[graph::kNode];
        CHECK(!graph::node_at(permissive_copy, ~0ull - 0x37, 0x5b0, nb) && g_low_calls == 0);
        uint8_t node[graph::kNode] = {}; node[0x24] = 4; map_range(node, sizeof node);
        uint8_t formnode[graph::kNode] = {}; formnode[0x2c] = 0; map_range(formnode, sizeof formnode);
        uint32_t id = 0, cat = 0;
        CHECK(!graph::form_def(permissive_copy, ~0ull - 0xff, ~0ull - 0x13f, node, &id, &cat) && g_low_calls == 0);   // wrapped nodes/defs bases: refused, copier untouched
        // boundary: the highest valid base is still readable when it is mapped
        std::vector<uint8_t> top(0x40); map_range(top.data(), top.size()); CHECK(graph::node_at(permissive_copy, reinterpret_cast<uintptr_t>(top.data()), 0, nb)); }
    // ---- scope probe: world + table offset must not leave user mode ----
    {   g_ranges.clear(); g_low_calls = g_calls = 0;
        struct Ctx { uint64_t w, a, b, c, d, e, f, g; } cx{}; uint8_t dtb[0x40] = {};
        cx.w = combat_native::kUserMax - 0x1000;                                                              // plausible head, but +0x58470 leaves user mode
        map_range(&cx, sizeof cx); map_range(dtb, sizeof dtb);
        scope_probe::Record r{};
        const void* ctxp = &cx;
        CHECK(scope_probe::capture(permissive_copy, scope_probe::kLockOn, combat_native::Phase::Post, ctxp, dtb, 0, 0, 1, &r) && (r.valid & 2) && !(r.valid & 4));
        CHECK(g_low_calls == 0 && g_calls == 3); }                                                              // ctx + world head + dt only: the table read never reached the copier
    // ---- scope probe: copies only, bounded ring, text format the offline analyzer reads ----
    {   static scope_probe::Ring ring;
        std::vector<uint8_t> world(0x58600, 0);
        struct Ctx { uint64_t w, a, b, c, d, e, f, g; } ctx{};
        uint8_t dt[0x40] = {};
        FILE* log = std::fopen(".work/native-build/scope_probe_sample.log", "w");
        CHECK(log != nullptr);
        for (uint32_t frame = 1; frame <= 8; ++frame) {
            const uint64_t counter = 5000 + frame; std::memcpy(world.data() + 0x10, &counter, 8);
            const uint32_t tick = frame * 16667; std::memcpy(dt + 8, &tick, 4);
            ctx.w = reinterpret_cast<uintptr_t>(world.data());
            for (int kind = 0; kind < 2; ++kind) {
                scope_probe::Record r{};
                CHECK(scope_probe::capture(copy_fn, kind ? scope_probe::kMovement : scope_probe::kLockOn, Phase::Post, &ctx, dt,
                                           0x1000 + kind, 0x2000, ring.next_seq(), &r));
                CHECK(r.valid == 15 && r.world == ctx.w && r.world_head[0x10] == (counter & 0xff));
                ring.push(r);
                static char line[4096];
                const size_t n = scope_probe::format_line(r, line, sizeof line);
                CHECK(n > 0 && line[0] == 'P' && line[n - 1] == '\n');
                if (log) std::fwrite(line, 1, n, log);
            }
        }
        if (log) std::fclose(log);
        CHECK(ring.size() == 16);
        scope_probe::Record r{};
        CHECK(ring.at(0, &r) && r.seq == 1 && ring.at(15, &r) && r.seq == 16 && !ring.at(16, &r));
        for (int i = 0; i < 300; ++i) { scope_probe::Record x{}; x.seq = 1000 + i; ring.push(x); }
        CHECK(ring.size() == scope_probe::Ring::kCapacity && ring.at(0, &r) && r.seq == 1044);   // last 256 of the 316 pushes
        // failures: bad pointers / failing copier produce partial or no record, never a crash
        scope_probe::Record bad{};
        CHECK(!scope_probe::capture(copy_fn, scope_probe::kLockOn, Phase::Post, nullptr, nullptr, 0, 0, 1, &bad));
        g_copy_ok = false; CHECK(!scope_probe::capture(copy_fn, scope_probe::kLockOn, Phase::Post, &ctx, dt, 0, 0, 1, &bad)); g_copy_ok = true;
        ctx.w = 8; scope_probe::Record part{};
        CHECK(scope_probe::capture(copy_fn, scope_probe::kLockOn, Phase::Post, &ctx, dt, 0, 0, 1, &part) && part.valid == 9);  // ctx + dt only
        char tiny[16]; CHECK(scope_probe::format_line(part, tiny, sizeof tiny) == 0);
    }

    std::printf(failures ? "combat native: %d FAILURE(S)\n" : "combat native: all checks passed\n", failures);
    return failures ? 1 : 0;
}
