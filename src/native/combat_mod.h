// Combat algorithms: target-relative directional finishers and the actual-contact enemy step.
// PURE state machines + the callback surface only. Every engine value arrives BY VALUE in a snapshot filled
// by readers that must be proven first (evidence/re/combat-contracts); nothing here dereferences a native
// pointer, installs a hook, or calls the game. With no readers installed every entry point is fail-closed
// ("not handled") so the shared chooser detour forwards to the original result.
// Threading: callbacks may run on any engine thread (thread identity unproven); state is guarded by a
// tiny spin lock and gates are atomics. Snapshots/pointers never escape the callback.
#pragma once
#include "diagnostics.h"
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace combat_mod {

// ---- tunables: NOT proven game constants; defaults are starting values to be calibrated in manual tests ----
struct Tuning {
    float forward_dot_min = 0.5f;        // alignment with the direction to the locked target counted as Forward
    float back_dot_max = -0.5f;          // ... counted as Back (away from the target)
    float stick_deadzone = 0.35f;        // movement magnitude below this is no direction
    uint32_t sequence_window_ms = 450;   // Back -> Forward -> attack must complete inside this window
    uint32_t hold_grace_ms = 120;        // a direction still counts this long after it was released
};

enum class Dir : uint8_t { None, Forward, Back };
enum class Combo : uint8_t { None, Forward, Back, BackForward };   // what the player did before the attack press
enum class Attack : uint8_t { None, Primary, Secondary };
enum class Finisher : uint8_t { None, UppercutSurge, SplitPivot, SplitOrbit, ConcussiveBeatdown, WideEdge, SpinEdge };

// Human-approved mapping (chunks/state.json finisher_mapping); Forward = toward the locked enemy, Back = away.
inline Finisher select_finisher(Combo combo, Attack attack) {
    if (attack == Attack::Primary) {
        if (combo == Combo::Back) return Finisher::UppercutSurge;
        if (combo == Combo::Forward) return Finisher::SplitPivot;
        if (combo == Combo::BackForward) return Finisher::SplitOrbit;
    } else if (attack == Attack::Secondary) {
        if (combo == Combo::Forward) return Finisher::ConcussiveBeatdown;
        if (combo == Combo::Back) return Finisher::WideEdge;
        if (combo == Combo::BackForward) return Finisher::SpinEdge;
    }
    return Finisher::None;
}

struct DirectionSample {      // computed by a proven reader from lock-on target + wanted movement
    uint32_t now_ms;
    bool locked;              // lock-on active
    float stick_magnitude;    // 0..1
    float target_alignment;   // dot(world movement direction, direction to locked target), -1..1
    uint64_t target_session;   // opaque id of the current lock target; any change starts a new gesture history
};

inline Dir classify(const DirectionSample& s, const Tuning& t) {
    if (!s.locked || s.stick_magnitude < t.stick_deadzone) return Dir::None;
    if (s.target_alignment >= t.forward_dot_min) return Dir::Forward;
    if (s.target_alignment <= t.back_dot_max) return Dir::Back;
    return Dir::None;
}

// Per-player tracker: feed one sample per update, ask resolve() at the attack-press edge.
class DirectionTracker {
public:
    explicit DirectionTracker(const Tuning& t = Tuning{}) : tuning_(t) {}
    void feed(const DirectionSample& s) {
        // A gesture never carries across unlock/relock or a target-session change (stale history is cleared).
        if (!s.locked) { reset(); return; }
        if (!locked_ || s.target_session != session_) { reset(); locked_ = true; session_ = s.target_session; }
        const Dir d = classify(s, tuning_);
        if (d != current_) {
            if (current_ == Dir::Back) back_end_ = s.now_ms;
            if (current_ == Dir::Forward) forward_end_ = s.now_ms;
            if (d == Dir::Back) { back_start_ = s.now_ms; have_back_ = true; }
            if (d == Dir::Forward) { forward_start_ = s.now_ms; have_forward_ = true; }
            current_ = d;
        }
        last_ms_ = s.now_ms;
    }
    // Locked required. BackForward = Back started, then Forward started after it, both inside the window, and
    // Forward is current (or ended within the grace). Otherwise the current (or just-released) direction.
    Combo resolve(uint32_t now_ms) const {
        if (!locked_) return Combo::None;
        const bool forward_now = current_ == Dir::Forward ||
            (have_forward_ && current_ != Dir::Forward && now_ms - forward_end_ <= tuning_.hold_grace_ms && forward_end_ >= forward_start_);
        if (have_back_ && have_forward_ && forward_start_ > back_start_ && forward_now &&
            now_ms - back_start_ <= tuning_.sequence_window_ms) return Combo::BackForward;
        if (current_ == Dir::Forward) return Combo::Forward;
        if (current_ == Dir::Back) return Combo::Back;
        if (have_forward_ && now_ms - forward_end_ <= tuning_.hold_grace_ms && forward_end_ >= forward_start_) return Combo::Forward;
        if (have_back_ && now_ms - back_end_ <= tuning_.hold_grace_ms && back_end_ >= back_start_) return Combo::Back;
        return Combo::None;
    }
    void reset() { *this = DirectionTracker(tuning_); }
private:
    Tuning tuning_;
    Dir current_ = Dir::None;
    uint64_t session_ = 0;
    bool locked_ = false, have_back_ = false, have_forward_ = false;
    uint32_t back_start_ = 0, back_end_ = 0, forward_start_ = 0, forward_end_ = 0, last_ms_ = 0;
};

// ---- enemy step: the ONLY new cancel rule ----
struct StepInput {
    bool airborne;            // proven airborne state
    bool jump_down;           // raw jump button level this update
    bool actual_contact;      // REAL player-vs-enemy contact (not proximity) at this update
    uint64_t contact_actor;   // opaque id of the enemy touched (0 if none)
};
enum class StepAction : uint8_t { None, Step };
struct StepDecision {
    StepAction action;
    uint64_t actor;           // enemy that was stepped on; the same enemy may repeat on every fresh press
};

class StepTracker {
public:
    // Fresh press only: a held button never repeats; ground presses and non-contact presses are ignored.
    // observe() always tracks the button level (also while the feature is disabled) and never acts.
    // After baseline_unknown() (feature enabled / state lost) a press only counts once a release was seen,
    // so a Jump that was already held when the feature turned on can never trigger a Step.
    void observe(bool jump_down) {
        if (!jump_down) baseline_ = true;
        prev_down_ = jump_down;
    }
    StepDecision update(const StepInput& in) {
        const bool edge = in.jump_down && !prev_down_ && baseline_;
        observe(in.jump_down);
        if (edge && in.airborne && in.actual_contact && in.contact_actor != 0) return {StepAction::Step, in.contact_actor};
        return {StepAction::None, 0};
    }
    void baseline_unknown() { baseline_ = false; }
    void reset() { prev_down_ = false; baseline_ = false; }
private:
    bool prev_down_ = false;
    bool baseline_ = false;   // a release has been observed since (re)arming
};

// ---- row readers (offset tables, evidence cb2/cb1): pure functions on COPIED row bytes; candidates until runtime ----
// Callers must pass bytes copied inside the hook call (never a retained native pointer) and at least `kMin*` bytes.
namespace rows {
constexpr size_t kMinCombo = 0x48, kMinLock = 0xf0, kMinMovement = 0x80, kMinAim = 0x40;
constexpr size_t kComboCurrentNode = 0x20, kComboInputFlags = 0x28;           // u32 (0xffffffff none); 4 bytes kinds 0..3
constexpr size_t kLockLocked = 0x0, kLockTargetPos = 0x90, kLockPosValid = 0xa0;   // byte; vec4 f32; byte
constexpr size_t kMoveWorldDir = 0x00, kMoveMagnitude = 0x24;                  // vec4 f32; f32
constexpr size_t kAimPos = 0x0;                                                // vec4 f32

inline uint32_t combo_current_node(const uint8_t* c) { uint32_t v; std::memcpy(&v, c + kComboCurrentNode, 4); return v; }
inline bool combo_input_kind(const uint8_t* c, unsigned kind) { return kind < 4 && c[kComboInputFlags + kind] != 0; }
// Candidate mapping (UNVERIFIED which physical buttons): Primary = kind 0 (+0x28), Secondary = kind 2 (+0x2a).
inline Attack attack_from_combo(const uint8_t* c) {
    if (combo_input_kind(c, 0)) return Attack::Primary;
    if (combo_input_kind(c, 2)) return Attack::Secondary;
    return Attack::None;
}
inline void read_vec3(const uint8_t* p, float* v) { std::memcpy(v, p, 3 * sizeof(float)); }
// Scale by the largest component (no overflow/underflow when squaring), then normalize; false when non-finite or
// (near) zero length.
inline bool unit3(float* v) {
    const float m = std::fmax(std::fmax(std::fabs(v[0]), std::fabs(v[1])), std::fabs(v[2]));
    if (!std::isfinite(m) || !(m > 1e-12f)) return false;
    for (int i = 0; i < 3; ++i) v[i] /= m;
    const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!std::isfinite(n) || !(n > 0.0f)) return false;
    for (int i = 0; i < 3; ++i) v[i] /= n;
    return true;
}
// Direction sample from lock-status, movement and aim rows. `up_axis` (0/1/2) is projected out so alignment is
// horizontal; the engine up axis is UNVERIFIED (default 1). No sample (false) when the lock is not active, the
// target position/handle is invalid, any input is non-finite, the magnitude is negative, or a POSITIVE magnitude
// has a degenerate movement vector or target direction (inconsistent row). A valid NEUTRAL movement (magnitude 0)
// is a real sample (stick_magnitude 0, alignment 0): the tracker must see it so release grace keeps working.
inline bool direction_sample(const uint8_t* lock, const uint8_t* move, const uint8_t* aim, uint64_t target_handle,
                             uint32_t now_ms, DirectionSample* out, int up_axis = 1) {
    if (up_axis < 0 || up_axis > 2 || lock[kLockLocked] == 0 || lock[kLockPosValid] == 0 || target_handle == 0) return false;
    float t[3], p[3], w[3], mag;
    read_vec3(lock + kLockTargetPos, t); read_vec3(aim + kAimPos, p); read_vec3(move + kMoveWorldDir, w);
    std::memcpy(&mag, move + kMoveMagnitude, 4);
    for (int i = 0; i < 3; ++i) if (!std::isfinite(t[i]) || !std::isfinite(p[i]) || !std::isfinite(w[i])) return false;
    if (!std::isfinite(mag) || mag < 0.0f) return false;
    if (mag == 0.0f) { *out = {now_ms, true, 0.0f, 0.0f, target_handle}; return true; }
    float d[3] = {t[0] - p[0], t[1] - p[1], t[2] - p[2]};
    d[up_axis] = 0.0f; w[up_axis] = 0.0f;
    if (!unit3(d) || !unit3(w)) return false;
    float dot = d[0] * w[0] + d[1] * w[1] + d[2] * w[2];
    if (!std::isfinite(dot)) return false;
    if (dot > 1.0f) dot = 1.0f;
    if (dot < -1.0f) dot = -1.0f;
    *out = {now_ms, true, mag, dot, target_handle};
    return true;
}
} // namespace rows

