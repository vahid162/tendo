"""Host-only rendering experiments for RC17; never changes firmware or care state."""
from collections import deque
import json
import re
import time
from typing import NamedTuple

from measure_performance import Measurements, UINT32


class Experiment(NamedTuple):
    field: str
    values: tuple
    command: bytes
    fixed_field: str
    fixed_value: int
    schema: str

    def name(self, value):
        return ('BANDS' if value else 'BBOX') if self.field == 'bands' else f'{value} ms'

    def acknowledgement(self, value):
        return (f'SIM: transfer={self.name(value)}' if self.field == 'bands'
                else f'SIM: target_ms={value}')


EXPERIMENTS = {
    'transfer': Experiment('bands', (0, 1), b'o\n', 'target_ms', 60,
                           'tando-transfer-comparison/v1'),
    'frame_target': Experiment('target_ms', (60, 120), b'f\n', 'bands', 1,
                               'tando-frame-target-comparison/v1'),
}


class ComparisonError(RuntimeError):
    pass


def fields(line):
    if not line.startswith('SIMPERF '):
        return None
    values = {k: int(v) for k, v in re.findall(r'\b([a-z_]+)=(\d{1,10})\b', line)}
    required = ('uptime_ms', 'frames', 'bands', 'target_ms', 'cache_l', 'cache_r', 'bg_cache')
    if not all(k in values and 0 <= values[k] < UINT32 for k in required):
        return None
    return values


def workload(lines):
    result = {}
    for line in lines:
        for label in ('Demo started', 'Clock running', 'Stage', 'Sleep Request pending',
                      'Autonomous state', 'Current-stage care mask', 'Progress credits', 'Complete'):
            if line.startswith(label + ': '):
                result[label] = line.split(': ', 1)[1]
        for label in ('Hunger requests', 'PET requests'):
            if line.startswith(label + ': '):
                active = re.search(r'\bactive=(YES|NO)\b', line)
                if active:
                    result[label] = active.group(1)
    return result


