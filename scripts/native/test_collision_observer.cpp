// Host-side proof of src/native/collision_observer.h (non-blocking 4-arg observer wrapper) over FAKE memory.
// build+run: scripts/native/test_combat_logic.sh (no game, no engine reads)
#include "../../src/native/collision_observer.h"
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#ifndef _WIN32
#include <thread>
#endif

using namespace collision_observer;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static bool copy_fn(void* dst, const void* src, size_t n) { std::memcpy(dst, src, n); return true; }
static thread_local uint32_t g_err = 0;   // like the real last-error: per thread
static std::atomic<uint32_t> g_clock{1000}, g_clock_step{0};   // atomic: now_fn is called from the producer threads of the concurrency test
static uint32_t get_err() { return g_err; }
static void set_err(uint32_t v) { g_err = v; }
static uint32_t tid_fn() { return 9; }
static uint32_t now_fn() { return g_clock.fetch_add(g_clock_step.load()); }
static uint64_t addr(const void* p) { return reinterpret_cast<uintptr_t>(p); }

// A copier that clobbers the "last error" like a real API call would, to prove the wrapper restores it.
static bool noisy_copy(void* dst, const void* src, size_t n) { g_err = 0xDEAD; std::memcpy(dst, src, n); return true; }

struct Fake {
    static constexpr uint64_t kRow = 1;
    std::vector<uint8_t> ctl = std::vector<uint8_t>(3 * 0x20), cc = std::vector<uint8_t>(3 * 0x20), tr = std::vector<uint8_t>(3 * 0x40);
    std::array<uint8_t, 0x100> container{};
    std::vector<uint8_t> entries = std::vector<uint8_t>(16 * 12);
    std::array<uint64_t, 0x1d> cols{};
    Fake() {
        cols[0] = addr(ctl.data()); cols[3] = addr(cc.data()); cols[9] = addr(tr.data()); cols[0x1c] = kRow;
        const uint64_t c = addr(container.data()), owner = 0xABCD0001;
        std::memcpy(ctl.data() + kRow * 0x20 + 8, &c, 8);
        std::memcpy(container.data() + 0x18, &owner, 8);
        set_list({});
    }
    void set_list(const std::vector<uint64_t>& handles) {
        const uint64_t p = addr(entries.data()); const uint32_t n = static_cast<uint32_t>(handles.size());
        std::memcpy(cc.data() + kRow * 0x20, &p, 8); std::memcpy(cc.data() + kRow * 0x20 + 8, &n, 4);
        for (size_t i = 0; i < handles.size() && i < 16; ++i) { std::memcpy(entries.data() + i * 12, &handles[i], 8); entries[i * 12 + 8] = static_cast<uint8_t>(i + 1); }
    }
};

// ---- fake "original" that records its arguments and (optionally) re-enters the wrapper / sets the last error ----
static std::vector<std::array<void*, 4>> g_calls;
static Observer* g_obs = nullptr;
static bool g_reenter = false;
static uint32_t g_orig_sets_err = 0;
static void fake_orig(void* a, void* b, void* c, void* d) {
    g_calls.push_back({a, b, c, d});
    if (g_orig_sets_err) g_err = g_orig_sets_err;
    if (g_reenter && g_obs) { g_reenter = false; g_obs->run(fake_orig, a, b, c, d); }
}


// ---- fixtures for the CB30 fixes ----
static std::atomic<int> g_gate{0}, g_parked{0};
static void parked_orig(void*, void*, void*, void*) { g_parked.store(1); while (!g_gate.load()) {} }
static void noop_orig(void*, void*, void*, void*) {}
static uint32_t g_alt_calls = 0;
static uint32_t alt_get() { ++g_alt_calls; return 0; }
static void alt_set(uint32_t) { ++g_alt_calls; }
static uint32_t& slow_depth() { g_err = 0xBAD; static thread_local uint32_t d = 0; return d; }   // a slow thread-local path that clobbers the last error
static uint32_t g_outer_seen = 0, g_nested_seen = 0; static bool g_first_done = false, g_nest = false;
static void tls_orig(void* a, void* b, void* c, void* d) {
    if (!g_first_done) { g_outer_seen = g_err; g_first_done = true; }
    if (g_nest) { g_nest = false; g_err = 55; g_obs->run(tls_orig, a, b, c, d); g_nested_seen = g_err; g_err = 4321; return; }
    g_nested_seen = g_err;                                                                              // nested path: the error seen by the nested original
}

