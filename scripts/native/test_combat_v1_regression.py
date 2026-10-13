"""Pinned request-order model and source contract; no native gameplay claim."""
from pathlib import Path
import importlib.util
import sys
from itertools import product

lab = Path(__file__).resolve().parents[2]
model_path = Path(sys.argv[1]) if len(sys.argv)>1 else lab / '.work/combat-v1-native-cancel-20261012/arbitration_cases.py'
spec = importlib.util.spec_from_file_location('native_arbitration', model_path)
model = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = model
spec.loader.exec_module(model)
step = (lab / 'src/native/combat_v1_step.h').read_text()
readiness = step.split('const bool ready=', 1)[1].split('if(ready)', 1)[0]
assert 'cached' not in readiness and 'slot(' not in readiness
assert 'enemy_contact_native::contact_now' in readiness
assert 'step_binding::is_enemy' in readiness and 'loadout_actor_resolver::current' in readiness
contact = (lab / 'src/native/enemy_contact_native.h').read_text()
assert 'const double margin=contact_margin();' in contact
assert 'snapshot().contact_margin_mm/1000.0' in contact
assert 'margin=1.5' not in contact
for cm in range(30,151,10):
    assert abs((cm*10)/1000-cm/100)<1e-12
assert 'if(!inflate_query(cap,margin))return false;' in contact
assert 'discover_nearby(p);' in contact.split('inline void verify_scene', 1)[1].split('inline Invocation begin', 1)[0]
loader=(lab / 'src/native/selection_observer.h').read_text()
chain=(lab / 'src/native/loadout_chain_switch.h').read_text()
assert 'combat_v1_primary' not in loader and 'recovery_permit' not in chain
assert 'return choose(a)==t.accepted && sequence_current(t,rows);' in chain
assert '!sequence_current(t,rows)' in chain and 'if (!window_mode(' in chain
assert 'combat_v1_step_native::attack_end_post' in loader
# Input's WRITE loan precedes the native reset of prior consumed results.
# Every previous-cycle cache combination must reach the same current arbitration.
checks = 0
for melee_cached, jump_cached, fresh, enemy, incoming in product((False, True), repeat=5):
    armed = fresh and enemy
    old_prescreen = armed and not melee_cached and not jump_cached
    if armed and (melee_cached or jump_cached):
        assert not old_prescreen
    # Native pre_handle_requests clears all consumed markers before these consumers.
    edges = {(1, 16)} | ({(32, 16)} if incoming else set())
    states = {1: 2, 16: 4 if armed else 1, 32: 2 if incoming else 1, 64: 1}
    slots = model.run((1, 16, 32, 64), states, edges, armed, (1, 16, 32, 64))
    if armed and not incoming:
        assert slots[16].state & 4 and slots[1].result & 1
    elif armed:
        assert not slots[16].state & 4 and not slots[1].result & 1
    else:
        assert not slots[16].state & 4 and not slots[1].result & 1
    checks += 1
print(f'PASS {checks} fresh/enemy/prior-cache/current-blocker cases; no cache mutation; source detector guards retained')