class Session:
    def __init__(self, connect, log, clock, experiment='transfer'):
        self.connect, self.log, self.clock = connect, log, clock
        self.experiment = EXPERIMENTS[experiment]
        self.start = clock()
        self.port = None
        self.pending = bytearray()
        self.lines = deque()
        self.last = None
        self.expected_value = None
        self.validate = True
        self.may_have_toggled = False
        self.cleanup_deadline = None

    def open(self):
        self.port = self.connect()
        self.pending.clear()
        self.lines.clear()

    def close(self):
        if self.port is not None:
            port, self.port = self.port, None
            port.close()

    def send(self, command):
        if command not in (b'v\n', b'i\n', self.experiment.command):
            raise ComparisonError('Unsupported diagnostic command')
        if command == self.experiment.command:
            self.may_have_toggled = True  # A failed write may have partially reached Wokwi.
        if self.port.write(command) != len(command):
            raise ComparisonError('Incomplete serial command write')

    def line(self, deadline):
        while self.clock() < deadline:
            if self.lines:
                elapsed, line = self.lines.popleft()
                if self.validate:
                    if line.startswith(('ESP-ROM:', 'TANDO FIRMWARE ')):
                        raise ComparisonError('Wokwi restarted during comparison')
                    value = fields(line)
                    if value:
                        axis = self.experiment
                        if value[axis.fixed_field] != axis.fixed_value or any(value[k] != 1 for k in ('cache_l', 'cache_r', 'bg_cache')):
                            raise ComparisonError(f'Expected {axis.fixed_field}={axis.fixed_value} and all frame caches ON; restart Wokwi')
                        if value[axis.field] not in axis.values:
                            raise ComparisonError(f'Invalid {axis.field}')
                        if self.expected_value is not None and value[axis.field] != self.expected_value:
                            raise ComparisonError(f'{axis.field} changed outside the comparison')
                        if self.last and any((value[k] - self.last[k]) % UINT32 >= UINT32 // 2
                                             for k in ('uptime_ms', 'frames')):
                            raise ComparisonError('Firmware counters reset during comparison')
                        self.last = value
                return elapsed, line
            chunk = self.port.read(max(1, min(self.port.in_waiting, 4096)))
            elapsed = self.clock() - self.start
            self.pending.extend(chunk)
            while b'\n' in self.pending:
                raw, _, self.pending = self.pending.partition(b'\n')
                line = raw.rstrip(b'\r').decode('utf-8', errors='replace')
                self.log.write(json.dumps({'host_elapsed_s': elapsed, 'line': line}) + '\n')
                self.log.flush()
                self.lines.append((elapsed, line))
            if len(self.pending) > 65536:
                raise ComparisonError('Serial line exceeds 64 KiB')
        return None

    def wait(self, predicate, timeout=8):
        deadline = self.clock() + timeout
        if self.cleanup_deadline is not None:
            deadline = min(deadline, self.cleanup_deadline)
        while self.clock() < deadline:
            item = self.line(deadline)
            if item and predicate(item[1]):
                return item
        raise ComparisonError('Timed out waiting for Wokwi acknowledgement')

    def probe(self):
        self.send(b'v\n')
        return self.wait(lambda line: fields(line) is not None)

    def status(self):
        self.send(b'i\n')
        self.wait(lambda line: line == '---------- TANDO STATUS ----------')
        lines = []
        def done(line):
            lines.append(line)
            return line == '----------------------------------'
        self.wait(done)
        value = workload(lines)
        if len(value) != 10:
            raise ComparisonError('Incomplete workload status; comparison stopped')
        return value

    def barrier(self):
        # At most one pending i reply can precede an uncertain toggle: status()
        # is sequential and mode() crosses this barrier before toggling.
        # Two replies put us after that toggle; replies from a failed barrier
        # already follow it. Never infer current
        # mode from an old SIMPERF still waiting in the socket/line queue.
        self.send(b'i\n')
        self.send(b'i\n')
        for _ in range(2):
            self.wait(lambda line: line == '----------------------------------')

    def mode(self, desired):
        self.barrier()
        # Validate queued samples against the old expected mode until the
        # ordered barrier has passed; our own toggle is the only exception.
        self.expected_value = None
        _, line = self.probe()
        axis = self.experiment
        if fields(line)[axis.field] != desired:
            self.send(axis.command)
            self.wait(lambda line: line == axis.acknowledgement(desired))
            self.send(b'v\n')
            self.wait(lambda line: fields(line) is not None and fields(line)[axis.field] == desired)
        self.expected_value = desired
        # Exclude in-flight and first post-switch frames from the measured window.
        start, initial = self.probe()
        initial = fields(initial)
        deadline = self.clock() + 10
        next_query = self.clock() + .5
        while self.clock() < deadline:
            if self.clock() >= next_query:
                self.send(b'v\n')
                next_query = self.clock() + .5
            item = self.line(min(deadline, next_query))
            if item and fields(item[1]):
                current = fields(item[1])
                if item[0] - start >= .5 and (current['frames'] - initial['frames']) % UINT32 >= 2:
                    return
        raise ComparisonError('No frame progress after setting confirmation')

    def restore(self, desired):
        self.validate = False
        self.expected_value = None
        self.cleanup_deadline = self.clock() + 20
        # Reconnect once if the existing socket has failed. Always query first:
        # blindly repeating the toggle could leave the opposite mode selected.
        errors = []
        for attempt in range(2):
            if self.clock() >= self.cleanup_deadline:
                break
            try:
                if attempt or self.port is None:
                    self.close()
                    self.open()
                self.barrier()
                _, line = self.probe()
                axis = self.experiment
                current = fields(line)[axis.field]
                if current not in axis.values:
                    raise ComparisonError(f'Invalid {axis.field} during restoration')
                if current != desired:
                    self.send(axis.command)
                    self.wait(lambda line: line == axis.acknowledgement(desired))
                self.send(b'v\n')
                self.wait(lambda line: fields(line) is not None and fields(line)[axis.field] == desired)
                return {'confirmed': True, axis.field: desired}
            except (OSError, RuntimeError) as exc:
                errors.append(str(exc))
        return {'confirmed': False, self.experiment.field: desired, 'errors': errors,
                'action': 'Restart Wokwi before continuing; original-setting restoration was not confirmed.'}


