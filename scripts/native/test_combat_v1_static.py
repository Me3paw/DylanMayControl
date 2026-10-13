"""Validate prototype pins and damage IDs against the pinned executable and decoded player assets."""
import hashlib
import json
from pathlib import Path
import re
import sys
import struct
import pefile

lab = Path(__file__).resolve().parents[2]
binary = Path(sys.argv[1])
data = binary.read_bytes()
assert hashlib.sha256(data).hexdigest() == '9f56611d767c9227a8a1b250c01acf9478b50933c65f86a1e044fcc53df67cb1'
pe = pefile.PE(data=data, fast_load=True)
source = '\n'.join((lab / f'src/native/{name}.h').read_text() for name in ('combat_v1_damage','combat_v1_runtime','combat_v1_step','combat_v1_wave'))
pins = re.findall(r'\{"(v1_[^"]+)",([^,]+),\d+,"([0-9a-f]+)"\}', source)
assert len(pins) == 28
for name, rva, signature in pins:
    address = 0x2709f10 if rva == 'kRva' else int(rva, 16)
    expected = bytes.fromhex(signature)
    assert pe.get_data(address, len(expected)) == expected, name
    # Six typed wave builders share this prologue. Its additional interior pin
    # uniquely identifies the damage-pool builder; both are checked live.
    assert data.count(expected) == (6 if name == 'v1_wave_pool' else 1), name
for call, target in ((0x239f5c3,0x239dcf0),(0x23acff6,0x24927d0),
                     (0x23abca2,0x24927d0),(0x23abefb,0x24927d0),(0x24981aa,0x2706570),(0x238b0ab,0x2492a00),(0x243b3bf,0x22d0880),(0x2698c5b,0x22d0880),
                     (0x22d2018,0x22d1c10),(0x23ad7db,0x23acb30),(0x270a0e7,0x27147e0),(0x270a163,0x27147e0),(0x2707ddb,0x2709150),(0x249603b,0x2493dd0)):
    code=pe.get_data(call,5)
    assert code[0]==0xe8 and call+5+struct.unpack_from('<i',code,1)[0]==target, hex(call)
assert pe.get_data(0x3afec10,4)==bytes.fromhex("00000034") # native negligible-output epsilon
assert pe.get_data(0x23dcaf7,6)==bytes.fromhex("41b804676ebb") # exact resource key, not upper-bound key+1
assert 'key!=0xbb6e6704' in (lab / 'src/native/combat_v1_wave.h').read_text()
policy = (lab / 'src/native/combat_v1_policy.h').read_text().split('inline constexpr Tuning tuning[] = {', 1)[1].split('\n};', 1)[0]
ids = [int(x, 16) for x in re.findall(r'\{(0x[0-9a-f]+),\{', policy)]
assert len(ids) == len(set(ids)) == 21
assets = lab / '.work/combat-static-map-20261012'
definitions = json.loads((assets / 'damage-definitions.json').read_text())
records = {int(r['id'], 16): r for c in definitions['components'] for r in c.get('records', [])}
links = json.loads((assets / 'attack-damage-map.json').read_text())['records']
hit_ids = {int(h['damage_id'], 16) for r in links for h in r['hit_damage_links']}
for key in ids:
    assert key in records and (key in hit_ids or key == 0x31f6e1fb), hex(key)
    assert all(2 <= v['type'] <= 9 and v['value'] >= 0 for v in records[key]['values'])
    assert not records[key]['name'].startswith(('flurry_', 'slice_', 'slash_'))
assert not any(records[key]['name'].startswith('crush_') for key in ids)
graph = json.loads((assets / 'authored-attack-graph.json').read_text())['nodes']
by_id = {int(n['id']['id'],16):n['name'] for n in graph}
logical = (lab / 'src/native/combat_v1_policy.h').read_text().split('pairs[][2]={',1)[1].split('};',1)[0]
pairs = re.findall(r'\{(0x[0-9a-f]+),(0x[0-9a-f]+)\}',logical)
assert len(pairs)==25
for ground,air in pairs:
    g,a=by_id[int(ground,16)],by_id[int(air,16)]
    assert a==g.replace('_ground_','_air_') or a==g+'_air', (g,a)
# The observer must remain compiled out of ordinary release builds.
build = (lab / 'scripts/native/build.sh').read_text()
assert 'combat_v1=0' in build and '--combat-v1) combat_v1=1' in build
assert 'mode="combat-v1-$mode"' in build
assert '#if CRMOD_COMBAT_V1' in (lab / 'src/native/selection_observer.h').read_text()
print(json.dumps({'status': 'PASS', 'pins': len(pins), 'unique_pins': len(pins)-1, 'joined_damage_ids': len(set(ids) & hit_ids),
                  'additional_player_definition': 'split_explosion; no physical-hit join',
                  'primary_and_crush_definitions_unchanged': True,
                  'executable_sha256': hashlib.sha256(data).hexdigest()}))
