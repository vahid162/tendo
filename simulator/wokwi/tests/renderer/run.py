"""Compare the real simulator renderer with the canonical full-frame renderer.

Requires the four pinned Adafruit GFX 1.13.0 source files, WASI clang++, and Node22+.
No network access or installer changes. Generated files stay in --build-dir or TEMP.
"""
import argparse
from contextlib import nullcontext
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SIM = HERE.parents[1]
REPO = SIM.parents[1]
FIRMWARE = REPO / 'firmware' / 'tando_final_demo_15min'
FUNCTIONS = """rgb565 blend565 clamp01 smoothStep autonomousLidAmount
petRequestLidAmount sleepRequestLidAmount getRingColor getRingPulseAmount
drawProgressRing drawZSymbol drawSleepGraphics drawSleepRequestGraphics
drawCompletionSparkles drawChickenDrumstick drawHungerFoodOverlay drawPetRequestHeart
drawPetRequestParen drawPetRequestOverlay drawFantasyEye""".split()
GLOBALS = """SCREEN_CX SCREEN_CY EYE_BASE_Y TOTAL_VISUAL_CREDITS RING_PULSE_MS
PET_REQUEST_DURATION_MS leftEye rightEye happyBlend surpriseBlend sleepClose
specialGlow blinkAmount reaction reactionStart completionFlag currentStage
visualCredits ringPulseStart autonomousState autonomousDuration autonomousStart
autonomousWinkLeft autonomousVariant petRequestPromptActive petRequestPromptStart
sleepRequestPending hungerPromptActive hungerPromptStart sleepTagPresent
C_BLACK C_WHITE C_EYE C_EYE_GLOW C_EYE_SHINE C_BLUSH C_RING_TRACK C_RING_S1
C_RING_S2 C_RING_S3 C_SLEEP_Z C_SPARK""".split()


def block(source, pattern, semicolon=False):
    # Preserve offsets while ignoring braces in comments and string literals.
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"',
                    lambda m: re.sub(r'[^\n]', ' ', m[0]), source)
    match = re.search(pattern, masked, re.M)
    if not match:
        raise ValueError(f"Renderer extraction no longer matches: {pattern}")
    pos = masked.index('{', match.start()) + 1
    depth = 1
    while depth and pos < len(masked):
        depth += (masked[pos] == '{') - (masked[pos] == '}')
        pos += 1
    if depth:
        raise ValueError("Unbalanced renderer source")
    if semicolon:
        pos = masked.index(';', pos) + 1
    return source[match.start():pos] + '\n'


def function(source, name):
    return block(source, rf'^[^\n]*\b{re.escape(name)}\([^\n]*\)\s*\{{')


def normalized(source):
    return re.sub(r'\s+', '', re.sub(r'//[^\n]*|/\*[\s\S]*?\*/', '', source))


def select_mode(source, simulator):
    """Select only the explicit simulator branches; preserve normal C++ text."""
    active, stack, output = True, [], []
    for line in source.splitlines(True):
        match = re.match(r'\s*#if\s+(!?)TANDO_WOKWI_MODE\b', line)
        if match:
            branch = bool(simulator) != bool(match[1])
            stack.append((active, branch))
            active = active and branch
        elif stack and re.match(r'\s*#(?:if|ifdef|ifndef|elif)\b', line):
            raise ValueError('Unsupported conditional inside simulator branch')
        elif re.match(r'\s*#else\b', line) and stack:
            parent, branch = stack[-1]
            active = parent and not branch
        elif re.match(r'\s*#endif\b', line) and stack:
            active = stack.pop()[0]
        elif active:
            output.append(line)
    if stack:
        raise ValueError('Unbalanced simulator conditional')
    return ''.join(output)