// ---- proven-by-code native reset WRITE SETS (offset tables only; evidence cb7/cb8/cb9) ----
// These mirror what the native per-update code writes when the player is GROUNDED. Using them while airborne is
// UNPROVEN (needs runtime validation) and nothing in this module calls them: the actuator layer installs them
// only after a runtime packet confirms the next fresh action is legal. No landing event is involved.
namespace write_sets {
constexpr size_t kJumpRow = 0x28;          // jump::Jump row stride
constexpr size_t kJumpPhase = 0x14, kJumpType = 0x15, kJumpTimer = 0x1c, kJumpIndex = 0x20, kJumpIndexHi = 0x21, kJumpFlag22 = 0x22;
// User expanded the step to restore all landing-reset air actions. Native
// melee_attack142367e90 resets this used counter when grounded (cb8/106).
constexpr size_t kAirAttacksRow=0x10, kAirAttacksUsed=8;
constexpr size_t kDodgeRow = 0x40;         // dodge::DodgeData row stride
constexpr size_t kDodgeCooldown = 0x28, kDodgeUsed = 0x30, kDodgeAirUsed = 0x34;

inline void jump_counter_reset(uint8_t* row) {          // native: FUN_1423f9d00 branch (phase 0 && grounded)
    row[kJumpIndex] = 0xff; row[kJumpIndexHi] = 0; row[kJumpType] = 3; row[kJumpFlag22] = 0;
    const uint32_t zero = 0; std::memcpy(row + kJumpTimer, &zero, sizeof zero);
}
inline void air_dodge_reset(uint8_t* row, bool full_refill) {   // native: FUN_142655db0 grounded; +0x30 on cooldown elapse
    const uint32_t zero = 0; const float zerof = 0.0f;
    std::memcpy(row + kDodgeAirUsed, &zero, sizeof zero);
    if (full_refill) { std::memcpy(row + kDodgeUsed, &zero, sizeof zero); std::memcpy(row + kDodgeCooldown, &zerof, sizeof zerof); }
}
inline void air_attack_reset(uint8_t* row) {
    const uint32_t zero=0; std::memcpy(row+kAirAttacksUsed,&zero,sizeof zero);
}
} // namespace write_sets

