// NON-BLOCKING four-argument observer wrapper for FUN_14277f170(cols, ctx, p3, p4) (evidence cb26/cb29/cb30): the engine hot-path
// side of collision_probe.h. It NEVER calls collision_probe::Probe::observe (that class spins on a lock) and NEVER blocks,
// allocates or performs I/O on the engine thread:
//   - producers (pre/post capture) publish into a bounded lock-free MPMC ring (Vyukov sequence slots, bounded CAS retries);
//     a full ring or lost CAS DROPS the new record and counts it (nothing already queued is overwritten);
//   - a single consumer drains with try_flush(): an atomic-flag try-lock (busy => returns at once), at most `max_records`,
//     at most 100 ms and a bounded attempt counter, and a record is removed ONLY after the sink accepted it
//     (try-publish: a sink failure retains the record and everything behind it, in order);
//   - OPT-IN, default OFF: while disabled the wrapper only forwards the four arguments (and nothing else happens).
// LIFETIME CONTRACT (honest): the hook and this observer are meant to be PINNED UNTIL PROCESS END. There is NO unhook / free /
// unload safety claim. stop() only SEALS recording and does a BOUNDED wait for in-flight capture/publish sections (never for the
// original); the StopReport says whether the evidence is incomplete (queued records, skipped post captures, or wrapper calls
// still admitted inside the original).
// Exact forward: the original is called with (a, b, c, d) unchanged in order. GetLastError handling: the incoming error is saved
// BEFORE the first thread-local access, restored before the original (also on the nested path), and the error returned by the
// original is restored LAST. A thread-local depth guard makes nested calls pass straight through. configure() is IMMUTABLE after
// the first successful enable (an old run() still reads the error/clock functions after the original returns).
// No Player authority and no current-contact verdict: it only logs.
#pragma once
#include "collision_probe.h"

namespace collision_observer {
using collision_probe::Config;
using collision_probe::Phase;
using collision_probe::Record;
using combat_native::CopyFn;

using GetErrFn = uint32_t (*)();
using SetErrFn = void (*)(uint32_t);
using NowMsFn = uint32_t (*)();                         // monotonic milliseconds
using TidFn = uint32_t (*)();
using DepthFn = uint32_t& (*)();                        // per-thread re-entrancy counter accessor (replaceable for tests)
using Original4 = void (*)(void*, void*, void*, void*);
using Sink = bool (*)(const char* line, size_t len, void* user);   // true = accepted (record may be removed); false = busy (retain)

constexpr uint32_t kMaxFlushMs = 100;                    // hard cap on one try_flush call
constexpr uint32_t kMaxStopMs = 100;                     // hard cap on the bounded capture wait of stop()
constexpr uint32_t kMaxFlushAttempts = 64;               // hard cap on loop iterations of one try_flush call
constexpr unsigned kCasRetries = 4;                      // bounded producer retries before a contended drop

enum class FlushStop : uint8_t { Empty, MaxRecords, Deadline, Attempts, SinkBusy, Busy, NoSink };

struct Counters {
    uint64_t attempts, enqueued, dropped_full, dropped_contended, skipped_filter, capture_failed, disabled_passthrough,
             nested_passthrough, flush_calls, flush_busy, flush_sink_failed, flushed_records, post_skipped_after_stop;
};

struct StopReport {
    bool sealed;                   // recording is off
    bool captures_idle;            // no capture/publish section was in flight when the bounded wait ended
    uint32_t captures_inflight;
    uint32_t admitted_inflight;    // enabled wrapper calls currently admitted (pre captured / original running): NEVER waited for
    uint64_t pending;              // queued, undelivered records
    uint64_t post_skipped_after_stop;
    bool incomplete;               // !captures_idle || pending > 0 || post_skipped_after_stop > 0 || admitted_inflight > 0
};

class Observer {
public:
    static constexpr size_t kCapacity = 256;             // power of two

    Observer() { for (size_t i = 0; i < kCapacity; ++i) slots_[i].seq.store(i, std::memory_order_relaxed); }