def generate():
    eyes = (FIRMWARE / 'Eyes.cpp').read_text(encoding='utf-8')
    canonical = select_mode(eyes, False)
    candidate = select_mode(eyes, True)
    config = (FIRMWARE / 'TandoConfig.h').read_text(encoding='utf-8')
    declarations = '\n'.join((
        (FIRMWARE / 'TandoTypes.h').read_text(encoding='utf-8'), config, eyes,
        (FIRMWARE / 'Reactions.cpp').read_text(encoding='utf-8'),
        (FIRMWARE / 'CareRequests.cpp').read_text(encoding='utf-8'),
        (FIRMWARE / 'Demo.cpp').read_text(encoding='utf-8')))
    state = ''
    for name in ('EyeMotion', 'Reaction', 'AutonomousState'):
        pattern = rf'^(?:struct|enum) {name}\b[^{{]*\{{'
        original = block(declarations, pattern, True)
        state += original
    for name in GLOBALS:
        pattern = rf'^(?:static )?(?:const )?(?:float|bool|uint\d+_t|EyeMotion|Reaction|AutonomousState)\s+{name}\b[^\n]*;'
        match = re.search(pattern, declarations, re.M)
        if not match:
            raise ValueError(f"Missing canonical state declaration: {name}")
        state += match[0] + '\n'
    dimensions = ''.join(re.search(rf'^#define {name}\s+[^\n]+', config, re.M)[0] + '\n'
                         for name in ('FRAME_W', 'FRAME_H', 'FRAME_X', 'FRAME_Y'))
    interval = re.search(r'^const uint32_t FRAME_MS\s*=.*;', select_mode(config, True), re.M)
    if not interval:
        raise ValueError('Missing simulator frame interval')
    frames = dimensions + interval[0] + '\n' + candidate[candidate.index('static TandoTrackedCanvas frame'):candidate.index('static uint16_t C_BLACK;')]
    geometry = candidate[candidate.index('struct ProgressRingSegment {'):candidate.index('void initProgressRingGeometry()')]
    current_names = ['initProgressRingGeometry'] + FUNCTIONS + [
        'renderBackgroundRow', 'restoreRenderBackground', 'getRenderBackgroundKey',
        'pushFrame', 'renderDisplay']
    code = geometry + ''.join(function(candidate, name) for name in current_names)
    baseline_names = FUNCTIONS + ['pushLeft', 'pushRight', 'renderDisplay']
    for name in FUNCTIONS + ['renderDisplay']:
        original = function(canonical, name)
        original = re.sub(r'\bframe\b', 'baselineFrame', original)
        for renamed in baseline_names:
            original = re.sub(rf'\b{renamed}\s*\(', f'baseline_{renamed}(', original)
        code += original
    # Count actual ring-background probes without adding firmware diagnostics.
    ring_probe = 'if (background[x] != C_BLACK)'
    if code.count(ring_probe) != 1:
        raise ValueError('Ring overlay probe changed; review performance fixture')
    code = code.replace(ring_probe, 'if ((++ringOverlayChecks, background[x] != C_BLACK))')
    palette = '\n'.join(re.findall(r'^  C_\w+\s*=.*;', function(canonical, 'setupEyes'), re.M))
    if len(palette.splitlines()) != 12:
        raise ValueError("Canonical palette changed; review renderer fixture")
    actual_palette = '\n'.join(re.findall(r'^  C_\w+\s*=.*;', function(candidate, 'setupEyes'), re.M))
    if normalized(palette) != normalized(actual_palette):
        raise ValueError("Simulator palette changed; review renderer corpus")
    harness = (HERE / 'harness.cpp').read_text(encoding='utf-8')
    for marker, value in [('FRAME_GLOBALS', frames), ('STATE', state), ('FUNCTIONS', code), ('PALETTE', palette)]:
        harness = harness.replace(f'/*{marker}*/', value)
    return harness


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gfx-dir', type=Path, required=True)
    parser.add_argument('--native', action='store_true', help='Run host C++ pixel checks instead of WASM')
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--node', default='node')
    parser.add_argument('--build-dir', type=Path)
    args = parser.parse_args()
    compiler = args.compiler or (Path(shutil.which('g++') or 'g++') if args.native else
        Path(os.environ.get('WASI_SDK_PATH', str(Path.home() / '.wokwi/wasi-sdk'))) /
        'bin' / ('clang++.exe' if os.name == 'nt' else 'clang++'))
    for line in (HERE / 'gfx.sha256').read_text().splitlines():
        if not line or line.startswith('#'):
            continue
        digest, name = line.split()
        if not (args.gfx_dir / name).is_file():
            parser.error(f"Missing Adafruit GFX source: {name}")
        if hashlib.sha256((args.gfx_dir / name).read_bytes()).hexdigest() != digest:
            parser.error(f"Adafruit GFX 1.13.0 SHA-256 mismatch: {name}")
    manager = nullcontext(args.build_dir.resolve()) if args.build_dir else tempfile.TemporaryDirectory(prefix='tando-renderer-')
    with manager as directory:
        output = Path(directory)
        output.mkdir(parents=True, exist_ok=True)
        environment = os.environ.copy()
        if os.name == 'nt':
            environment.update(TEMP=str(output), TMP=str(output))
        (output / 'renderer.cpp').write_text(generate(), encoding='utf-8')
        for name in ('Adafruit_GFX.cpp', 'Adafruit_GFX.h', 'glcdfont.c', 'gfxfont.h'):
            shutil.copyfile(args.gfx_dir / name, output / name)
        common = [str(compiler), '-O2', '-fno-exceptions', '-std=c++17', '-DARDUINO=10800', '-I', str(HERE / 'stubs'), '-I', str(SIM), '-I', str(output)]
        for source in ('renderer.cpp', 'Adafruit_GFX.cpp'):
            subprocess.run(common + ['-c', str(output / source), '-o', str(output / (source + '.o'))], check=True, env=environment)
        executable = output / ('renderer-native' + ('.exe' if os.name == 'nt' else '') if args.native else 'renderer.wasm')
        subprocess.run([str(compiler), str(output / 'renderer.cpp.o'), str(output / 'Adafruit_GFX.cpp.o'), '-o', str(executable)], check=True, env=environment)
        command = [str(executable)] if args.native else [args.node, '--no-warnings', str(HERE / 'run.mjs'), str(executable)]
        subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
