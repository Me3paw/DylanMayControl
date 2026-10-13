"""Stock-flow source contracts against the immutable accepted mod; no gameplay claim."""
from pathlib import Path

lab = Path(__file__).resolve().parents[2]
accepted = lab / '.work/patch-0564-20261011/source-final/src/native'
before = lab / '.work/combat-v1-stock-flow-20261012/before/src/native'
source = lab / 'src/native'
stock = (accepted / 'finisher_binding.h').read_text()
current = (source / 'finisher_binding.h').read_text()
for begin, end in (
    ('inline bool shortcut_input(', 'inline bool read_shortcut_legal('),
    ('inline uint32_t choose_with_finisher_chain(', 'inline void graph_detour('),
    ('inline uint32_t chooser_detour(', '}  // namespace finisher_binding'),
):
    assert stock.split(begin, 1)[1].split(end, 1)[0] == current.split(begin, 1)[1].split(end, 1)[0], begin
assert (source / 'finisher_chooser_scope.h').read_bytes() == (accepted / 'finisher_chooser_scope.h').read_bytes()
assert (source / 'loadout_chain_switch.h').read_bytes() == (accepted / 'loadout_chain_switch.h').read_bytes()
runtime = (source / 'combat_v1_runtime.h').read_text()
step = (source / 'combat_v1_step.h').read_text()
loader = (source / 'selection_observer.h').read_text()
for removed in ('automatic_continuation', 'continuation_gate', 'commitment_gate', 'graph_observer',
                'sequence_escape_ready', 'window_pulse', 'combat_v1_primary', 'voluntary_action'):
    assert removed not in current + runtime + step + loader, removed
assert '&combat_v1_runtime::detect' not in loader and '&combat_v1_runtime::window' not in loader
assert '&combat_v1_step_native::request)' not in loader
assert 'ticket.pair' in step and 'conflict_original(logic,candidate,other,dependency)' in step
assert 'conflict_original(logic,other,candidate' not in step
assert 'request_original=reinterpret_cast<Request>(base+0x22d0880);' in step
for name in ('combat_v1_damage.h', 'combat_v1_wave.h'):
    assert (source / name).read_bytes() == (before / name).read_bytes(), name
table = lambda p: p.read_text().split('inline constexpr Tuning tuning[] = {', 1)[1].split('\n};', 1)[0]
assert table(source / 'combat_v1_policy.h') == table(before / 'combat_v1_policy.h')
assert runtime.split('inline bool repeat(', 1)[1].split('inline bool prepare(', 1)[0] == (before / 'combat_v1_runtime.h').read_text().split('inline bool repeat(', 1)[1].split('inline bool prepare(', 1)[0]
assert 'commit_observer(node,combo,cfg,accepted,direct);' in current
assert 'const bool mapped=input_combo!=combo;' in current
assert 'inherit_native_input' in runtime
print('PASS: accepted stock shortcut/chooser/manual followups/scope/handoff exact contracts; custom auto/lock/whiff hooks absent; numerical and final-repeat provenance unchanged; Step-only pair preserved')