// ---- enemy-step actions: injected, all-or-nothing preflight, fixed order ----
struct StepContext {          // by-value snapshot; native pointers valid only inside the call that built it
    uint64_t actor;           // contacted entity handle from the proven contact reader
    void* player;             // opaque player row bundle for the actuators (never stored)
};
// Two-phase contract (review: pointer presence does not prove success; cancel must never run without the jump):
//  1. preflight: ownership + three NON-MUTATING can_* predicates must all pass; any failure => nothing mutates;
//  2. commit in the order reset -> jump -> cancel (the most disruptive, last): a mutation that fails after an
//     earlier mutation is a PARTIAL mutation: it is logged with its stage and the enemy-step feature disables itself.
struct StepActuators {        // each member is installed only after its native contract is proven; nullptr => blocked
    bool (*owns_player_and_frame)(const StepContext&) = nullptr;  // same player, world and update frame as the readers
    bool (*can_reset)(const StepContext&) = nullptr;              // non-mutating: reset target rows reachable and valid
    bool (*can_start_jump)(const StepContext&) = nullptr;         // non-mutating: native can-jump accepts a jump now
    bool (*can_cancel)(const StepContext&) = nullptr;             // non-mutating: native cancel route available now
    bool (*reset_air_resources)(const StepContext&) = nullptr;    // jump counter / air dash / air-combo resources, no landing event
    bool (*start_jump)(const StepContext&) = nullptr;             // native jump start
    bool (*cancel_attack)(const StepContext&) = nullptr;          // native cancel route (only new cancel rule)
};
enum class StepStage : uint8_t { Done, Ownership, Preflight, Reset, Jump, Cancel, MissingActuator };
struct StepResult { StepAction action; StepStage failed; bool mutated; uint64_t actor; };

