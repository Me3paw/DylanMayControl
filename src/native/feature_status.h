// Structured, bounded feature-status / event logging for the combat bindings. NO fake claims: a feature is Active only when every
// prerequisite is proven present; otherwise it is Unbound/Disabled with the exact missing list. Lines are written through an
// injected sink (root's non-blocking collector); formatting uses a stack buffer, never allocates, never blocks.
// Line format (one line, key=value):  CRMOD feat=<finisher|enemy_step> ev=<status|attempt|reject|applied|exception> world=0x.. actor=0x..
//                                     id=0x.. rva=0x.. state=<unbound|bound|disabled|active> detail=<text>
// Event lines are rate limited per (feature, event): the first 16, then every 256th, with a `suppressed=N` counter.
#pragma once
#include "diagnostics.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace feature_status {
using LogFn = void (*)(const char* line, size_t len);
enum class Feature : uint8_t { Finisher = 0, EnemyStep = 1, Count = 2 };
enum class Event : uint8_t { Status = 0, Attempt, Reject, Applied, Exception, Count };
enum class State : uint8_t { Unbound = 0, Bound, Disabled, Active };

inline const char* feature_name(Feature f) { return f == Feature::Finisher ? "finisher" : "enemy_step"; }
inline const char* event_name(Event e) {
    switch (e) { case Event::Status: return "status"; case Event::Attempt: return "attempt"; case Event::Reject: return "reject";
                 case Event::Applied: return "applied"; default: return "exception"; }
}
inline const char* state_name(State s) {
    switch (s) { case State::Unbound: return "unbound"; case State::Bound: return "bound"; case State::Disabled: return "disabled"; default: return "active"; }
}

class Registry {
public:
    void set_sink(LogFn fn) { if constexpr(diagnostics::enabled)sink_.store(fn,std::memory_order_release); }
    // `missing` is a short comma-separated list of what is absent (empty when Active).
    void set_state(Feature f, State s, const char* missing) {
        const size_t i = static_cast<size_t>(f);
        state_[i].store(static_cast<uint8_t>(s), std::memory_order_release);
        if constexpr(!diagnostics::enabled)return;
        std::snprintf(missing_[i], sizeof missing_[i], "%.255s", missing ? missing : "");
        emit(f, Event::Status, 0, 0, 0, 0, s, missing ? missing : "");
    }
    State state(Feature f) const { return static_cast<State>(state_[static_cast<size_t>(f)].load(std::memory_order_acquire)); }
    bool active(Feature f) const { return state(f) == State::Active; }
    // Re-emits the status line (call from a slow tick; cheap).
    void emit_status(Feature f) { emit(f, Event::Status, 0, 0, 0, 0, state(f), missing_[static_cast<size_t>(f)]); }
    // Rate-limited event line. Never blocks; drops when the sink is absent.
    void event(Feature f, Event e, uint64_t world, uint64_t actor, uint64_t id, uint32_t hook_rva, const char* detail) {
        if constexpr(!diagnostics::enabled)return;
        std::atomic<uint64_t>& c = count_[static_cast<size_t>(f)][static_cast<size_t>(e)];
        const uint64_t n = c.fetch_add(1, std::memory_order_relaxed);
        if (n >= 16 && (n & 255) != 0) return;                          // suppressed (the counter still counts)
        emit(f, e, world, actor, id, hook_rva, state(f), detail, n >= 16 ? n - 15 : 0);
    }
    uint64_t count(Feature f, Event e) const { return count_[static_cast<size_t>(f)][static_cast<size_t>(e)].load(std::memory_order_relaxed); }

private:
    void emit(Feature f, Event e, uint64_t world, uint64_t actor, uint64_t id, uint32_t rva, State s, const char* detail, uint64_t suppressed = 0) {
        if constexpr(!diagnostics::enabled)return;
        const LogFn fn = sink_.load(std::memory_order_acquire);
        if (!fn) return;
        char line[320];
        int n = std::snprintf(line, sizeof line, "CRMOD feat=%s ev=%s world=0x%llx actor=0x%llx id=0x%llx rva=0x%x state=%s suppressed=%llu detail=%.160s\n",
                              feature_name(f), event_name(e), static_cast<unsigned long long>(world), static_cast<unsigned long long>(actor),
                              static_cast<unsigned long long>(id), rva, state_name(s), static_cast<unsigned long long>(suppressed), detail ? detail : "");
        if (n > 0) fn(line, static_cast<size_t>(n < static_cast<int>(sizeof line) ? n : static_cast<int>(sizeof line) - 1));
    }
    std::atomic<LogFn> sink_{nullptr};
    std::atomic<uint8_t> state_[static_cast<size_t>(Feature::Count)] = {};
    char missing_[static_cast<size_t>(Feature::Count)][256] = {};
    std::atomic<uint64_t> count_[static_cast<size_t>(Feature::Count)][static_cast<size_t>(Event::Count)] = {};
};
inline Registry& registry() { static Registry r; return r; }
}  // namespace feature_status
