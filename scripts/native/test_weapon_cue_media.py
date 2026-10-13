"""Check the pinned native PCM header and preservation of audition sample bytes."""
from pathlib import Path
import runpy
import struct
import wave

lab = Path(__file__).resolve().parents[2]
encode = runpy.run_path(str(lab / 'scripts/native/build_weapon_cues.py'))['native_pcm_media']
# Actual mono/stereo fmt payloads from stock media 1045542478 and 846919879.
stock_fmt = {
    1: bytes.fromhex('feff010080bb000000770100020010000600000001410000'),
    2: bytes.fromhex('feff020080bb000000ee0200040010000600000002310000'),
}
for path in (lab / 'src/audio/weapon_cues').glob('*.wav'):
    with wave.open(str(path), 'rb') as source:
        channels = source.getnchannels()
        pcm = source.readframes(source.getnframes())
    ordinary = path.read_bytes()
    assert ordinary[12:16] == b'fmt ' and struct.unpack_from('<I', ordinary, 16)[0] == 16
    # The game's fmt+20 channel-config read would hit the ordinary WAV data length.
    assert struct.unpack_from('<I', ordinary, 40)[0] == len(pcm)
    wem = encode(path)
    assert wem[:4] == b'RIFF' and struct.unpack_from('<I', wem, 4)[0] == len(wem) - 8
    assert wem[8:16] == b'WAVEfmt ' and struct.unpack_from('<I', wem, 16)[0] == 24
    assert wem[20:44] == stock_fmt[channels]
    assert struct.unpack_from('<I', wem, 40)[0] == (0x4101 if channels == 1 else 0x3102)
    assert wem[44:48] == b'data' and struct.unpack_from('<I', wem, 48)[0] == len(pcm)
    assert wem[52:] == pcm
print('PASS: six native PCM fmt headers match stock; original incompatible read reproduced; all PCM unchanged')
