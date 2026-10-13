"""Process-local diagnostic client. No global input or game-memory operations."""
import argparse
import json
import socket
import struct
import time
from pathlib import Path

PACKET = struct.Struct('<IIIHBBhhhh')
OPS = {'observe': 1, 'state': 2, 'neutral': 3, 'pass': 4, 'stop': 5}

def encode(op, index=0, lease=0, buttons=0, lt=0, rt=0, lx=0, ly=0, rx=0, ry=0):
    if op not in OPS.values() or not 0 <= index < 4:
        raise ValueError('invalid operation/controller index')
    if not 0 <= buttons <= 0xffff or buttons & 0x0c00:
        raise ValueError('invalid button mask')
    if not all(0 <= v <= 255 for v in (lt, rt)):
        raise ValueError('trigger outside byte range')
    if not all(-32768 <= v <= 32767 for v in (lx, ly, rx, ry)):
        raise ValueError('axis outside signed-short range')
    if op == 2 and not 1 <= lease <= 500:
        raise ValueError('state requires a 1..500ms lease')
    return PACKET.pack(op, index, lease, buttons, lt, rt, lx, ly, rx, ry)

def metadata(path):
    ready = None
    for line in Path(path).read_text().splitlines():
        item = json.loads(line)
        if item.get('event') == 'ready':
            ready = item
        if item.get('event') == 'stopped':
            ready = None
    if not ready or ready.get('protocol') != 1 or not ready.get('backend_verified'):
        raise ValueError('no live verified diagnostic backend in log')
    if not 1 <= ready['port'] <= 65535:
        raise ValueError('invalid loopback port')
    token = bytes.fromhex(ready['token'])
    if len(token) != 16:
        raise ValueError('invalid secret')
    return ready, token

class Client:
    def __init__(self, path):
        info, token = metadata(path)
        self.socket = socket.create_connection(('127.0.0.1', info['port']), timeout=2)
        self.buffer = bytearray()
        try:
            self.socket.sendall(token)
            self.read()
            observed = self.command('observe')
            if observed['pid'] != info['pid']:
                raise ValueError('process identity changed')
        except BaseException:
            self.socket.close()
            raise
    def read(self):
        while b'\n' not in self.buffer:
            part = self.socket.recv(512)
            if not part:
                raise ConnectionError('diagnostic disconnected')
            self.buffer.extend(part)
            if len(self.buffer) > 2048:
                raise ValueError('oversized diagnostic response')
        line, _, remainder = self.buffer.partition(b'\n')
        self.buffer = bytearray(remainder)
        result = json.loads(line)
        if result.get('ok') is not True:
            raise ValueError(f'diagnostic rejected command: {result}')
        return result
    def command(self, name, **values):
        self.socket.sendall(encode(OPS[name], **values))
        return self.read()
    def close(self):
        try:
            self.command('neutral')
        except (OSError, ValueError):
            pass
        finally:
            self.socket.close()
    def __enter__(self):
        return self
    def __exit__(self, *_):
        self.close()

def self_test():
    assert PACKET.size == 24
    values = (2, 3, 500, 0x8001, 255, 1, -32768, 32767, -1, 0)
    assert PACKET.unpack(encode(*values)) == values
    for kwargs in ({'index': 4}, {'lease': 0}, {'lease': 501}, {'buttons': 0x400}, {'lt': 256}, {'lx': -32769}):
        try:
            encode(2, **({'lease': 250} | kwargs))
        except ValueError:
            continue
        raise AssertionError(kwargs)
    print('24-byte protocol and invalid-input boundaries verified')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=['self-test', 'observe', 'neutral', 'pass', 'stop', 'hold'])
    parser.add_argument('--log')
    parser.add_argument('--index', type=int, default=0)
    parser.add_argument('--buttons', type=lambda v: int(v, 0), default=0)
    parser.add_argument('--seconds', type=float, default=0.5)
    args = parser.parse_args()
    if args.operation == 'self-test':
        self_test()
        return
    if not args.log:
        parser.error('--log is required')
    if not 0 <= args.seconds <= 30:
        parser.error('--seconds must be 0..30')
    with Client(args.log) as client:
        if args.operation == 'hold':
            deadline = time.monotonic() + args.seconds
            sent_time = None
            while time.monotonic() < deadline:
                if sent_time is not None and time.monotonic() - sent_time > 0.2:
                    raise TimeoutError('input lease schedule missed; replay aborted')
                sent_time = time.monotonic()
                client.command('state', index=args.index, buttons=args.buttons, lease=250)
                time.sleep(min(0.1, max(0, deadline - time.monotonic())))
            print(json.dumps(client.command('neutral')))
        else:
            print(json.dumps(client.command(args.operation)))

if __name__ == '__main__':
    main()