struct Collected { std::vector<std::string> lines; size_t fail_after = ~size_t(0); };
static bool collect_sink(const char* line, size_t n, void* user) {
    Collected* c = static_cast<Collected*>(user);
    if (c->lines.size() >= c->fail_after) return false;
    c->lines.emplace_back(line, n);
    return true;
}

static void fresh(Observer& o, CopyFn cp = copy_fn, bool log_empty = false) {
    Config cfg; cfg.log_empty = log_empty;
    o.configure(cp, tid_fn, now_fn, get_err, set_err, cfg);
}

int main() {
    // ---- default OFF: only exact forward, nothing captured ----
    {   static Observer o; fresh(o); g_obs = &o; g_calls.clear(); Fake f; f.set_list({0xE1});
        void* a = f.cols.data(); void* b = reinterpret_cast<void*>(0x2222); void* c = reinterpret_cast<void*>(0x3333); void* d = reinterpret_cast<void*>(0x4444);
        CHECK(!o.enabled());
        o.run(fake_orig, a, b, c, d);
        CHECK(g_calls.size() == 1 && g_calls[0][0] == a && g_calls[0][1] == b && g_calls[0][2] == c && g_calls[0][3] == d);
        CHECK(o.counters().disabled_passthrough == 1 && o.counters().attempts == 0 && o.pending() == 0);
        Observer none; CHECK((none.enable(true), !none.enabled()));                    // cannot enable without the copier / LastError pair
        o.run(nullptr, a, b, c, d);                                                    // null original: nothing happens, no crash
        CHECK(g_calls.size() == 1); }
    // ---- enabled: exact forward, pre+post records, LastError preserved even when the copier clobbers it ----
    {   static Observer o; fresh(o, noisy_copy); g_obs = &o; g_calls.clear(); Fake f; f.set_list({0xE1, 0xE2});
        o.enable(true); CHECK(o.enabled());
        void* a = f.cols.data(); void* b = reinterpret_cast<void*>(0x2222); void* c = reinterpret_cast<void*>(0x3333); void* d = reinterpret_cast<void*>(0x4444);
        g_err = 77; g_orig_sets_err = 0;
        o.run(fake_orig, a, b, c, d);
        CHECK(g_calls.size() == 1 && g_calls[0][0] == a && g_calls[0][1] == b && g_calls[0][2] == c && g_calls[0][3] == d);
        CHECK(g_err == 77);                                                            // the observer never changed the error seen by the engine
        g_err = 5; g_orig_sets_err = 1234; o.run(fake_orig, a, b, c, d); CHECK(g_err == 1234); g_orig_sets_err = 0;   // an error set BY the original survives the post capture
        Counters k = o.counters(); CHECK(k.attempts == 4 && k.enqueued == 4 && o.pending() == 4);   // 2 runs x (pre, post); the list is not consumed by a fake original
        Collected col; FlushStop why{};
        CHECK(o.try_flush(collect_sink, &col, 100, 100, &why) == 4 && why == FlushStop::Empty && col.lines.size() == 4 && o.pending() == 0);
        CHECK(col.lines[0][0] == 'K' && col.lines[0].find(" E:e1/1,e2/2 ") != std::string::npos);
        CHECK(col.lines[0].find("K 1 0 ") == 0 && col.lines[1].find("K 2 1 ") == 0);   // pre then post, sequence order
        g_err = 0; }
    // ---- reentrancy guard: nested calls pass straight through ----
    {   static Observer o; fresh(o); g_obs = &o; g_calls.clear(); Fake f; f.set_list({0xE1}); o.enable(true); g_reenter = true;
        o.run(fake_orig, f.cols.data(), nullptr, nullptr, nullptr);
        CHECK(g_calls.size() == 2 && o.counters().nested_passthrough == 1 && o.counters().enqueued == 2);   // only the outer pre/post were captured
        CHECK(o.stop(10).captures_idle); }
    // ---- bounded ring: a full ring DROPS new records, keeps the queued ones, never blocks ----
    {   static Observer o; fresh(o); Fake f; f.set_list({0xE1}); o.enable(true);
        for (size_t i = 0; i < Observer::kCapacity + 50; ++i) o.observe(f.cols.data(), Phase::kPre);
        Counters k = o.counters(); CHECK(k.enqueued == Observer::kCapacity && k.dropped_full == 50 && o.pending() == Observer::kCapacity);
        Collected col; FlushStop why{};
        CHECK(o.try_flush(collect_sink, &col, 10, 100, &why) == 10 && why == FlushStop::MaxRecords && o.pending() == Observer::kCapacity - 10);
        CHECK(col.lines[0].find("K 1 0 ") == 0 && col.lines[9].find("K 10 0 ") == 0);       // the OLDEST are delivered first (nothing overwritten)
        for (int i = 0; i < 10; ++i) o.observe(f.cols.data(), Phase::kPre);                // freed slots are reusable
        CHECK(o.counters().enqueued == Observer::kCapacity + 10 && o.pending() == Observer::kCapacity); }
    // ---- try-publish: a failing sink RETAINS the record and everything behind it, in order, no duplicates ----
    {   static Observer o; fresh(o); Fake f; f.set_list({0xE1}); o.enable(true);
        for (int i = 0; i < 6; ++i) o.observe(f.cols.data(), Phase::kPre);
        Collected col; col.fail_after = 3; FlushStop why{};
        CHECK(o.try_flush(collect_sink, &col, 100, 100, &why) == 3 && why == FlushStop::SinkBusy && o.pending() == 3 && o.counters().flush_sink_failed == 1);
        col.fail_after = ~size_t(0);
        CHECK(o.try_flush(collect_sink, &col, 100, 100, &why) == 3 && why == FlushStop::Empty && col.lines.size() == 6);
        for (int i = 0; i < 6; ++i) { char want[16]; std::snprintf(want, sizeof want, "K %d 0 ", i + 1); CHECK(col.lines[static_cast<size_t>(i)].find(want) == 0); }   // order intact, no loss, no duplicate
        CHECK(o.try_flush(nullptr, &col, 10, 100, &why) == 0 && why == FlushStop::NoSink); }
    // ---- try-flush: busy flag, deadline cap (<=100 ms even when 5000 is requested), attempt cap ----
    {   static Observer o; fresh(o); Fake f; f.set_list({0xE1}); o.enable(true);
        for (int i = 0; i < 40; ++i) o.observe(f.cols.data(), Phase::kPre);
        struct Nest { Observer* o; FlushStop inner; size_t delivered; } nest{&o, FlushStop::Empty, 99};
        auto nested_sink = [](const char*, size_t, void* u) { Nest* n = static_cast<Nest*>(u); Collected c; n->delivered = n->o->try_flush(collect_sink, &c, 5, 100, &n->inner); return true; };
        FlushStop why{}; CHECK(o.try_flush(nested_sink, &nest, 1, 100, &why) == 1);
        CHECK(nest.delivered == 0 && nest.inner == FlushStop::Busy && o.counters().flush_busy == 1);   // second flusher returns at once
        g_clock = 5000; g_clock_step = 30; Collected col;                                              // each now() call advances 30 ms
        const size_t got = o.try_flush(collect_sink, &col, 1000, 5000, &why);
        CHECK(why == FlushStop::Deadline && got >= 1 && got <= 5);                                     // 100 ms cap honoured despite the 5000 ms request
        g_clock_step = 0;
        Observer big; fresh(big); big.enable(true);
        for (int i = 0; i < 200; ++i) big.observe(f.cols.data(), Phase::kPre);
        Collected col2; CHECK(big.try_flush(collect_sink, &col2, 1000, 100, &why) == kMaxFlushAttempts && why == FlushStop::Attempts && big.pending() == 200 - kMaxFlushAttempts); }
    // ---- filtering and capture failures are counted, not queued ----
    {   static Observer o; fresh(o); Fake f; o.enable(true);                                           // empty list + log_empty off
        o.observe(f.cols.data(), Phase::kPre); CHECK(o.counters().skipped_filter == 1 && o.pending() == 0);
        o.observe(nullptr, Phase::kPre); CHECK(o.counters().capture_failed == 1 && o.pending() == 0);
        static Observer e; fresh(e, copy_fn, true); e.enable(true); e.observe(f.cols.data(), Phase::kPre); CHECK(e.pending() == 1);
        static Observer w; Config cfg; cfg.watch_owner = 0xABCD0001; w.configure(copy_fn, tid_fn, now_fn, get_err, set_err, cfg); w.enable(true);
        w.observe(f.cols.data(), Phase::kPre); CHECK(w.pending() == 1);                                // watched owner is logged even when empty (integrator choice, no authority) }
    }
    // ---- stop(): seals recording, bounded, reports; enable() after a stop only toggles the sealed flag ----
    {   static Observer o; fresh(o); o.enable(true); Fake f; f.set_list({0xE1}); o.observe(f.cols.data(), Phase::kPre);
        g_clock = 0; g_clock_step = 10;
        StopReport r = o.stop(50);
        CHECK(r.sealed && r.captures_idle && r.pending == 1 && r.incomplete && r.admitted_inflight == 0 && !o.enabled());   // queued record => incomplete
        o.observe(f.cols.data(), Phase::kPost); CHECK(o.counters().post_skipped_after_stop == 1 && o.pending() == 1);        // sealed: nothing captured/published
        g_clock_step = 0; o.enable(true); CHECK(o.enabled() && o.frozen()); }
    // ---- configure() is IMMUTABLE after the first successful enable ----
    {   static Observer o; Config cfg; CHECK(o.configure(copy_fn, tid_fn, now_fn, get_err, set_err, cfg)); CHECK(!o.frozen());
        o.enable(true); CHECK(o.frozen());
        g_alt_calls = 0;
        CHECK(!o.configure(copy_fn, tid_fn, now_fn, alt_get, alt_set, cfg));                                 // rejected while enabled
        o.stop(10); CHECK(!o.configure(copy_fn, tid_fn, now_fn, alt_get, alt_set, cfg));                      // and after the stop / while sealed
        o.enable(true); Fake f; f.set_list({0xE1}); g_err = 9; o.run(noop_orig, f.cols.data(), nullptr, nullptr, nullptr);
        CHECK(g_alt_calls == 0 && g_err == 9); }                                                              // the ORIGINAL error functions are still the ones in use
    // ---- thread-local slow path clobbers the error: entry error restored before the original (first and nested), final = original's ----
    {   static Observer o; Config cfg; o.configure(noisy_copy, tid_fn, now_fn, get_err, set_err, cfg, slow_depth); o.enable(true); g_obs = &o;
        Fake f; f.set_list({0xE1}); g_err = 77; g_outer_seen = g_nested_seen = 0xFFFF; g_first_done = false; g_nest = true;
        o.run(tls_orig, f.cols.data(), nullptr, nullptr, nullptr);
        CHECK(g_outer_seen == 77);                                                                            // not 0xBAD / 0xDEAD
        CHECK(g_nested_seen == 55);                                                                           // nested path restored before the original too
        CHECK(g_err == 4321); g_err = 0; g_nest = false; }
    // ---- passthrough while disabled is exact and is not a capture ----
    {   static Observer o; fresh(o); g_obs = &o; g_calls.clear(); Fake f; void* x = reinterpret_cast<void*>(0x77);
        o.run(fake_orig, f.cols.data(), x, x, x); CHECK(g_calls.size() == 1 && o.counters().attempts == 0 && o.stop(10).captures_inflight == 0); }
    // ---- two instances flushing concurrently keep their own intact lines (per-instance buffer) ----
    {   std::unique_ptr<Observer> A(new Observer), B(new Observer); fresh(*A); fresh(*B); A->enable(true); B->enable(true);
        Fake fa, fb; const uint64_t ob = 0xABCD0002; std::memcpy(fb.container.data() + 0x18, &ob, 8); fa.set_list({0xE1}); fb.set_list({0xE2});
        for (int i = 0; i < 120; ++i) { A->observe(fa.cols.data(), Phase::kPre); B->observe(fb.cols.data(), Phase::kPre); }
        struct Chk { const char* want; const char* other; size_t bad = 0, good = 0; } ca{" abcd0001 ", " abcd0002 "}, cb{" abcd0002 ", " abcd0001 "};
        auto sink = [](const char* line, size_t n, void* u) { Chk* c = static_cast<Chk*>(u); std::string l(line, n);
            if (l.find(c->want) != std::string::npos && l.find(c->other) == std::string::npos && l.back() == '\n' && l[0] == 'K') ++c->good; else ++c->bad; return true; };
#ifndef _WIN32
        std::thread ta([&] { FlushStop w{}; while (A->pending()) if (!A->try_flush(sink, &ca, 64, 100, &w)) break; });
        std::thread tb([&] { FlushStop w{}; while (B->pending()) if (!B->try_flush(sink, &cb, 64, 100, &w)) break; });
        ta.join(); tb.join();
        CHECK(ca.bad == 0 && cb.bad == 0 && ca.good == 120 && cb.good == 120);
#endif
    }
    // ---- stop() vs capture race: after captures_idle no later capture/publish happens (TSan clean) ----
    {   for (int round = 0; round < 6; ++round) {
            std::unique_ptr<Observer> o(new Observer); fresh(*o); o->enable(true); Fake f; f.set_list({0xE1});
            std::atomic<bool> stopping{false};
#ifndef _WIN32
            std::vector<std::thread> th;
            for (int t = 0; t < 3; ++t) th.emplace_back([&] { for (int i = 0; i < 4000; ++i) { o->run(noop_orig, f.cols.data(), nullptr, nullptr, nullptr); if (stopping.load() && i > 3000) break; } });
            while (o->counters().attempts < 30) {}
            StopReport r = o->stop(100); stopping = true;
            CHECK(r.sealed && r.captures_idle);
            const Counters k1 = o->counters(); const size_t p1 = o->pending();
            for (auto& t : th) t.join();
            const Counters k2 = o->counters();
            CHECK(k2.enqueued == k1.enqueued && k2.attempts == k1.attempts && o->pending() == p1);          // nothing captured or published after the seal
            CHECK(k2.post_skipped_after_stop >= k1.post_skipped_after_stop);
#endif
        } }
    // ---- parked original: stop() does NOT wait for it, reports it, evidence stays incomplete even with a drained queue ----
    {
#ifndef _WIN32
        std::unique_ptr<Observer> o(new Observer); fresh(*o); o->enable(true); Fake f; f.set_list({0xE1}); g_gate = 0; g_parked = 0;
        std::thread t([&] { o->run(parked_orig, f.cols.data(), nullptr, nullptr, nullptr); });
        while (!g_parked.load()) {}
        StopReport r = o->stop(100);
        CHECK(r.sealed && r.captures_idle && r.admitted_inflight == 1 && r.incomplete && r.pending == 1);       // returned although the original is still running
        Collected col; FlushStop w{}; CHECK(o->try_flush(collect_sink, &col, 10, 100, &w) == 1);
        StopReport r2 = o->stop(100);
        CHECK(r2.pending == 0 && r2.post_skipped_after_stop == 0 && r2.admitted_inflight == 1 && r2.incomplete);   // incomplete SOLELY because of the parked original
        g_gate = 1; t.join();
        StopReport r3 = o->stop(100);
        CHECK(r3.admitted_inflight == 0 && r3.post_skipped_after_stop == 1 && r3.incomplete);                    // the late post was skipped and counted
#endif
    }
#ifndef _WIN32
    // ---- concurrent producers: every attempt is accounted for, nothing blocks, ring order stays valid ----
    {   static Observer o; fresh(o); o.enable(true); Fake f; f.set_list({0xE1});
        std::vector<std::thread> th;
        for (int t = 0; t < 4; ++t) th.emplace_back([&] { for (int i = 0; i < 2000; ++i) o.observe(f.cols.data(), Phase::kPre); });
        for (auto& t : th) t.join();
        Counters k = o.counters();
        CHECK(k.attempts == 8000 && k.enqueued + k.dropped_full + k.dropped_contended + k.skipped_filter + k.capture_failed == 8000);
        CHECK(k.enqueued <= Observer::kCapacity && o.pending() == k.enqueued);
        Collected col; FlushStop why{}; size_t total = 0;
        while (o.pending()) { const size_t n = o.try_flush(collect_sink, &col, 64, 100, &why); if (!n) break; total += n; }
        CHECK(total == k.enqueued && col.lines.size() == k.enqueued);
    }
#endif
    std::printf(failures ? "collision observer: %d FAILURE(S)\n" : "collision observer: all checks passed\n", failures);
    return failures ? 1 : 0;
}