    // Not hot. Returns false (and changes nothing) after the first successful enable: the setup is then immutable.
    bool configure(CopyFn cp, TidFn tid, NowMsFn now, GetErrFn get_err, SetErrFn set_err, const Config& cfg, DepthFn depth = nullptr) {
        if (frozen_.load(std::memory_order_acquire)) return false;
        cp_ = cp; tid_ = tid; now_ = now; get_err_ = get_err; set_err_ = set_err; depth_ = depth ? depth : &default_depth;
        log_empty_.store(cfg.log_empty, std::memory_order_relaxed);
        watch_owner_.store(cfg.watch_owner, std::memory_order_relaxed);
        return true;
    }
    // Never enables without the guarded copier and the LastError pair. The first successful enable freezes configure().
    void enable(bool on) {
        if (on) {
            if (!(cp_ && get_err_ && set_err_)) return;
            if (!depth_) depth_ = &default_depth;
            frozen_.store(true, std::memory_order_release);
        }
        enabled_.store(on, std::memory_order_seq_cst);
    }
    bool enabled() const { return enabled_.load(std::memory_order_acquire); }
    bool frozen() const { return frozen_.load(std::memory_order_acquire); }

    // The detour body. Exact forward of the four arguments; observation only when enabled and not nested.
    void run(Original4 orig, void* a, void* b, void* c, void* d) {
        if (!orig) return;
        if (!enabled_.load(std::memory_order_acquire)) { bump(c_disabled_); orig(a, b, c, d); return; }
        const uint32_t incoming = get_err_();                               // BEFORE the first thread-local access
        uint32_t& depth = depth_();
        if (depth != 0) { set_err_(incoming); bump(c_nested_); orig(a, b, c, d); return; }   // restore BEFORE the original on the nested path
        depth = 1;
        admitted_.fetch_add(1, std::memory_order_seq_cst);
        observe(a, Phase::kPre);
        set_err_(incoming);
        orig(a, b, c, d);
        const uint32_t after = get_err_();
        observe(a, Phase::kPost);
        depth = 0;                                                          // thread-local reset BEFORE the final restore
        set_err_(after);                                                    // LAST statement that can touch the error
        admitted_.fetch_sub(1, std::memory_order_seq_cst);                  // atomics only: they never touch the error
    }

    // Hot path capture + non-blocking publish. Public for tests; the detour uses run().
    void observe(const void* cols, Phase phase) {
        captures_.fetch_add(1, std::memory_order_seq_cst);
        if (!enabled_.load(std::memory_order_seq_cst)) {                    // Dekker pairing with stop(): disabled => nothing may be captured/published
            captures_.fetch_sub(1, std::memory_order_seq_cst);
            if (phase == Phase::kPost) bump(c_post_skipped_);
            return;
        }
        bump(c_attempts_);
        Record r;
        const uint32_t seq = static_cast<uint32_t>(seq_.fetch_add(1, std::memory_order_relaxed) + 1);
        if (!collision_probe::capture(cp_, cols, phase, seq, tid_ ? tid_() : 0, now_ ? now_() : 0, &r)) {
            bump(c_capture_failed_);
        } else {
            const uint64_t watch = watch_owner_.load(std::memory_order_relaxed);
            const bool interesting = r.count != 0 || r.corrupt || log_empty_.load(std::memory_order_relaxed) || (watch != 0 && r.owner == watch);
            if (!interesting) bump(c_skipped_); else publish(r);
        }
        captures_.fetch_sub(1, std::memory_order_seq_cst);
    }

    // Seals recording and waits (<= min(max_ms, 100 ms), only for capture/publish sections, NEVER for the original), then reports.
    StopReport stop(uint32_t max_ms) {
        enabled_.store(false, std::memory_order_seq_cst);
        const uint32_t limit = max_ms < kMaxStopMs ? max_ms : kMaxStopMs;
        const uint32_t t0 = now_ ? now_() : 0;
        bool idle = false;
        for (;;) {                                                           // `idle` = the counter was observed at 0 AFTER the seal
            if (captures_.load(std::memory_order_seq_cst) == 0) { idle = true; break; }
            if (!now_ || static_cast<uint32_t>(now_() - t0) >= limit) break;
        }
        // From the moment 0 was observed after the seal no later observe() can capture or publish: it increments, sees `disabled`
        // and leaves. A LATER snapshot of the counter may be transiently non-zero because of exactly such leaving calls, so the
        // verdict comes from the observation above, not from a re-read.
        StopReport r{};
        r.sealed = !enabled_.load(std::memory_order_seq_cst);
        r.captures_idle = idle;
        r.captures_inflight = idle ? 0u : captures_.load(std::memory_order_seq_cst);
        r.admitted_inflight = admitted_.load(std::memory_order_seq_cst);
        r.pending = pending();
        r.post_skipped_after_stop = c_post_skipped_.load();
        r.incomplete = !r.captures_idle || r.pending > 0 || r.post_skipped_after_stop > 0 || r.admitted_inflight > 0;
        return r;
    }

