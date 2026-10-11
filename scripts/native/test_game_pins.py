"""Verify current native pins against the exact supported executable, without running it."""
import hashlib
from pathlib import Path
import re
import sys
import pefile

lab = Path(__file__).resolve().parents[2]
data = Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(data).hexdigest() == '9f56611d767c9227a8a1b250c01acf9478b50933c65f86a1e044fcc53df67cb1'
pe = pefile.PE(data=data, fast_load=True)
assert pe.FILE_HEADER.Machine == 0x8664 and pe.OPTIONAL_HEADER.ImageBase == 0x140000000
text = next(section.get_data() for section in pe.sections if section.Name.rstrip(b'\0') == b'.text')
pins = re.findall(r'\{"([^\"]+)",\s*(0x[0-9a-f]+),\s*\d+,\s*"([0-9a-f]+)"',
                  (lab/'src/native/native_pins.h').read_text())
assert len(pins) == 16
for name, rva, raw in pins:
    pin = bytes.fromhex(raw)
    assert pe.get_data(int(rva, 16), len(pin)) == pin and text.count(pin) == 1, name
ui = (lab/'src/native/native_ui_pins.h').read_text()
for name in ('dom', 'release', 'destroy'):
    raw = re.search(name+r'_bytes\[\]=\{([^}]+)', ui)[1]
    pin = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-f]+)', raw))
    rva = int(re.search(name+r'_rva=(0x[0-9a-f]+)', ui)[1], 16)
    assert pe.get_data(rva, len(pin)) == pin and text.count(pin) == 1, name
    if name == 'destroy':
        assert len(pin) == 48 and text.count(pin[:45]) == 2
print('PASS: 16 native and 3 lifecycle pins match uniquely; duplicate 45-byte listener prefix rejected')