def compare(connect, phase_seconds, log, clock=time.monotonic, experiment='transfer'):
    """Observe repeated ABBA windows; workload may differ, so never pick a winner."""
    session = Session(connect, log, clock, experiment)
    axis = session.experiment
    initial_key = 'initial_' + axis.field
    result = {
        'schema': axis.schema, 'status': 'aborted', 'phases': [],
        initial_key: None, 'restoration': {'required': False},
        'limitations': [
            'Host receipt timing measures firmware render pairs, not GUI presentation FPS.',
            'Natural animation continues. Repeated ABBA windows do not guarantee identical scenes.',
            'Status does not expose the current reaction; unchanged snapshots are not proof of scene equivalence.',
            'draw_us, spi_us and bytes describe the latest frame, not cumulative interval work.',
        ],
    }
    if experiment == 'frame_target':
        result['limitations'].append(
            'Firmware caps eye-dynamics steps at 100 ms; the 120 ms target can change motion progression and scene content.')
    try:
        session.open()
        _, initial = session.probe()
        result[initial_key] = initial_mode = fields(initial)[axis.field]
        session.expected_value = initial_mode
        opposite = next(value for value in axis.values if value != initial_mode)
        order = [initial_mode, opposite, opposite, initial_mode] * 2
        for index, mode in enumerate(order):
            name = axis.name(mode)
            print(f'COMPARE phase={index + 1}/{len(order)} mode={name}: confirming and warming up', flush=True)
            session.mode(mode)
            before = session.status()
            measurements = Measurements()
            elapsed, line = session.probe()
            measurements.line(line, elapsed)
            phase = {'index': index + 1, axis.field: mode, 'mode': name, 'before': before,
                     'events': [], 'samples': measurements.samples, 'intervals': measurements.intervals}
            result['phases'].append(phase)
            deadline = session.clock() + phase_seconds
            next_query = session.clock() + 1
            while session.clock() < deadline:
                if session.clock() >= next_query:
                    session.send(b'v\n')
                    next_query = session.clock() + 1
                item = session.line(min(deadline, next_query))
                if item:
                    timestamp, text = item
                    measurements.line(text, timestamp)
                    if text.strip() and not text.startswith('SIMPERF '):
                        phase['events'].append({'host_elapsed_s': timestamp, 'line': text})
            phase['summary'] = measurements.summary()
            phase['after'] = session.status()
            phase['workload_transition_detected'] = bool(phase['events']) or before != phase['after']
            print(f"COMPARE phase={index + 1} mode={name} summary={json.dumps(phase['summary'])} "
                  f"workload_transition={phase['workload_transition_detected']}", flush=True)
        result['status'] = 'complete' if all(p['summary']['status'] == 'measured' for p in result['phases']) else 'inconclusive'
    except (OSError, RuntimeError, KeyboardInterrupt) as exc:
        result['error'] = 'Interrupted by user' if isinstance(exc, KeyboardInterrupt) else str(exc)
        print(f"Comparison stopped: {result['error']}", flush=True)
    finally:
        if result[initial_key] is not None and session.may_have_toggled:
            try:
                result['restoration'] = session.restore(result[initial_key])
            except KeyboardInterrupt:
                result['restoration'] = {'confirmed': False, 'action': 'Restart Wokwi; restoration was interrupted.'}
            if not result['restoration']['confirmed']:
                result['status'] = 'restoration_unconfirmed'
                print(result['restoration']['action'], flush=True)
            else:
                print(f'Original {axis.field} restored and confirmed.', flush=True)
        try:
            session.close()
        except OSError as exc:
            result['close_error'] = str(exc)
    return result
