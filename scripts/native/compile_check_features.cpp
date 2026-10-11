// Compile-only check that the feature binding headers are consistent (host + Win64). Not a test suite.
#include "../../src/native/feature_hooks.h"
#include "../../src/native/camera_lock_log.h"
#include "../../src/native/pusher_probe.h"
static void sink(const char*, size_t) {}
static bool wr(void* d, const void* s, size_t n) { std::memcpy(d, s, n); return true; }
static uint32_t clk() { return 0; }
int compile_check_features(uint64_t base, combat_native::CopyFn cp) {
    const bool a = finisher_binding::init(base, cp, clk, sink);
    const bool b = step_binding::init(base, cp, wr, sink);
    finisher_binding::set_original(nullptr);
    const char cfg[] = "# x\nfinisher=WideEdge form=4242 node_id=105 node_index=5\n";
    finisher_binding::parse_config(cfg, sizeof cfg - 1, &finisher_binding::table());
    step_binding::set_originals(nullptr, nullptr); step_binding::set_hooked(true, true); step_binding::feed_world_from_ctx(nullptr); step_binding::refresh_status();
    camera_lock_log::set_original(nullptr); camera_lock_log::set_hooked(true); (void)camera_lock_log::missing(); camera_lock_log::lockon_detour(nullptr, nullptr, nullptr);
    camera_lock_log::flush(nullptr, nullptr, 0); step_binding::flush_state(nullptr, nullptr, 0); pusher_probe::capture(nullptr, 0); pusher_probe::flush(nullptr, nullptr, 0); pusher_probe::set_qpc_source(nullptr); step_binding::set_qpc_source(nullptr);
    step_binding::loco_detour(nullptr, nullptr); step_binding::jump_detour(nullptr, nullptr, nullptr, nullptr);
    step_binding::set_player_source(nullptr); { step_binding::PlayerRows pr; (void)step_binding::resolve_player(0, 0, &pr); (void)step_binding::is_enemy(0, 0); uint8_t dn[16]; uint64_t gid = 0; (void)step_binding::entity_labels(0, 0, dn, &gid); }
    size_t n = 0; (void)feature_hooks::table(&n);
    return a && b && finisher_binding::chooser_detour(nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) == 0;
}