// ---- chooser callback surface (shared detour calls this; no second hook in this file) ----
struct ChooserArgs {          // by-value snapshot of the 10 args of FUN_14235e1e0 (evidence cb1); valid only inside the call
    void* node; uint8_t phase_is_2; void* combo_data; void* conditions; void* loadout;
    void* unlock_data; void* state_set; void* combo_cfg; void* graph; void* anim_out;
};
struct Readers {              // each member is installed only after its contract is proven; nullptr => fail-closed
    bool (*direction_sample)(const ChooserArgs&, DirectionSample*) = nullptr;
    bool (*attack_press)(const ChooserArgs&, Attack*) = nullptr;                       // fresh press edge this update
    bool (*finisher_node)(const ChooserArgs&, Finisher, uint32_t* node_index) = nullptr;
    // Native legality of the node with ONLY the combo-count requirement bypassed (dee0 + selected-unlock gate etc.).
    bool (*legal_without_step_requirement)(const ChooserArgs&, uint32_t node_index) = nullptr;
};

struct Record { uint32_t now_ms; uint8_t kind, combo, attack, finisher; uint32_t node; uint64_t actor; };
class Module {
public:
    // Replacing/clearing readers or toggling a feature invalidates the matching history.
    void set_readers(const Readers& r) { lock(); readers_ = r; tracker_.reset(); step_tracker_.reset(); unlock(); }
    void enable_finishers(bool on) { lock(); tracker_.reset(); finishers_.store(on); unlock(); }
    void enable_enemy_step(bool on) { lock(); step_tracker_.baseline_unknown(); step_.store(on); unlock(); }
    bool finishers_enabled() const { return finishers_.load(); }
    bool enemy_step_enabled() const { return step_.load(); }

