"""Read pinned PE bytes for observation targets. Does not attach, patch or call."""
import argparse
import hashlib
import json
from pathlib import Path
import capstone
import pefile

SHA256 = '2c6575be23ea9a2d316fb530d094773b371ab1da6344aa7a97b8cc2dabaf1ca0'
BASE = 0x140000000
TARGETS = {
    'ui_selection_commit': (0x141f08580, '48895c241048896c2418565741564883ec60', 'sc-b9.txt'),
    'node_flag_setter': (0x141ddc7e0, '4883ec08440fb782f40100004c8bda440fb7d1664585c2', 'sc-b11.txt'),
    'live_node_state_system': (0x141de1f00, None, 'sc-mk4.txt'),
}


def verify(executable):
    data = executable.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        raise ValueError('executable differs from pinned target build')
    pe = pefile.PE(data=data)
    if pe.FILE_HEADER.Machine != 0x8664 or pe.OPTIONAL_HEADER.ImageBase != BASE:
        raise ValueError('target machine/image base differs')
    text = next(section for section in pe.sections if section.Name.rstrip(b'\0') == b'.text')
    code = text.get_data()
    relocations = []
    for block in getattr(pe, 'DIRECTORY_ENTRY_BASERELOC', []):
        for entry in block.entries:
            if entry.type != 0:
                relocations.append((entry.rva, entry.rva + (8 if entry.type == 10 else 4)))
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    result = dict(sha256=digest, image_base=hex(BASE), machine=hex(pe.FILE_HEADER.Machine),
                  runtime_verified=False, hook_installation_authorized=False, targets={})
    for name, (address, expected, source) in TARGETS.items():
        rva = address - BASE
        offset = rva - text.VirtualAddress
        if not 0 <= offset < len(code) - 64:
            raise ValueError(f'{name}: target outside code section')
        # The live system entry is a 16-byte tail thunk; adjacent code is not its prologue.
        thunk = name == 'live_node_state_system'
        bound = 16 if thunk else 128
        window = code[offset:offset + bound]
        known = bytes.fromhex(expected) if expected else None
        if known and not window.startswith(known):
            raise ValueError(f'{name}: bytes disagree with saved Ghidra prologue')
        instructions = list(decoder.disasm(window, address, count=24))
        endpoints = [item.address + item.size - address for item in instructions]
        signature = None
        for length in endpoints:
            if length < (16 if thunk else 24) or length > (16 if thunk else 96):
                continue
            if any(start < rva + length and end > rva for start, end in relocations):
                continue
            pattern = window[:length]
            found = code.find(pattern)
            if found == offset and code.find(pattern, found + 1) == -1:
                signature = pattern
                break
        if signature is None:
            raise ValueError(f'{name}: no unique relocation-safe prefix within function observation bound')
        result['targets'][name] = dict(va=hex(address), rva=hex(rva), saved_export=source,
            saved_asm_prefix_verified=known is not None, signature_hex=signature.hex(),
            signature_length=len(signature), section='.text', unique_matches=1,
            observed_entry_kind='tail_thunk' if thunk else 'function_body',
            observation_bound=bound, tail_target='0x141ddedd0' if thunk else None,
            prologue=[dict(address=hex(item.address), bytes=item.bytes.hex(),
                           instruction=f'{item.mnemonic} {item.op_str}') for item in instructions])
    pe.close()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    lab = Path(__file__).resolve().parents[2]
    output = args.out.resolve()
    if not output.is_relative_to(lab / '.work'):
        parser.error('raw instruction output belongs under lab .work')
    result = verify(args.exe)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(sha256=result['sha256'], targets={name: {
        'rva': value['rva'], 'unique_matches': value['unique_matches'],
        'signature_length': value['signature_length'],
        'saved_asm_prefix_verified': value['saved_asm_prefix_verified']
    } for name, value in result['targets'].items()}, runtime_verified=False)))


if __name__ == '__main__':
    main()
