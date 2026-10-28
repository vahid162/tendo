"""Timestamp Wokwi SIMPERF output on the host through its documented RFC2217 port.

Run with the existing simulator .venv Python; PlatformIO installs pyserial 3.5.
Keep Wokwi visible. This observes ordinary execution and requests a status line
with v every five real seconds. RFC2217 negotiates the virtual serial settings.
"""
import argparse
from datetime import datetime, timezone
import json
import hashlib
from pathlib import Path
import re
import time


UINT32 = 1 << 32


class Measurements:
    def __init__(self):
        self.previous = None
        self.segment = 0
        self.samples = []
        self.intervals = []
        self.buffered_segments = set()

    def disconnect(self):
        self.previous = None
        self.segment += 1

    def line(self, line, elapsed):
        if line.startswith('ESP-ROM:') or line.startswith('TANDO FIRMWARE '):
            self.disconnect()
        if not line.startswith('SIMPERF '):
            return None
        fields = {key: int(value) for key, value in re.findall(r'\b([a-z_]+)=(\d{1,10})\b', line)}
        if not all(key in fields and 0 <= fields[key] < UINT32 for key in ('uptime_ms', 'frames')):
            return None
        sample = {'host_elapsed_s': elapsed, 'segment': self.segment, **fields}
        interval = None
        if self.previous:
            real = elapsed - self.previous['host_elapsed_s']
            virtual = (fields['uptime_ms'] - self.previous['uptime_ms']) % UINT32
            frames = (fields['frames'] - self.previous['frames']) % UINT32
            if virtual >= UINT32 // 2 or frames >= UINT32 // 2:
                self.disconnect()
                sample['segment'] = self.segment
            else:
                buffered = 0 <= real < 0.1 and virtual >= 500
                if buffered:
                    self.buffered_segments.add(self.segment)
                if real <= 0:
                    self.samples.append(sample)
                    self.previous = sample
                    return None
                interval = {
                    'segment': self.segment,
                    'real_s': real,
                    'simulated_s': virtual / 1000,
                    'frame_pairs': frames,
                    'real_pairs_per_s': frames / real,
                    'simulated_time_per_real_s': virtual / 1000 / real,
                    'possible_buffered_delivery': buffered,
                }
                self.intervals.append(interval)
        self.samples.append(sample)
        self.previous = sample
        return interval

    def summary(self):
        # Receipt timing can be distorted by paused/background windows and TCP
        # buffering. Retain all samples and flag implausibly compressed arrivals.
        usable = [i for i in self.intervals if i['segment'] not in self.buffered_segments]
        real = sum(i['real_s'] for i in usable)
        frames = sum(i['frame_pairs'] for i in usable)
        virtual = sum(i['simulated_s'] for i in usable)
        conclusive = real >= 5 and not self.buffered_segments
        return {
            'status': ('inconclusive_buffered_delivery' if self.buffered_segments else
                       'measured' if real >= 5 else 'insufficient_window'),
            'buffered_segments': sorted(self.buffered_segments),
            'usable_intervals': len(usable),
            'real_s': real,
            'frame_pairs': frames,
            'real_pairs_per_s': frames / real if conclusive else None,
            'simulated_time_per_real_s': virtual / real if conclusive else None,
            'boundary': 'Host receipt timing, not GUI presentation timing. Keep Wokwi visible and unpaused.',
        }


def collect(connect, seconds, measurements, log, clock=time.monotonic, sleep=time.sleep):
    start = clock()
    deadline = start + seconds
    pending = bytearray()
    connections = 0
    next_wait_notice = start
    while clock() < deadline:
        try:
            connection = connect()
        except (OSError, RuntimeError) as exc:
            if clock() >= next_wait_notice:
                print(f'Waiting for Wokwi on localhost: {type(exc).__name__}', flush=True)
                next_wait_notice = clock() + 5
            sleep(min(1, max(0, deadline - clock())))
            continue
        connections += 1
        measurements.disconnect()
        pending.clear()
        print('Connected. Keep the Wokwi simulator visible; collecting ordinary execution.', flush=True)
        next_query = clock()
        try:
            with connection:
                while clock() < deadline:
                    if clock() >= next_query:
                        connection.write(b'v\n')
                        next_query = clock() + 5
                    chunk = connection.read(max(1, min(connection.in_waiting, 4096)))
                    if not chunk:
                        continue
                    elapsed = clock() - start
                    pending.extend(chunk)
                    while b'\n' in pending:
                        raw, _, pending = pending.partition(b'\n')
                        line = raw.rstrip(b'\r').decode('utf-8', errors='replace')
                        log.write(json.dumps({'host_elapsed_s': elapsed, 'line': line}) + '\n')
                        log.flush()
                        result = measurements.line(line, elapsed)
                        if result:
                            print('HOSTPERF real_pairs_per_s={:.2f} simulated_clock_ratio={:.3f} real_s={:.2f}{}'.format(
                                result['real_pairs_per_s'], result['simulated_time_per_real_s'],
                                result['real_s'], ' [buffered arrival]' if result['possible_buffered_delivery'] else ''), flush=True)
                    if len(pending) > 65536:
                        raise RuntimeError('Serial line exceeds 64 KiB')
        except (OSError, RuntimeError) as exc:
            print(f'Connection interrupted: {type(exc).__name__}; retrying.', flush=True)
        measurements.disconnect()
        sleep(min(1, max(0, deadline - clock())))
    return connections



def local_build():
    # Files on disk identify the expected build, not the already-running GUI.
    project = Path(__file__).resolve().parent
    version = project.parents[1] / 'VERSION'
    binary = project / '.pio/build/tando-s3-sim/firmware.bin'
    return {'expected_version': version.read_text().strip() if version.is_file() else None,
            'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest() if binary.is_file() else None,
            'running_firmware_identity': 'Verify the boot banner after restarting Wokwi.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=4000)
    parser.add_argument('--seconds', type=float, default=45)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or not 10 <= args.seconds <= 600:
        parser.error('port must be 1..65535 and seconds must be 10..600')
    try:
        import serial
    except ImportError:
        parser.error('Use .venv\\Scripts\\python.exe from the simulator project; its PlatformIO environment includes pyserial.')
    url = f'rfc2217://127.0.0.1:{args.port}?timeout=3'
    def connect():
        # Configure modem-line preferences before opening the simulated port.
        port = serial.serial_for_url(url, baudrate=115200, timeout=0.2, do_not_open=True)
        port.dtr = False
        port.rts = False
        port.open()
        return port
    if args.output is None:
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
        args.output = Path('.tando-performance') / f'host-{stamp}.json'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    log_path = args.output.with_suffix('.jsonl')
    measured = Measurements()
    print(f'Capturing for {args.seconds:g} real seconds. Output: {args.output.resolve()}', flush=True)
    connections = None
    try:
        with log_path.open('w', encoding='utf-8') as log:
            connections = collect(connect, args.seconds, measured, log)
    except KeyboardInterrupt:
        print('Capture stopped; saving available samples.', flush=True)
    result = {
        'schema': 'tando-host-perf/v1',
        'created_utc': datetime.now(timezone.utc).isoformat(),
        'transport': url,
        'connections': connections,
        'local_build': local_build(),
        'summary': measured.summary(),
        'samples': measured.samples,
        'intervals': measured.intervals,
    }
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result['summary'], indent=2), flush=True)
    print(f'Saved {args.output.resolve()} and {log_path.resolve()}', flush=True)
    return 0 if result['summary']['status'] == 'measured' else 2


if __name__ == '__main__':
    raise SystemExit(main())