    // Single consumer. Returns the number of records delivered; `stop` tells why it returned.
    size_t try_flush(Sink sink, void* user, size_t max_records, uint32_t budget_ms, FlushStop* stop_reason) {
        bump(c_flush_calls_);
        FlushStop why = FlushStop::Empty;
        size_t delivered = 0;
        if (!sink) { if (stop_reason) *stop_reason = FlushStop::NoSink; return 0; }
        if (flushing_.test_and_set(std::memory_order_acquire)) { bump(c_flush_busy_); if (stop_reason) *stop_reason = FlushStop::Busy; return 0; }
        const uint32_t budget = budget_ms < kMaxFlushMs ? budget_ms : kMaxFlushMs;
        const uint32_t t0 = now_ ? now_() : 0;
        for (uint32_t attempt = 0;; ++attempt) {
            if (attempt >= kMaxFlushAttempts) { why = FlushStop::Attempts; break; }
            if (delivered >= max_records) { why = FlushStop::MaxRecords; break; }
            if (now_ && static_cast<uint32_t>(now_() - t0) >= budget && attempt != 0) { why = FlushStop::Deadline; break; }
            const uint64_t pos = head_.load(std::memory_order_relaxed);
            Slot& s = slots_[pos & (kCapacity - 1)];
            if (s.seq.load(std::memory_order_acquire) != pos + 1) { why = FlushStop::Empty; break; }   // nothing published at the head
            const Record copy = s.rec;                                                            // the producer finished before the release store
            const size_t n = collision_probe::format_line(copy, line_, sizeof line_);             // PER-INSTANCE buffer, guarded by this instance's flag
            if (n == 0 || !sink(line_, n, user)) { bump(c_sink_failed_); why = FlushStop::SinkBusy; break; }   // RETAIN: head not advanced
            s.seq.store(pos + kCapacity, std::memory_order_release);                              // slot reusable for the next lap
            head_.store(pos + 1, std::memory_order_relaxed);
            bump(c_flushed_);
            ++delivered;
        }
        flushing_.clear(std::memory_order_release);
        if (stop_reason) *stop_reason = why;
        return delivered;
    }

    size_t pending() const { return static_cast<size_t>(tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire)); }
    Counters counters() const {
        return {c_attempts_.load(), c_enqueued_.load(), c_full_.load(), c_contended_.load(), c_skipped_.load(), c_capture_failed_.load(),
                c_disabled_.load(), c_nested_.load(), c_flush_calls_.load(), c_flush_busy_.load(), c_sink_failed_.load(), c_flushed_.load(),
                c_post_skipped_.load()};
    }

private:
    struct Slot { std::atomic<uint64_t> seq; Record rec; };
    static uint32_t& default_depth() { static thread_local uint32_t d = 0; return d; }
    static void bump(std::atomic<uint64_t>& c) { c.fetch_add(1, std::memory_order_relaxed); }

    void publish(const Record& r) {
        for (unsigned i = 0; i < kCasRetries; ++i) {
            uint64_t pos = tail_.load(std::memory_order_relaxed);
            Slot& s = slots_[pos & (kCapacity - 1)];
            const uint64_t seq = s.seq.load(std::memory_order_acquire);
            const int64_t diff = static_cast<int64_t>(seq) - static_cast<int64_t>(pos);
            if (diff == 0) {
                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    s.rec = r;                                                   // exclusive between the claim and the release store
                    s.seq.store(pos + 1, std::memory_order_release);
                    bump(c_enqueued_);
                    return;
                }
            } else if (diff < 0) {                                               // the slot of the previous lap is not drained: full
                bump(c_full_);
                return;
            }
        }
        bump(c_contended_);
    }

    std::atomic<bool> enabled_{false}, frozen_{false};
    std::atomic<uint64_t> tail_{0}, head_{0}, seq_{0};
    std::atomic<uint32_t> captures_{0}, admitted_{0};
    std::atomic<bool> log_empty_{false};
    std::atomic<uint64_t> watch_owner_{0};
    std::atomic_flag flushing_;
    CopyFn cp_ = nullptr; TidFn tid_ = nullptr; NowMsFn now_ = nullptr; GetErrFn get_err_ = nullptr; SetErrFn set_err_ = nullptr; DepthFn depth_ = nullptr;
    char line_[collision_probe::kLineMax];
    Slot slots_[kCapacity];
    std::atomic<uint64_t> c_attempts_{0}, c_enqueued_{0}, c_full_{0}, c_contended_{0}, c_skipped_{0}, c_capture_failed_{0}, c_disabled_{0},
                          c_nested_{0}, c_flush_calls_{0}, c_flush_busy_{0}, c_sink_failed_{0}, c_flushed_{0}, c_post_skipped_{0};
};

}  // namespace collision_observer
