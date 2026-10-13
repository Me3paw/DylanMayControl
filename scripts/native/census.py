"""Read-only string census over the game exe (Q1/Q4). Writes full-string starts (VA, section, text) to argv[2].
usage: python -I census.py <exe> <out.json> term [term...]   (case-insensitive ASCII)"""
import json, re, sys, pefile

exe, out, terms = sys.argv[1], sys.argv[2], sys.argv[3:]
pe = pefile.PE(exe, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
with open(exe, "rb") as f:
    data = f.read()
secs = [(s.Name.rstrip(b"\0").decode(), s.PointerToRawData, s.SizeOfRawData, s.VirtualAddress) for s in pe.sections]


def loc(off):
    for n, raw, sz, va in secs:
        if raw <= off < raw + sz:
            return n, base + va + off - raw
    return None, None


res = {"image_base": hex(base), "terms": {}}
for t in terms:
    seen = {}
    for m in re.finditer(re.escape(t.encode()), data, re.I):
        o = m.start()
        s = o
        while s > 0 and 0x20 <= data[s - 1] < 0x7f:
            s -= 1
        e = o
        while e < len(data) and 0x20 <= data[e] < 0x7f:
            e += 1
        if s in seen:
            continue
        n, va = loc(s)
        seen[s] = {"va": hex(va) if va else None, "section": n, "len": e - s, "text": data[s:e].decode()[:400]}
    res["terms"][t] = {"count": len(seen), "strings": list(seen.values())[:5000]}
with open(out, "w") as f:
    json.dump(res, f, indent=1)
for t, v in res["terms"].items():
    print(t, v["count"])
