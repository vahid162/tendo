import unittest
import io
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location('measure_performance',
    Path(__file__).resolve().parents[1] / 'measure_performance.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
Measurements, collect = module.Measurements, module.collect


def perf(ms, frames):
    return f'SIMPERF uptime_ms={ms} frames={frames} pair_us=30000 draw_us=20000 spi_us=10000 bytes=0'


class MeasurementTests(unittest.TestCase):
    def test_real_fps_and_clock_ratio_use_host_receipt_time(self):
        m = Measurements()
        self.assertIsNone(m.line(perf(1000, 10), 1))
        value = m.line(perf(4324, 45), 11)
        self.assertEqual(value['real_pairs_per_s'], 3.5)
        self.assertAlmostEqual(value['simulated_time_per_real_s'], .3324)

    def test_reconnect_does_not_count_disconnected_time(self):
        m = Measurements()
        m.line(perf(1000, 10), 1)
        m.disconnect()
        self.assertIsNone(m.line(perf(5000, 60), 50))
        self.assertEqual(m.summary()['usable_intervals'], 0)

    def test_boot_banner_and_unannounced_counter_reset_split_sessions(self):
        m = Measurements()
        m.line(perf(10000, 100), 1)
        m.line('ESP-ROM:esp32s3', 2)
        self.assertIsNone(m.line(perf(1000, 10), 3))
        self.assertIsNone(m.line(perf(500, 3), 4))
        self.assertEqual(m.summary()['usable_intervals'], 0)

    def test_unsigned_clock_rollover_is_not_a_reboot(self):
        m = Measurements()
        m.line(perf((1 << 32) - 500, 100), 1)
        value = m.line(perf(500, 110), 2)
        self.assertEqual(value['simulated_time_per_real_s'], 1)
        self.assertEqual(value['real_pairs_per_s'], 10)

    def test_buffered_output_is_flagged_and_excluded_from_summary(self):
        m = Measurements()
        m.line(perf(1000, 10), 1)
        self.assertTrue(m.line(perf(6000, 80), 1.01)['possible_buffered_delivery'])
        self.assertEqual(m.summary()['usable_intervals'], 0)

    def test_partial_and_invalid_lines_do_not_replace_reference(self):
        m = Measurements()
        m.line(perf(1000, 10), 1)
        self.assertIsNone(m.line('SIMPERF uptime_ms=2000', 2))
        self.assertIsNone(m.line('SIMPERF uptime_ms=3000 frames=9999999999999', 3))
        self.assertEqual(m.line(perf(4000, 40), 4)['real_pairs_per_s'], 10)

    def test_transport_collects_split_lines_and_sends_only_status_query(self):
        class Clock:
            now = 0
            def __call__(self): return self.now
            def sleep(self, seconds): self.now += seconds
        clock = Clock()
        class Connection:
            in_waiting = 0
            closed = False
            writes = []
            chunks = [perf(1000, 10).encode()[:20], perf(1000, 10).encode()[20:] + b'\n',
                      perf(2000, 20).encode() + b'\n']
            def __enter__(self): return self
            def __exit__(self, *args): self.closed = True
            def write(self, data): self.writes.append(data)
            def read(self, count):
                clock.now += 1
                return self.chunks.pop(0) if self.chunks else b''
        port = Connection()
        m = Measurements()
        self.assertEqual(collect(lambda: port, 7, m, io.StringIO(), clock, clock.sleep), 1)
        self.assertTrue(port.closed)
        self.assertEqual(port.writes, [b'v\n', b'v\n'])
        self.assertEqual(len(m.samples), 2)
        self.assertEqual(m.intervals[0]['real_pairs_per_s'], 10)
        self.assertEqual(m.summary()['status'], 'insufficient_window')

    def test_transport_closes_on_keyboard_interrupt(self):
        class Connection:
            in_waiting = 0
            closed = False
            def __enter__(self): return self
            def __exit__(self, *args): self.closed = True
            def write(self, data): pass
            def read(self, count): raise KeyboardInterrupt
        port = Connection()
        with self.assertRaises(KeyboardInterrupt):
            collect(lambda: port, 7, Measurements(), io.StringIO())
        self.assertTrue(port.closed)

    def test_buffered_burst_cannot_produce_a_plausible_incorrect_summary(self):
        m = Measurements()
        m.line(perf(0, 0), 0)
        for index in range(1, 5):
            m.line(perf(index * 5000, index * 50), 20 + index * .001)
        m.line(perf(25000, 250), 25)
        self.assertIsNone(m.summary()['real_pairs_per_s'])
        self.assertIsNone(m.summary()['simulated_time_per_real_s'])

    def test_zero_progress_interval_preserves_paused_real_time(self):
        m = Measurements()
        m.line(perf(1000, 10), 1)
        m.line(perf(1000, 10), 6)
        m.line(perf(2000, 20), 11)
        self.assertEqual(m.summary()['real_pairs_per_s'], 1)
        self.assertEqual(m.summary()['simulated_time_per_real_s'], .1)

    def test_adjacent_auto_and_queried_lines_are_not_backlog(self):
        m = Measurements()
        m.line(perf(1000, 10), 1)
        m.line(perf(1005, 10), 1.005)
        m.line(perf(6000, 60), 6)
        self.assertEqual(m.summary()['real_pairs_per_s'], 10)

    def test_same_receive_timestamp_with_virtual_jump_invalidates_summary(self):
        m = Measurements()
        m.line(perf(1000, 10), 1)
        m.line(perf(6000, 60), 1)
        m.line(perf(11000, 110), 6)
        self.assertIsNone(m.summary()['real_pairs_per_s'])

    def test_tiny_window_then_silence_is_insufficient(self):
        m = Measurements()
        m.line(perf(1000, 10), 1)
        m.line(perf(1005, 10), 1.005)
        self.assertEqual(m.summary()['status'], 'insufficient_window')
        self.assertIsNone(m.summary()['real_pairs_per_s'])


if __name__ == '__main__':
    unittest.main()
