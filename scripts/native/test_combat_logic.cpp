// Host-side proof of the combat_mod.h state machines (no game, no engine reads).
// build+run: scripts/native/test_combat_logic.sh
#include "../../src/native/combat_mod.h"
#include <array>
#include <limits>

using namespace combat_mod;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static DirectionSample S(uint32_t t, float align, float mag = 1.0f, bool locked = true, uint64_t session = 1) { return {t, locked, mag, align, session}; }
static bool g_fail_dir = false;
static int g_order[16]; static int g_order_n = 0, g_mutations = 0;
static bool g_ok_own = true, g_ok_cancel = true, g_ok_reset = true, g_ok_jump = true;
static bool g_ok_can_reset = true, g_ok_can_jump = true, g_ok_can_cancel = true;
static bool own_a(const StepContext&) { g_order[g_order_n++] = 1; return g_ok_own; }
static bool cancel_a(const StepContext&) { g_order[g_order_n++] = 2; ++g_mutations; return g_ok_cancel; }
static bool reset_a(const StepContext&) { g_order[g_order_n++] = 3; ++g_mutations; return g_ok_reset; }
static bool jump_a(const StepContext&) { g_order[g_order_n++] = 4; ++g_mutations; return g_ok_jump; }
static bool can_reset_a(const StepContext&) { g_order[g_order_n++] = 5; return g_ok_can_reset; }
static bool can_jump_a(const StepContext&) { g_order[g_order_n++] = 6; return g_ok_can_jump; }
static bool can_cancel_a(const StepContext&) { g_order[g_order_n++] = 7; return g_ok_can_cancel; }
static void hold(DirectionTracker& d, uint32_t from, uint32_t to, float align) { for (uint32_t t = from; t < to; t += 16) d.feed(S(t, align)); }

static const ChooserArgs kArgs{};
static Attack g_attack = Attack::None;
static DirectionSample g_sample{};
static bool g_legal = true;
static bool dir_reader(const ChooserArgs&, DirectionSample* s) { if (g_fail_dir) return false; *s = g_sample; return true; }
static bool atk_reader(const ChooserArgs&, Attack* a) { *a = g_attack; return true; }
static bool node_reader(const ChooserArgs&, Finisher f, uint32_t* n) { *n = 100 + static_cast<uint32_t>(f); return true; }
static bool legal_reader(const ChooserArgs&, uint32_t) { return g_legal; }
// Copied-row readers: the buffers stand in for bytes copied inside the hook call (aim stays at the origin).
static std::array<uint8_t, rows::kMinLock> g_row_lock;
static std::array<uint8_t, rows::kMinMovement> g_row_move;
static std::array<uint8_t, rows::kMinAim> g_row_aim;
static std::array<uint8_t, rows::kMinCombo> g_row_combo;
static uint32_t g_row_now = 0;
static bool row_dir_reader(const ChooserArgs&, DirectionSample* s) {
    return rows::direction_sample(g_row_lock.data(), g_row_move.data(), g_row_aim.data(), 77, g_row_now, s);
}
static bool row_atk_reader(const ChooserArgs&, Attack* a) { *a = rows::attack_from_combo(g_row_combo.data()); return true; }

