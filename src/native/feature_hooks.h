// LOADER HOOK REGISTRATION TABLE (root owns installation; this header is the exact contract). Build 0.563.737.9, image base 0x140000000.
// For every hook: verify the pin (native_pins::verify) FIRST, install with MinHook at base+rva, store the trampoline BEFORE enabling.
//   H1 finisher chooser  RVA 0x235e1e0  10 args  detour finisher_binding::chooser_detour   trampoline -> finisher_binding::set_original(ChooserFn)
//   H2 collision wrapper RVA 0x277f170   4 args  detour [](a,b,c,d){ g_obs.run(g_orig,a,b,c,d); } with collision_observer::Observer (default OFF)
//   H3 lock-on body      RVA 0x27e6cb0   3 args  (cols, ctx, dt)  passive observer, install ONLY when scope sources exist (cb13/cb14)
//   H4 movement body     RVA 0x228ec70   3 args  (cols, ctx, dt)  passive observer, same condition
//   H5 locomotion state  RVA 0x2278f90   2 args  (cols, x)  detour step_binding::loco_detour   (player_locomotion::update_locomotion_state body)
//   H6 jump input body   RVA 0x23f9e80   4 args  (cols, p2, p3, float* dt)  detour step_binding::jump_detour   (jump::handle_input body)
//   H2 must call step_binding::feed_world_from_ctx(ctx) (arg 2 of the collision wrapper) BEFORE forwarding: it supplies the world pointer
//   for the enemy filter (AIBodyComponent test); without it the enemy step stays Unbound with reason world_unknown.
// Calls the bindings make WITHOUT hooking (no install): 0x2339010 0x235d740 0x235c420 0x235d2b0 0x235d5f0 0x2dfecb0 0x228d1f0 (pins in native_pins.h).
// Startup sequence:
//   1. feature_status::registry().set_sink(collector)         (non-blocking sink; one line per call)
//   2. finisher_binding::init(base, guardedCopy, monotonicMs, collector)  -> false = a pin mismatched: DO NOT install H1 (status line says which)
//   3. parse the evidence config and fill the table:  finisher_binding::parse_config(text, len, &finisher_binding::table()); then refresh_status()
//   4. install H1 (set_original first). With the node table empty / scope sources missing the detour only forwards and logs discovery.
//   5. step_binding::init(base, guardedCopy, guardedWrite, collector); install H5 + H6 (set_originals first), step_binding::set_hooked(true,true); H2 feeds the world
//   6. slow tick (~1 s): finisher_binding::refresh_status(); feature_status::registry().emit_status(Feature::Finisher/EnemyStep)
// Config file (one line per finisher; produced from the discovery lines `G ...`): finisher=UppercutSurge form=<def id> node_id=<id> node_index=<idx>
#pragma once
#include "finisher_binding.h"
#include "step_binding.h"

namespace feature_hooks {
struct Hook { const char* name; uint32_t rva; uint8_t args; const char* detour; const char* condition; };
inline const Hook* table(size_t* n) {
    static const Hook kHooks[] = {
        {"chooser",        0x235e1e0, 10, "finisher_binding::chooser_detour", "pin ok"},
        {"collision_wrap", 0x277f170, 4,  "collision_observer::Observer::run (via 4-arg thunk)", "pin ok; observer default OFF"},
        {"lockon_body",    0x27e6cb0, 3,  "combat_native::observe_lockon", "scope sources installed"},
        {"movement_body",  0x228ec70, 3,  "combat_native::observe_movement", "scope sources installed"},
        {"loco_state",     0x2278f90, 2,  "step_binding::loco_detour", "pin ok; set_originals() before enable; step_binding::init ok"},
        {"jump_input",     0x23f9e80, 4,  "step_binding::jump_detour", "pin ok; same as loco_state"},
    };
    if (n) *n = sizeof kHooks / sizeof kHooks[0];
    return kHooks;
}
}  // namespace feature_hooks
