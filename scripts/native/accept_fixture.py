"""Acceptance checks for the running production diagnostic fixture, not gameplay."""
import argparse
import json
import socket
import time
from pathlib import Path
from bridge import Client, PACKET, metadata

FIELDS = ('buttons', 'lt', 'rt', 'lx', 'ly', 'rx', 'ry')
ZERO = dict.fromkeys(FIELDS, 0)
PRESS = dict(buttons=0x1000, lt=255, rt=64, lx=-32768, ly=32767, rx=-1, ry=99)


def signature(row):
    return (row['result'], *(row[name] for name in FIELDS))


def matches(row, values):
    return row['result'] == 0 and all(row[name] == value for name, value in values.items())


class Trace:
    def __init__(self, path):
        self.path = Path(path)
        self.position = 0
        self.partial = ''
        self.rows = []

    def refresh(self):
        if not self.path.exists():
            return
        with self.path.open() as source:
            source.seek(self.position)
            data = source.read(2_000_000)
            self.position = source.tell()
        lines = (self.partial + data).split('\n')
        self.partial = lines.pop()
        for line in lines:
            if not line.startswith('{'):
                continue
            item = json.loads(line)
            if all(key in item for key in ('tick_ms', 'index', 'packet', 'result', *FIELDS)):
                self.rows.append(item)
        if len(self.rows) > 10000:
            raise RuntimeError('fixture trace exceeded bounded test size')

    def mark(self):
        self.refresh()
        return len(self.rows)

    def wait(self, index, predicate=lambda row: True, after=0, timeout=1.5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.refresh()
            for row in self.rows[after:]:
                if row['index'] == index and predicate(row):
                    return row
            time.sleep(0.02)
        raise AssertionError(f'no matching fresh fixture row for index {index}')


def ready(path):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        try:
            return metadata(path)
        except (FileNotFoundError, ValueError):
            time.sleep(0.05)
    raise AssertionError('no live backend-verified fixture metadata')


def run(args, report):
    trace = Trace(args.trace)
    info, token = ready(args.log)
    report['windows_pid'] = info['pid']
    report['protocol'] = info['protocol']
    baseline = {i: trace.wait(i) for i in range(4)}
    client = None
    report['active_case'] = 'authentication'
    bad = bytearray(token)
    bad[0] ^= 255
    with socket.create_connection(('127.0.0.1', info['port']), timeout=2) as probe:
        probe.sendall(bad)
        assert probe.recv(1) == b'', 'invalid authentication was accepted'
    report['cases'].append('authentication')
    try:
        client = Client(args.log)
        report['active_case'] = 'scripted_buttons_axes'
        mark = trace.mark()
        response = client.command('state', index=0, lease=500, **PRESS)
        assert response['controlled'] and response['index'] == 0 and response['lease_ms'] >= 400
        trace.wait(0, lambda row: matches(row, PRESS), mark)
        report['cases'].append('scripted_buttons_axes')

        report['active_case'] = 'malformed_requests'
        for op, index, lease in ((99, 0, 500), (2, 4, 500), (2, 0, 501)):
            client.command('state', index=0, lease=500, **PRESS)
            mark = trace.mark()
            client.socket.sendall(PACKET.pack(op, index, lease, 0x2000, 0, 0, 0, 0, 0, 0))
            try:
                client.read()
            except ValueError:
                pass
            else:
                raise AssertionError('malformed raw request accepted')
            response = client.command('observe')
            assert response['controlled'] and response['index'] == 0
            trace.wait(0, lambda row: matches(row, PRESS), mark)
        report['cases'].append('malformed_requests')

        report['active_case'] = 'held_renewal_and_other_channels'
        client.command('state', index=0, lease=500, **PRESS)
        mark = trace.mark()
        first = trace.wait(0, lambda row: matches(row, PRESS), mark)
        held_last = first
        renewals = 0
        hold_deadline = time.monotonic() + 4
        while held_last['tick_ms'] - first['tick_ms'] < 1200:
            assert time.monotonic() < hold_deadline, 'renewal take exceeded bounded time'
            mark_next = trace.mark()
            renewed = client.command('state', index=0, lease=500, **PRESS)
            assert 400 <= renewed['lease_ms'] <= 500
            row = trace.wait(0, lambda row: matches(row, PRESS), mark_next)
            assert row['packet'] == first['packet'], 'held renewal generated a changed packet'
            held_last = row
            renewals += 1
        trace.refresh()
        held_rows = [row for row in trace.rows[mark:] if row['index'] == 0]
        gaps = [b['tick_ms'] - a['tick_ms'] for a, b in zip(held_rows, held_rows[1:])]
        assert gaps and 0 < max(gaps) <= 200, 'fixture polling too irregular to bound lease timing'
        poll_allowance = max(100, max(gaps)) + 20
        report['renewal_ms'] = held_last['tick_ms'] - first['tick_ms']
        report['renewals'] = renewals
        report['poll_allowance_ms'] = poll_allowance
        for row in trace.rows[mark:]:
            if row['index'] == 0:
                assert matches(row, PRESS) and row['packet'] == first['packet']
            else:
                assert signature(row) == signature(baseline[row['index']]), 'unselected physical channel changed during take'
        report['cases'].append('held_renewal_and_other_channels')

        report['active_case'] = 'neutral_packet'
        mark = trace.mark()
        client.command('neutral')
        neutral = trace.wait(0, lambda row: matches(row, ZERO), mark)
        assert neutral['packet'] != first['packet']
        report['cases'].append('neutral_packet')

        report['active_case'] = 'lease_expiry'
        trace.refresh()
        before_set = next(row for row in reversed(trace.rows) if row['index'] == 0)['tick_ms']
        response = client.command('state', index=0, lease=500, **PRESS)
        assert 400 <= response['lease_ms'] <= 500
        mark = trace.mark()  # first later poll brackets the native reply clock from above
        positive = trace.wait(0, lambda row: matches(row, PRESS), mark)
        remaining = client.command('observe')['lease_ms']
        assert 250 <= remaining <= 500, 'take was too delayed or lease exceeded its ceiling'
        earliest_expiry = positive['tick_ms'] + remaining - 20
        latest_expiry = positive['tick_ms'] + response['lease_ms'] + poll_allowance
        mark = trace.mark()
        expired = trace.wait(0, lambda row: matches(row, ZERO), mark)
        assert expired['tick_ms'] >= earliest_expiry, 'lease cleared before observed remaining time'
        assert expired['tick_ms'] <= latest_expiry, 'lease cleared later than conservative deadline plus measured poll allowance'
        report['expiry_clock'] = dict(before_set_tick=before_set, after_reply_tick=positive['tick_ms'],
                                    bracket_ms=positive['tick_ms'] - before_set,
                                    reply_remaining_ms=response['lease_ms'], lower_bound=earliest_expiry,
                                    upper_bound=latest_expiry, first_neutral_tick=expired['tick_ms'])
        assert expired['packet'] != positive['packet']
        assert client.command('observe')['lease_ms'] == 0
        mark = trace.mark()
        stable = trace.wait(0, lambda row: matches(row, ZERO), mark)
        assert stable['packet'] == expired['packet']
        report['cases'].append('lease_expiry')

        report['active_case'] = 'abrupt_client_loss'
        mark = trace.mark()
        client.command('state', index=0, lease=500, **PRESS)
        positive = trace.wait(0, lambda row: matches(row, PRESS), mark)
        remaining = client.command('observe')['lease_ms']
        assert remaining >= 300, 'take was too delayed to distinguish loss from expiry'
        mark = trace.mark()
        client.socket.close()  # deliberately omit neutral/close protocol
        client = None
        dropped = trace.wait(0, lambda row: matches(row, ZERO), mark, timeout=0.8)
        assert dropped['tick_ms'] < positive['tick_ms'] + remaining - 20, 'loss only cleared at lease expiry'
        assert dropped['packet'] != positive['packet']
        report['cases'].append('abrupt_client_loss')

        client = Client(args.log)
        report['active_case'] = 'pass_through_packet'
        values = dict(PRESS)
        values['buttons'] = 0x1000 if baseline[0]['buttons'] != 0x1000 else 0x2000
        mark = trace.mark()
        client.command('state', index=0, lease=500, **values)
        positive = trace.wait(0, lambda row: matches(row, values), mark)
        mark = trace.mark()
        assert not client.command('pass')['controlled']
        restored = trace.wait(0, lambda row: signature(row) == signature(baseline[0]), mark)
        assert restored['packet'] != positive['packet']
        report['cases'].append('pass_through_packet')

        report['active_case'] = 'second_channel_and_stop'
        mark = trace.mark()
        client.command('state', index=1, lease=500, **PRESS)
        positive = trace.wait(1, lambda row: matches(row, PRESS), mark)
        assert signature(trace.wait(0, after=mark)) == signature(baseline[0])
        mark = trace.mark()
        response = client.command('stop')
        assert response['stopping']
        stopped = trace.wait(1, lambda row: matches(row, ZERO), mark)
        assert stopped['packet'] != positive['packet']
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            complete = Path(args.log).read_text().splitlines()
            if any(json.loads(line).get('event') == 'stopped' for line in complete):
                break
            time.sleep(0.02)
        else:
            raise AssertionError('service stopped record not flushed')
        report['cases'].append('second_channel_and_stop')
        report['active_case'] = None
    finally:
        if client is None:
            try:
                client = Client(args.log)
            except (OSError, ValueError):
                pass
        if client:
            try:
                client.command('stop')
            except (OSError, ValueError):
                pass
            client.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', required=True)
    parser.add_argument('--trace', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    report = dict(status='failed', cases=[], owner_process_cleanup_required=True,
                  scope='production diagnostic API only; no gameplay proof')
    try:
        run(args, report)
        report['status'] = 'passed'
    except (AssertionError, OSError, ValueError, RuntimeError) as error:
        report['error'] = str(error)
    Path(args.out).write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
    raise SystemExit(0 if report['status'] == 'passed' else 1)


if __name__ == '__main__':
    main()
