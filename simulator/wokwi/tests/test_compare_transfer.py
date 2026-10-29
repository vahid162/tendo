import contextlib
import io
from pathlib import Path
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT))
try:
    from compare_transfer import compare
finally:
    sys.path.pop(0)


class Clock:
    now = 0.0
    def __call__(self):
        return self.now


class Device:
    """Serial protocol double with a real-time clock independent of firmware time."""
    def __init__(self):
        self.clock = Clock()
        self.bands = 1
        self.target = 60
        self.writes = []
        self.ports = []
        self.on_write = None
        self.on_read = None
        self.silent = False
        self.frames_offset = 0
        self.ms_offset = 10000
        self.pet = False
        self.connection_error = False

    def perf(self):
        return (f'SIMPERF uptime_ms={int(self.clock.now * 250) + self.ms_offset} '
                f'frames={int(self.clock.now * 4) + self.frames_offset + 100} '
                f'bands={self.bands} target_ms={self.target} cache_l=1 cache_r=1 bg_cache=1')

    def status(self):
        return '\n'.join([
            '---------- TANDO STATUS ----------', 'Demo started: NO',
            'Clock running: NO / PAUSED', 'Stage: 1', 'Sleep Request pending: NO',
            'Autonomous state: NONE', 'Current-stage care mask: 0x0',
            'Progress credits: 0 / 9', 'Complete: NO',
            'Hunger requests: stage=1 active=NO',
            f"PET requests: stage=1 active={'YES' if self.pet else 'NO'}",
            '----------------------------------',
        ])

    def connect(self):
        if self.connection_error:
            raise OSError('Cannot connect')
        port = Port(self)
        self.ports.append(port)
        return port


class Port:
    def __init__(self, device):
        self.device = device
        self.pending = bytearray()
        self.closed = False
        self.chunk_size = 4096

    @property
    def in_waiting(self):
        return len(self.pending)

    def emit(self, line):
        self.pending.extend(line.encode() + b'\n')

    def write(self, data):
        d = self.device
        d.writes.append(data)
        if d.on_write and d.on_write(self, data):
            return len(data)
        if not d.silent:
            if data == b'v\n':
                self.emit(d.perf())
            elif data == b'i\n':
                self.emit(d.status())
            elif data == b'o\n':
                d.bands = 1 - d.bands
                self.emit('SIM: transfer=' + ('BANDS' if d.bands else 'BBOX'))
            else:
                raise AssertionError(f'Unexpected command {data!r}')
        return len(data)

    def read(self, count):
        self.device.clock.now += .05
        if self.device.on_read:
            self.device.on_read(self)
        count = min(count, self.chunk_size)
        data = self.pending[:count]
        del self.pending[:count]
        return data

    def close(self):
        self.closed = True


def run(device, seconds=10):
    with contextlib.redirect_stdout(io.StringIO()):
        return compare(device.connect, seconds, io.StringIO(), device.clock)


