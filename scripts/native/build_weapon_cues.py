"""Build a Wwise v154 PCM bank from the six user-selected local auditions."""
from pathlib import Path
import hashlib
import json
import struct
import sys
import wave


def fnv(name):
    value = 2166136261
    for byte in name.lower().encode('ascii'):
        value = ((value * 16777619) ^ byte) & 0xffffffff
    return value


def chunk(tag, data):
    return tag + struct.pack('<I', len(data)) + data


def object_record(kind, data):
    return bytes([kind]) + struct.pack('<I', len(data)) + data


def native_pcm_media(path):
    with wave.open(str(path), 'rb') as wav:
        channels, rate = wav.getnchannels(), wav.getframerate()
        assert wav.getsampwidth() == 2 and channels in (1, 2)
        assert rate == 48000 and 0 < wav.getnframes() <= 96000
        pcm = wav.readframes(wav.getnframes())
    # The pinned decoder reads channel configuration at fmt+20 even for PCM.
    # Match the game's 24-byte Wwise fmt; a normal 16-byte WAV fmt is incompatible.
    config = 0x4101 if channels == 1 else 0x3102
    fmt = struct.pack('<HHIIHHHHI', 0xfffe, channels, rate,
                      rate * channels * 2, channels * 2, 16, 6, 0, config)
    body = b'WAVE' + chunk(b'fmt ', fmt) + chunk(b'data', pcm)
    return b'RIFF' + struct.pack('<I', len(body)) + body


def build(output):
    lab = Path(__file__).resolve().parents[2]
    assets = lab / 'src/audio/weapon_cues'
    rows = json.loads((assets / 'manifest.json').read_text())
    assert [r['weapon'] for r in rows] == ['Strike', 'Cleave', 'Glaive', 'Crush', 'Drill', 'Extend']
    bank_id = fnv('crmod_weapon_queue_cues')
    # The stock Player bus (init.bnk) inherits volume_sfx, master_volume and mix.
    bus = 1069431850
    records, media, didx, event_ids = [], bytearray(), bytearray(), []
    all_ids = {bank_id}
    for row in rows:
        path = assets / row['asset']
        assert hashlib.sha256(path.read_bytes()).hexdigest() == row['sha256']
        data = native_pcm_media(path)
        prefix = 'crmod_queue_' + row['weapon'].lower()
        sound, action, event, source, cache = [fnv(prefix + suffix) for suffix in ('_sound', '_action', '', '_media', '_cache')]
        ids = {sound, action, event, source, cache}
        assert len(ids) == 5 and not (all_ids & ids) and cache
        all_ids.update(ids)
        event_ids.append(event)
        media.extend(bytes((-len(media)) % 16))
        didx.extend(struct.pack('<III', source, len(media), len(data)))
        media.extend(data)
        # Sound ID, PCM codec, in-bank source, source/cache IDs, size, has-source.
        # Give each clip a distinct v154 media-cache identity, matching stock banks.
        payload = struct.pack('<IIBIIIB', sound, 0x00010001, 0, source, cache, len(data), 0x80)
        payload += bytes(4)  # no local FX or metadata
        payload += struct.pack('<II', bus, 0)  # own output bus; no stock parent
        payload += bytes(3)  # priority defaults, no properties or random ranges
        payload += bytes([1])  # override positioning: plain 2D stereo
        payload += bytes([4]) + bytes(4)  # no auxiliary sends/reflection bus
        payload += bytes([0x18, 0, 0, 0, 0, 0])  # no local voice limit; default virtualization
        payload += bytes(4)  # no state properties/groups or RTPC curves
        records.append(object_record(2, payload))
        # Play on this game object; no delay, transition or randomization.
        payload = struct.pack('<IHI', action, 0x0403, sound) + bytes(4)
        payload += struct.pack('<II', bank_id, 0)
        records.append(object_record(3, payload))
        records.append(object_record(4, struct.pack('<IBI', event, 1, action)))
    header = struct.pack('<6I', 154, bank_id, fnv('sfx'), 16, 16668, 0) + bytes(24)
    bank = chunk(b'BKHD', header) + chunk(b'DIDX', didx) + chunk(b'DATA', media)
    bank += chunk(b'HIRC', struct.pack('<I', len(records)) + b''.join(records))
    output.mkdir(parents=True, exist_ok=True)
    target = output / 'weapon_cues.bnk'
    target.write_bytes(bank)
    (output / 'weapon_cues.rc').write_text('401 RCDATA "' + target.resolve().as_posix() + '"\n')
    (output / 'weapon_cue_ids.h').write_text(
        '#pragma once\ninline constexpr unsigned cr_weapon_cue_bank_id=' + str(bank_id) + 'u;\n'
        'inline constexpr unsigned cr_weapon_cue_events[6]={' + ','.join(str(i)+'u' for i in event_ids) + '};\n')
    print('Weapon queue bank:', len(bank), 'bytes; six hash-verified PCM samples')


if __name__ == '__main__':
    build(Path(sys.argv[1]))