    // Returns true and sets *chosen only when a finisher applies and every required reader proved it legal.
    bool on_chooser(const ChooserArgs& a, uint32_t now_ms, uint32_t* chosen) {
        if (!finishers_.load()) return false;
        lock();
        const Readers r = readers_;
        bool handled = false;
        if (r.direction_sample && r.attack_press && r.finisher_node && r.legal_without_step_requirement) {
            DirectionSample s{};
            Attack attack = Attack::None;
            if (!r.direction_sample(a, &s)) {
                tracker_.reset();   // a failed current reader leaves NO usable direction: invalidate and stay unhandled
                unlock();
                return false;
            }
            tracker_.feed(s);
            if (r.attack_press(a, &attack) && attack != Attack::None) {
                const Combo combo = tracker_.resolve(now_ms);
                const Finisher f = select_finisher(combo, attack);
                uint32_t node = 0xffffffffu;
                if (f != Finisher::None && r.finisher_node(a, f, &node) && node != 0xffffffffu &&
                    r.legal_without_step_requirement(a, node)) {
                    *chosen = node;
                    handled = true;
                    push({now_ms, 1, static_cast<uint8_t>(combo), static_cast<uint8_t>(attack), static_cast<uint8_t>(f), node, 0});
                } else {
                    push({now_ms, 2, static_cast<uint8_t>(combo), static_cast<uint8_t>(attack), static_cast<uint8_t>(f), node, 0});
                }
            }
        }
        unlock();
        return handled;
    }
    // Enemy step decision for one update; the caller performs the (proven) native jump/reset/cancel only on Step.
    StepDecision on_step_update(const StepInput& in, uint32_t now_ms) {
        lock();
        if (!step_.load()) {        // disabled: keep observing the button level, never act
            step_tracker_.observe(in.jump_down);
            unlock();
            return {StepAction::None, 0};
        }
        const StepDecision d = step_tracker_.update(in);
        if (d.action == StepAction::Step) push({now_ms, 3, 0, 0, 0, 0, d.actor});
        unlock();
        return d;
    }
    void set_step_actuators(const StepActuators& a) { lock(); actuators_ = a; step_tracker_.reset(); unlock(); }
    // Decision + actions for one update. Nothing runs unless every actuator is installed (preflight); order is
    // ownership -> cancel -> reset -> start jump, and a failing stage stops the sequence (reported, not hidden).
    StepResult step(const StepInput& in, void* player, uint32_t now_ms) {
        const StepDecision d = on_step_update(in, now_ms);
        if (d.action != StepAction::Step) return {StepAction::None, StepStage::Done, false, 0};
        lock();
        const StepActuators a = actuators_;
        unlock();
        const StepContext ctx{d.actor, player};
        StepStage failed = StepStage::Done;
        bool mutated = false;
        if (!a.owns_player_and_frame || !a.can_reset || !a.can_start_jump || !a.can_cancel ||
            !a.reset_air_resources || !a.start_jump || !a.cancel_attack) failed = StepStage::MissingActuator;
        else if (!a.owns_player_and_frame(ctx)) failed = StepStage::Ownership;
        else if (!a.can_reset(ctx) || !a.can_start_jump(ctx) || !a.can_cancel(ctx)) failed = StepStage::Preflight;
        else {
            // Every mutator may have partly written before it returns false (grouped writes): from the first mutator
            // call on, the outcome is "applied or UNKNOWN", never "unchanged". A failure anywhere is a partial/unknown
            // mutation (logged, feature self-disables); preflight is not atomicity.
            mutated = true;
            if (!a.reset_air_resources(ctx)) failed = StepStage::Reset;
            else if (!a.start_jump(ctx)) failed = StepStage::Jump;
            else if (!a.cancel_attack(ctx)) failed = StepStage::Cancel;
        }
        lock();
        const bool partial = mutated && failed != StepStage::Done;
        push({now_ms, static_cast<uint8_t>(failed == StepStage::Done ? 4 : (partial ? 6 : 5)), static_cast<uint8_t>(failed),
              static_cast<uint8_t>(mutated), 0, 0, d.actor});
        if (partial) { step_.store(false); step_tracker_.reset(); }   // partial mutation: the feature turns itself off
        unlock();
        return {StepAction::Step, failed, mutated, d.actor};
    }
    // Plain-data records only; call from the proxy flush path. Returns rows written.
    unsigned flush([[maybe_unused]] FILE* log) {
#if CRMOD_DIAGNOSTICS
        Record rows[64];
        lock();
        unsigned n = count_;
        std::memcpy(rows, ring_, n * sizeof(Record));
        count_ = 0;
        const unsigned dropped = dropped_;
        dropped_ = 0;
        unlock();
        for (unsigned i = 0; i < n; ++i)
            std::fprintf(log, "{\"event\":\"combat_mod\",\"kind\":%u,\"now_ms\":%u,\"combo\":%u,\"attack\":%u,\"finisher\":%u,\"node\":%u,\"actor\":%llu}\n",
                         rows[i].kind, rows[i].now_ms, rows[i].combo, rows[i].attack, rows[i].finisher, rows[i].node,
                         static_cast<unsigned long long>(rows[i].actor));
        if (dropped) std::fprintf(log, "{\"event\":\"combat_mod_dropped\",\"count\":%u}\n", dropped);
        if (n || dropped) std::fflush(log);
        return n;
#else
        return 0;
#endif
    }
private:
    void lock() { while (spin_.test_and_set(std::memory_order_acquire)) {} }
    void unlock() { spin_.clear(std::memory_order_release); }
    void push([[maybe_unused]] const Record& r) { CRMOD_DIAGNOSTIC(if (count_ == 64) ++dropped_; else ring_[count_++] = r); }
    std::atomic_flag spin_ = ATOMIC_FLAG_INIT;
    std::atomic<bool> finishers_{false}, step_{false};   // default OFF
    Readers readers_{};
    StepActuators actuators_{};
    DirectionTracker tracker_{};
    StepTracker step_tracker_{};
#if CRMOD_DIAGNOSTICS
    Record ring_[64]{};
    unsigned count_ = 0, dropped_ = 0;
#endif
};
} // namespace combat_mod