class ComparisonTests(unittest.TestCase):
    def test_abba_windows_are_measured_separately_and_original_mode_is_restored(self):
        d = Device()
        report = run(d)
        self.assertEqual(report['status'], 'complete')
        self.assertEqual([p['bands'] for p in report['phases']], [1, 0, 0, 1] * 2)
        self.assertEqual(d.bands, 1)
        self.assertTrue(report['restoration']['confirmed'])
        self.assertEqual(set(d.writes), {b'v\n', b'i\n', b'o\n'})
        self.assertTrue(all(p.closed for p in d.ports))
        self.assertTrue(all(p['summary']['real_s'] >= 5 for p in report['phases']))
        self.assertTrue(all(not p['workload_transition_detected'] for p in report['phases']))
        self.assertNotIn('winner', report)

    def test_initial_bbox_is_preserved(self):
        d = Device()
        d.bands = 0
        report = run(d)
        self.assertEqual(report['initial_bands'], 0)
        self.assertEqual([p['bands'] for p in report['phases']], [0, 1, 1, 0] * 2)
        self.assertEqual(d.bands, 0)
        self.assertTrue(report['restoration']['confirmed'])

    def test_no_data_exits_without_toggling(self):
        d = Device()
        d.silent = True
        report = run(d)
        self.assertEqual(report['status'], 'aborted')
        self.assertLess(d.clock.now, 9)
        self.assertNotIn(b'o\n', d.writes)
        self.assertFalse(report['restoration']['required'])

    def test_nondefault_frame_target_is_rejected_without_mutation(self):
        d = Device()
        d.target = 120
        report = run(d)
        self.assertEqual(report['status'], 'aborted')
        self.assertIn('target_ms=60', report['error'])
        self.assertNotIn(b'o\n', d.writes)

    def test_missing_toggle_ack_still_restores_applied_toggle(self):
        d = Device()
        def callback(port, command):
            if command == b'o\n' and d.writes.count(b'o\n') == 1:
                d.bands = 0
                return True
        d.on_write = callback
        report = run(d)
        self.assertEqual(report['status'], 'aborted')
        self.assertTrue(report['restoration']['confirmed'])
        self.assertEqual(d.bands, 1)

    def test_ctrl_c_after_toggle_restores_original_mode(self):
        d = Device()
        interrupted = False
        def callback(port):
            nonlocal interrupted
            if d.bands == 0 and not interrupted:
                interrupted = True
                raise KeyboardInterrupt
        d.on_read = callback
        report = run(d)
        self.assertEqual(report['error'], 'Interrupted by user')
        self.assertTrue(report['restoration']['confirmed'])
        self.assertEqual(d.bands, 1)

    def test_disconnect_reconnects_only_to_restore_not_to_mix_measurements(self):
        d = Device()
        failed = False
        def callback(port):
            nonlocal failed
            if d.bands == 0 and not failed:
                failed = True
                # Fail old socket on every further read, requiring a new one.
                d.on_read = lambda p: (_ for _ in ()).throw(OSError('Disconnected')) if p is port else None
                raise OSError('Disconnected')
        d.on_read = callback
        report = run(d)
        self.assertEqual(report['status'], 'aborted')
        self.assertEqual(len(d.ports), 2)
        self.assertTrue(report['restoration']['confirmed'])
        self.assertEqual(d.bands, 1)
        self.assertEqual(len(report['phases']), 1)

    def test_unconfirmed_restoration_has_explicit_non_success_status(self):
        d = Device()
        def callback(port):
            if d.bands == 0:
                d.connection_error = True
                raise OSError('Disconnected')
        d.on_read = callback
        report = run(d)
        self.assertEqual(report['status'], 'restoration_unconfirmed')
        self.assertFalse(report['restoration']['confirmed'])
        self.assertIn('Restart Wokwi', report['restoration']['action'])

    def test_boot_and_unannounced_counter_reset_abort(self):
        for boot_banner in (True, False):
            with self.subTest(boot_banner=boot_banner):
                d = Device()
                fired = False
                def callback(port, command):
                    nonlocal fired
                    if d.clock.now > 4 and command == b'v\n' and not fired:
                        fired = True
                        if boot_banner:
                            port.emit('ESP-ROM:esp32s3')
                        else:
                            d.ms_offset = 0
                        port.emit(d.perf())
                        return True
                d.on_write = callback
                report = run(d)
                self.assertEqual(report['status'], 'aborted')
                self.assertIn('reset' if not boot_banner else 'restarted', report['error'])

    def test_workload_changes_are_flagged_with_raw_events_and_snapshots(self):
        d = Device()
        def callback(port, command):
            if d.clock.now > 4 and not d.pet:
                d.pet = True
                port.emit('*** PET REQUEST START 1/10 ***')
        d.on_write = callback
        report = run(d)
        self.assertEqual(report['status'], 'complete')
        phase = report['phases'][0]
        self.assertTrue(phase['workload_transition_detected'])
        self.assertEqual(phase['before']['PET requests'], 'NO')
        self.assertEqual(phase['after']['PET requests'], 'YES')
        self.assertTrue(phase['events'])

    def test_mode_change_outside_protocol_aborts(self):
        d = Device()
        def callback(port, command):
            if d.clock.now > 4 and d.writes.count(b'o\n') == 0:
                d.bands = 0
        d.on_write = callback
        report = run(d)
        self.assertEqual(report['status'], 'aborted')
        self.assertIn('outside', report['error'])
        self.assertNotIn(b'o\n', d.writes)  # Never undo a change this tool did not make.

    def test_stale_initial_mode_samples_cannot_falsely_confirm_restoration(self):
        d = Device()
        def callback(port, command):
            if command == b'o\n' and d.writes.count(b'o\n') == 1:
                port.emit(d.perf())
                port.emit(d.perf())  # Both still report initial BANDS.
                d.bands = 0        # The uncertain write actually applied BBOX.
                raise OSError('Write result unknown')
        d.on_write = callback
        report = run(d)
        self.assertEqual(report['status'], 'aborted')
        self.assertTrue(report['restoration']['confirmed'])
        self.assertEqual(d.bands, 1)
        self.assertEqual(d.writes.count(b'o\n'), 2)

    def test_close_failure_does_not_discard_the_report(self):
        d = Device()
        original_connect = d.connect
        def connect():
            port = original_connect()
            def close():
                port.closed = True
                raise OSError('Close failure')
            port.close = close
            return port
        d.connect = connect
        report = run(d)
        self.assertEqual(report['status'], 'complete')
        self.assertTrue(report['restoration']['confirmed'])
        self.assertEqual(report['close_error'], 'Close failure')

    def test_no_frame_progress_is_bounded(self):
        d = Device()
        original_perf = d.perf
        def perf():
            d.frames_offset = -int(d.clock.now * 4)
            return original_perf()
        d.perf = perf
        report = run(d)
        self.assertEqual(report['status'], 'aborted')
        self.assertIn('No frame progress', report['error'])
        self.assertLess(d.clock.now, 12)

    def test_missing_workload_fields_are_rejected_not_called_unchanged(self):
        d = Device()
        original_status = d.status
        d.status = lambda: original_status().replace('Autonomous state: NONE\n', '')
        report = run(d)
        self.assertEqual(report['status'], 'aborted')
        self.assertIn('Incomplete workload', report['error'])

    def test_partial_toggle_write_is_treated_as_possibly_applied(self):
        d = Device()
        original_connect = d.connect
        partial = False
        def connect():
            port = original_connect()
            original_write = port.write
            def write(command):
                nonlocal partial
                if command == b'o\n' and not partial:
                    partial = True
                    original_write(command)
                    return 1
                return original_write(command)
            port.write = write
            return port
        d.connect = connect
        report = run(d)
        self.assertEqual(report['status'], 'aborted')
        self.assertIn('Incomplete serial command', report['error'])
        self.assertEqual(d.bands, 1)
        self.assertTrue(report['restoration']['confirmed'])

    def test_partial_lines_remain_supported(self):
        d = Device()
        connect = d.connect
        def partial_connect():
            port = connect()
            port.chunk_size = 70
            return port
        d.connect = partial_connect
        report = run(d)
        self.assertEqual(report['status'], 'complete')

    def test_buffered_virtual_jump_is_inconclusive_not_a_speedup(self):
        d = Device()
        fired = False
        def callback(port, command):
            nonlocal fired
            if d.clock.now > 4 and command == b'v\n' and not fired:
                fired = True
                port.emit(d.perf())
                d.ms_offset += 1000
                d.frames_offset += 15
                port.emit(d.perf())
                return True
        d.on_write = callback
        report = run(d)
        self.assertEqual(report['status'], 'inconclusive')
        self.assertIsNone(report['phases'][0]['summary']['real_pairs_per_s'])
        self.assertTrue(report['restoration']['confirmed'])


if __name__ == '__main__':
    unittest.main()