int main() {
    // classification
    Tuning t;
    CHECK(classify(S(0, 1.0f), t) == Dir::Forward);
    CHECK(classify(S(0, -1.0f), t) == Dir::Back);
    CHECK(classify(S(0, 0.0f), t) == Dir::None);
    CHECK(classify(S(0, 1.0f, 0.1f), t) == Dir::None);
    CHECK(classify(S(0, 1.0f, 1.0f, false), t) == Dir::None);

    // mapping table (human choice)
    CHECK(select_finisher(Combo::Back, Attack::Primary) == Finisher::UppercutSurge);
    CHECK(select_finisher(Combo::Forward, Attack::Primary) == Finisher::SplitPivot);
    CHECK(select_finisher(Combo::BackForward, Attack::Primary) == Finisher::SplitOrbit);
    CHECK(select_finisher(Combo::Forward, Attack::Secondary) == Finisher::ConcussiveBeatdown);
    CHECK(select_finisher(Combo::Back, Attack::Secondary) == Finisher::WideEdge);
    CHECK(select_finisher(Combo::BackForward, Attack::Secondary) == Finisher::SpinEdge);
    CHECK(select_finisher(Combo::None, Attack::Primary) == Finisher::None);
    CHECK(select_finisher(Combo::Forward, Attack::None) == Finisher::None);

    // quick Back -> Forward -> attack (not a chord)
    { DirectionTracker d; hold(d, 0, 100, -1.0f); hold(d, 100, 300, 1.0f);
      CHECK(d.resolve(300) == Combo::BackForward); }
    // long-held Forward after an old Back is plain Forward
    { DirectionTracker d; hold(d, 0, 100, -1.0f); hold(d, 100, 1500, 1.0f);
      CHECK(d.resolve(1500) == Combo::Forward); }
    // Back too long ago relative to the window
    { DirectionTracker d; hold(d, 0, 100, -1.0f); hold(d, 100, 700, 1.0f);
      CHECK(d.resolve(700) == Combo::Forward); }
    // plain Back, plain Forward
    { DirectionTracker d; hold(d, 0, 400, -1.0f); CHECK(d.resolve(400) == Combo::Back); }
    { DirectionTracker d; hold(d, 0, 400, 1.0f); CHECK(d.resolve(400) == Combo::Forward); }
    // not locked => none
    { DirectionTracker d; for (uint32_t x = 0; x < 300; x += 16) d.feed(S(x, 1.0f, 1.0f, false)); CHECK(d.resolve(300) == Combo::None); }
    // release grace: Back ended 60 ms ago still counts, 200 ms ago does not
    { DirectionTracker d; hold(d, 0, 100, -1.0f); d.feed(S(100, 0.0f)); CHECK(d.resolve(160) == Combo::Back); CHECK(d.resolve(300) == Combo::None); }
    // Back then Forward released shortly before the press still BackForward
    { DirectionTracker d; hold(d, 0, 100, -1.0f); hold(d, 100, 200, 1.0f); d.feed(S(200, 0.0f));
      CHECK(d.resolve(250) == Combo::BackForward); }

    // enemy step
    { StepTracker s;
      s.update({true, false, false, 0});                                           // a release baseline is required first
      CHECK(s.update({true, true, true, 7}).action == StepAction::Step);          // fresh airborne contact press
      CHECK(s.update({true, true, true, 7}).action == StepAction::None);          // held: never repeats
      CHECK(s.update({true, false, true, 7}).action == StepAction::None);
      StepDecision again = s.update({true, true, true, 7});                        // same enemy again on a fresh press
      CHECK(again.action == StepAction::Step && again.actor == 7);
      s.update({true, false, false, 0});
      CHECK(s.update({false, true, true, 7}).action == StepAction::None);         // grounded press
      s.update({true, false, false, 0});
      CHECK(s.update({true, true, false, 0}).action == StepAction::None);         // no actual contact (proximity is not contact)
      s.update({true, false, false, 0});
      CHECK(s.update({true, true, true, 0}).action == StepAction::None); }        // no actor identity

    // module: fail-closed without readers, gates default off
    { Module m; uint32_t node = 0;
      CHECK(!m.finishers_enabled() && !m.enemy_step_enabled());
      m.enable_finishers(true);
      CHECK(!m.on_chooser(kArgs, 0, &node));                                       // enabled but no readers => not handled
      Readers r; r.direction_sample = dir_reader; r.attack_press = atk_reader; r.finisher_node = node_reader; r.legal_without_step_requirement = legal_reader;
      m.set_readers(r);
      g_sample = S(0, -1.0f);   g_attack = Attack::None;  CHECK(!m.on_chooser(kArgs, 0, &node));
      for (uint32_t x = 16; x < 100; x += 16) { g_sample = S(x, -1.0f); m.on_chooser(kArgs, x, &node); }
      for (uint32_t x = 100; x < 300; x += 16) { g_sample = S(x, 1.0f); m.on_chooser(kArgs, x, &node); }
      g_sample = S(300, 1.0f); g_attack = Attack::Secondary; g_legal = true;
      CHECK(m.on_chooser(kArgs, 300, &node) && node == 100 + static_cast<uint32_t>(Finisher::SpinEdge));
      g_legal = false; CHECK(!m.on_chooser(kArgs, 316, &node));                    // native legality veto => not handled
      m.enable_finishers(false); g_legal = true; CHECK(!m.on_chooser(kArgs, 332, &node));
      FILE* f = std::tmpfile(); unsigned n = m.flush(f); CHECK(diagnostics::enabled?n >= 2:n==0); std::fclose(f);
      // enemy-step gate: default off => no decision; on => fresh-press decision
      CHECK(m.on_step_update({true, true, true, 9}, 1).action == StepAction::None);
      m.on_step_update({true, false, false, 0}, 2);
      m.enable_enemy_step(true);
      CHECK(m.on_step_update({true, true, true, 9}, 3).action == StepAction::None);   // armed only after a release is seen
      m.on_step_update({true, false, false, 0}, 4);
      CHECK(m.on_step_update({true, true, true, 9}, 5).action == StepAction::Step); }

    // regression: tracker history invalidation (stale direction / unlock-relock / target switch)
    { DirectionTracker d; hold(d, 0, 100, -1.0f);
      d.feed(S(100, 0.0f, 1.0f, false));                                                // unlock
      hold(d, 116, 400, 1.0f);                                                          // relock, plain Forward
      CHECK(d.resolve(400) == Combo::Forward); }
    { DirectionTracker d;
      for (uint32_t x = 0; x < 100; x += 16) d.feed(S(x, -1.0f, 1.0f, true, 1));
      for (uint32_t x = 100; x < 300; x += 16) d.feed(S(x, 1.0f, 1.0f, true, 2));      // target switched mid-gesture
      CHECK(d.resolve(300) == Combo::Forward); }
    { Module m; uint32_t node = 0; Readers r; r.direction_sample = dir_reader; r.attack_press = atk_reader;
      r.finisher_node = node_reader; r.legal_without_step_requirement = legal_reader; m.set_readers(r); m.enable_finishers(true);
      g_legal = true; g_attack = Attack::None; g_fail_dir = false;
      for (uint32_t x = 0; x < 100; x += 16) { g_sample = S(x, -1.0f); m.on_chooser(kArgs, x, &node); }
      for (uint32_t x = 100; x < 300; x += 16) { g_sample = S(x, 1.0f); m.on_chooser(kArgs, x, &node); }
      g_fail_dir = true; g_attack = Attack::Secondary;
      CHECK(!m.on_chooser(kArgs, 300, &node));                                          // failed reader => unhandled
      g_fail_dir = false; g_sample = S(316, 1.0f);
      CHECK(m.on_chooser(kArgs, 316, &node) && node == 100 + static_cast<uint32_t>(Finisher::ConcussiveBeatdown)); } // old Back history gone
    // regression: Jump held across enable / re-enable never steps
    { Module m; m.enable_enemy_step(false);
      m.on_step_update({true, true, true, 5}, 1);                                       // held while disabled
      m.enable_enemy_step(true);
      CHECK(m.on_step_update({true, true, true, 5}, 2).action == StepAction::None);
      CHECK(m.on_step_update({true, true, true, 5}, 3).action == StepAction::None);
      m.on_step_update({true, false, false, 0}, 4);
      CHECK(m.on_step_update({true, true, true, 5}, 5).action == StepAction::Step);
      m.enable_enemy_step(false); m.enable_enemy_step(true);                            // re-enable while still held
      CHECK(m.on_step_update({true, true, true, 5}, 6).action == StepAction::None); }
    // enemy-step actions: preflight, fixed order, stop on failure, nothing without all actuators
    // row readers (offset tables): attack flags, current node, direction sample from lock/movement/aim rows
    { uint8_t c[rows::kMinCombo]; std::memset(c, 0, sizeof c);
      CHECK(rows::attack_from_combo(c) == Attack::None && rows::combo_current_node(c) == 0);
      c[0x28] = 1; CHECK(rows::attack_from_combo(c) == Attack::Primary);
      c[0x28] = 0; c[0x2a] = 1; CHECK(rows::attack_from_combo(c) == Attack::Secondary);
      c[0x29] = 1; c[0x2b] = 1; c[0x2a] = 0; CHECK(rows::attack_from_combo(c) == Attack::None);   // release-type kinds are not presses
      const uint32_t node = 0xffffffffu; std::memcpy(c + 0x20, &node, 4); CHECK(rows::combo_current_node(c) == 0xffffffffu); }
    { uint8_t lock[rows::kMinLock], move[rows::kMinMovement], aim[rows::kMinAim];
      std::memset(lock, 0, sizeof lock); std::memset(move, 0, sizeof move); std::memset(aim, 0, sizeof aim);
      auto put = [](uint8_t* p, size_t off, float a, float b, float c) { float v[3] = {a, b, c}; std::memcpy(p + off, v, sizeof v); };
      DirectionSample s{};
      lock[0] = 1; lock[0xa0] = 1; put(lock, 0x90, 10, 5, 0);                         // target 10 units on +X, 5 up
      put(aim, 0, 0, 0, 0); const float mag = 1.0f; std::memcpy(move + 0x24, &mag, 4);
      put(move, 0, 1, 0, 0);                                                          // moving toward the target
      CHECK(rows::direction_sample(lock, move, aim, 77, 100, &s) && s.locked && s.target_session == 77 && s.target_alignment > 0.999f);
      put(move, 0, -1, 0, 0); CHECK(rows::direction_sample(lock, move, aim, 77, 100, &s) && s.target_alignment < -0.999f);
      put(move, 0, 0, 0, 1);  CHECK(rows::direction_sample(lock, move, aim, 77, 100, &s) && s.target_alignment > -0.001f && s.target_alignment < 0.001f);
      lock[0] = 0; CHECK(!rows::direction_sample(lock, move, aim, 77, 100, &s));        // not locked: no sample invented
      lock[0] = 1; lock[0xa0] = 0; CHECK(!rows::direction_sample(lock, move, aim, 77, 100, &s));  // target position invalid
      lock[0xa0] = 1; CHECK(!rows::direction_sample(lock, move, aim, 0, 100, &s));       // no target handle
      put(move, 0, 0, 1, 0); CHECK(!rows::direction_sample(lock, move, aim, 77, 100, &s));       // purely vertical movement degenerates after projection
      put(lock, 0x90, 0, 3, 0); put(move, 0, 1, 0, 0); CHECK(!rows::direction_sample(lock, move, aim, 77, 100, &s));   // target straight above: degenerate
      // fail-closed numerics: no usable sample and *out untouched for NaN/Inf/overflow/negative magnitude/inconsistent rows
      const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
      auto sentinel = [] { return DirectionSample{0xdeadu, false, -7.0f, -7.0f, 99}; };
      auto untouched = [](const DirectionSample& x) { return x.now_ms == 0xdeadu && !x.locked && x.target_session == 99; };
      auto reset_rows = [&] { std::memset(lock, 0, sizeof lock); std::memset(move, 0, sizeof move); std::memset(aim, 0, sizeof aim);
                              lock[0] = 1; lock[0xa0] = 1; put(lock, 0x90, 10, 5, 0); put(aim, 0, 0, 0, 0); put(move, 0, 1, 0, 0);
                              const float one = 1.0f; std::memcpy(move + 0x24, &one, 4); };
      for (float v : {nan, inf, -inf}) {
          for (int field = 0; field < 4; ++field) {
              reset_rows(); s = sentinel();
              if (field == 0) put(lock, 0x90, v, 5, 0);
              if (field == 1) put(aim, 0, 0, v, 0);
              if (field == 2) put(move, 0, 1, 0, v);
              if (field == 3) std::memcpy(move + 0x24, &v, 4);
              CHECK(!rows::direction_sample(lock, move, aim, 77, 100, &s) && untouched(s));
          }
      }
      reset_rows(); s = sentinel(); { const float neg = -0.5f; std::memcpy(move + 0x24, &neg, 4); }
      CHECK(!rows::direction_sample(lock, move, aim, 77, 100, &s) && untouched(s));            // negative magnitude
      reset_rows(); s = sentinel(); put(lock, 0x90, 3e38f, 0, 0); put(aim, 0, -3e38f, 0, 0);   // finite inputs whose difference overflows
      CHECK(!rows::direction_sample(lock, move, aim, 77, 100, &s) && untouched(s));
      reset_rows(); s = sentinel(); put(move, 0, 0, 0, 0);                                      // positive magnitude, zero direction: inconsistent
      CHECK(!rows::direction_sample(lock, move, aim, 77, 100, &s) && untouched(s));
      reset_rows(); s = sentinel(); put(lock, 0x90, 0, 5, 0);                                   // positive magnitude, target at the aim origin (horizontally)
      CHECK(!rows::direction_sample(lock, move, aim, 77, 100, &s) && untouched(s));
      reset_rows(); put(lock, 0x90, 1e30f, 0, 0); put(move, 0, 1, 0, 0);                       // huge but finite still works via scaling
      CHECK(rows::direction_sample(lock, move, aim, 77, 100, &s) && s.target_alignment > 0.999f);
      reset_rows(); s = sentinel(); put(lock, 0x90, 1e-30f, 0, 0);                              // tiny: below the degenerate threshold, rejected
      CHECK(!rows::direction_sample(lock, move, aim, 77, 100, &s) && untouched(s));
      // valid NEUTRAL movement is a real sample (not a reader failure)
      reset_rows(); { const float zero = 0.0f; std::memcpy(move + 0x24, &zero, 4); } put(move, 0, 0, 0, 0); s = sentinel();
      CHECK(rows::direction_sample(lock, move, aim, 77, 123, &s) && s.locked && s.now_ms == 123 && s.stick_magnitude == 0.0f &&
            s.target_alignment == 0.0f && s.target_session == 77);
      lock[0] = 0; s = sentinel(); CHECK(!rows::direction_sample(lock, move, aim, 77, 123, &s) && untouched(s));   // neutral never bypasses the lock check
      reset_rows(); lock[0xa0] = 0; { const float zero = 0.0f; std::memcpy(move + 0x24, &zero, 4); } s = sentinel();
      CHECK(!rows::direction_sample(lock, move, aim, 77, 123, &s) && untouched(s)); }                              // ... nor target validity
    // copied rows -> Module: Back, then a valid NEUTRAL frame, then an attack inside the release grace still selects the Back finisher;
    // a failed (non-finite) row clears the history so the same attack is NOT selected afterwards
    { g_row_lock.fill(0); g_row_move.fill(0); g_row_aim.fill(0); g_row_combo.fill(0);
      g_row_lock[0] = 1; g_row_lock[0xa0] = 1;
      { const float tp[3] = {10, 0, 0}; std::memcpy(g_row_lock.data() + 0x90, tp, sizeof tp); }
      Module m; uint32_t node = 0; Readers r; r.direction_sample = row_dir_reader; r.attack_press = row_atk_reader;
      r.finisher_node = node_reader; r.legal_without_step_requirement = legal_reader; g_legal = true;
      m.set_readers(r); m.enable_finishers(true);
      auto set_move = [](float x, float mag) { const float v[3] = {x, 0, 0}; std::memcpy(g_row_move.data(), v, sizeof v); std::memcpy(g_row_move.data() + 0x24, &mag, 4); };
      g_row_now = 0;
      for (uint32_t x = 0; x < 100; x += 16) { set_move(-1, 1); g_row_now = x; m.on_chooser(kArgs, x, &node); }   // Back held
      set_move(0, 0); g_row_now = 116; CHECK(!m.on_chooser(kArgs, 116, &node));                                     // neutral frame: valid sample, no attack
      g_row_combo[0x28] = 1; g_row_now = 132;
      CHECK(m.on_chooser(kArgs, 132, &node) && node == 100 + static_cast<uint32_t>(Finisher::UppercutSurge));       // Back + Primary, inside grace
      g_row_combo[0x28] = 0;
      set_move(-1, 1); for (uint32_t x = 200; x < 300; x += 16) { g_row_now = x; m.on_chooser(kArgs, x, &node); }
      { const float nanv = std::numeric_limits<float>::quiet_NaN(); std::memcpy(g_row_move.data() + 0x24, &nanv, 4); }
      g_row_now = 300; CHECK(!m.on_chooser(kArgs, 300, &node));                                                      // non-finite row: reader failure, history cleared
      set_move(0, 0); g_row_combo[0x28] = 1; g_row_now = 316;
      CHECK(!m.on_chooser(kArgs, 316, &node)); }                                                                      // no stale Back survives the failure
    // write-set descriptors touch exactly the documented bytes and nothing else (canary-checked)
    { uint8_t row[write_sets::kJumpRow]; std::memset(row, 0xAA, sizeof row);
      write_sets::jump_counter_reset(row);
      for (size_t i = 0; i < sizeof row; ++i) {
          const bool written = i == 0x15 || i == 0x20 || i == 0x21 || i == 0x22 || (i >= 0x1c && i < 0x20);
          CHECK(written ? (i == 0x15 ? row[i] == 3 : i == 0x20 ? row[i] == 0xff : row[i] == 0) : row[i] == 0xAA);
      } }
    { uint8_t row[write_sets::kDodgeRow]; std::memset(row, 0xAA, sizeof row);
      write_sets::air_dodge_reset(row, false);
      for (size_t i = 0; i < sizeof row; ++i) CHECK((i >= 0x34 && i < 0x38) ? row[i] == 0 : row[i] == 0xAA);
      write_sets::air_dodge_reset(row, true);
      for (size_t i = 0; i < sizeof row; ++i) CHECK(((i >= 0x34 && i < 0x38) || (i >= 0x30 && i < 0x34) || (i >= 0x28 && i < 0x2c)) ? row[i] == 0 : row[i] == 0xAA); }
    { uint8_t row[write_sets::kAirAttacksRow]; std::memset(row,0xAA,sizeof row);
      write_sets::air_attack_reset(row);
      for (size_t i=0;i<sizeof row;++i) CHECK(i>=8 && i<12 ? row[i]==0 : row[i]==0xAA); }
    // Two-phase contract: preflight (no mutation) then commit reset -> jump -> cancel; partial mutation self-disables.
    { StepActuators a;
      a.owns_player_and_frame = own_a; a.can_reset = can_reset_a; a.can_start_jump = can_jump_a; a.can_cancel = can_cancel_a;
      a.reset_air_resources = reset_a; a.start_jump = jump_a; a.cancel_attack = cancel_a;
      auto fresh = [&](Module& m) { m.enable_enemy_step(true); m.set_step_actuators(a); g_order_n = 0; g_mutations = 0;
                                    g_ok_own = g_ok_can_reset = g_ok_can_jump = g_ok_can_cancel = g_ok_cancel = g_ok_reset = g_ok_jump = true;
                                    m.step({true, false, false, 0}, nullptr, 1); };              // release baseline
      { Module m; fresh(m); StepResult r = m.step({true, true, true, 42}, nullptr, 2);
        CHECK(r.action == StepAction::Step && r.failed == StepStage::Done && r.mutated && r.actor == 42);
        CHECK(g_order_n == 7 && g_order[0] == 1 && g_order[1] == 5 && g_order[2] == 6 && g_order[3] == 7 &&
              g_order[4] == 3 && g_order[5] == 4 && g_order[6] == 2);                              // own, can_*(3), reset, jump, cancel
        FILE* f = std::tmpfile(); CHECK(diagnostics::enabled?m.flush(f) >= 2:m.flush(f)==0); std::fclose(f); }
      { Module m; fresh(m); g_ok_can_cancel = false;                                               // preflight failure: NOTHING mutates
        StepResult r = m.step({true, true, true, 42}, nullptr, 2);
        CHECK(r.failed == StepStage::Preflight && !r.mutated && g_order_n == 4 && g_mutations == 0); }
      { Module m; fresh(m); g_ok_own = false;
        StepResult r = m.step({true, true, true, 42}, nullptr, 2);
        CHECK(r.failed == StepStage::Ownership && !r.mutated && g_order_n == 1); }
      { Module m; fresh(m); g_ok_reset = false;                                                    // first mutator fails: outcome UNKNOWN (grouped writes may be partial): no jump/cancel, self-disable
        StepResult r = m.step({true, true, true, 42}, nullptr, 2);
        CHECK(r.failed == StepStage::Reset && r.mutated && g_mutations == 1 && g_order_n == 5 && !m.enemy_step_enabled()); }
      { Module m; fresh(m); g_ok_jump = false;                                                     // partial mutation: cancel never runs, feature disables itself
        StepResult r = m.step({true, true, true, 42}, nullptr, 2);
        CHECK(r.failed == StepStage::Jump && r.mutated && g_mutations == 2 && !m.enemy_step_enabled());
        m.enable_enemy_step(true); m.step({true, false, false, 0}, nullptr, 3);
        g_ok_jump = true; g_mutations = 0;
        CHECK(m.step({true, true, true, 42}, nullptr, 4).failed == StepStage::Done && g_mutations == 3); }
      { Module m; fresh(m); g_ok_cancel = false;                                                   // cancel is last: jump already happened, then it self-disables
        StepResult r = m.step({true, true, true, 42}, nullptr, 2);
        CHECK(r.failed == StepStage::Cancel && r.mutated && !m.enemy_step_enabled()); }
      { Module m; StepActuators b = a; b.can_cancel = nullptr; m.enable_enemy_step(true); m.set_step_actuators(b); g_order_n = 0;
        m.step({true, false, false, 0}, nullptr, 1);
        StepResult r = m.step({true, true, true, 7}, nullptr, 2);
        CHECK(r.failed == StepStage::MissingActuator && g_order_n == 0); } }                       // missing actuator: no action at all
    std::printf(failures ? "combat logic: %d FAILURES\n" : "combat logic: all checks passed\n", failures);
    return failures ? 1 : 0;
}
