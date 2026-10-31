#!/usr/bin/env python3
"""Build the browser preview from the canonical C++ firmware, without editing it."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
GFX_FILES = ('Adafruit_GFX.cpp', 'Adafruit_GFX.h', 'glcdfont.c', 'gfxfont.h')
CORE_MODULES = ('Demo', 'CareRequests', 'Reactions', 'Eyes', 'SerialCommands')
OWNER = 'tando-web-preview-build/v1\n'


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def within(path, parent):
    return path == parent or parent in path.parents


def compiler_path(sdk):
    sdk = Path(sdk).expanduser().resolve()
    names = ('clang++.exe', 'clang++') if os.name == 'nt' else ('clang++', 'clang++.exe')
    for name in names:
        compiler = sdk / 'bin' / name
        if compiler.is_file() and (sdk / 'share/wasi-sysroot').is_dir():
            return compiler
    raise ValueError('WASI SDK is incomplete: expected bin/clang++ and share/wasi-sysroot. Use --wasi-sdk.')


def discover_sdk(explicit=None, environ=None, home=None):
    environ = os.environ if environ is None else environ
    home = Path.home() if home is None else Path(home)
    selected = explicit or environ.get('WASI_SDK_PATH')
    if selected:
        sdk = Path(selected).expanduser().resolve()
        compiler_path(sdk)
        return sdk
    sdk = (home / '.wokwi/wasi-sdk').resolve()
    if sdk.is_dir():
        compiler_path(sdk)
        return sdk
    raise ValueError('WASI SDK was not found. Set WASI_SDK_PATH or pass --wasi-sdk; no tools are installed automatically.')


def parse_gfx_hashes(text, label):
    hashes = {}
    for line in text.splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        match = re.fullmatch(r'([0-9a-f]{64})\s+([^\s]+)', line.strip())
        if not match or match[2] not in GFX_FILES or match[2] in hashes:
            raise ValueError('Invalid pinned GFX hash file: ' + str(label))
        hashes[match[2]] = match[1]
    if set(hashes) != set(GFX_FILES):
        raise ValueError('Pinned GFX hash file must contain the four Adafruit GFX sources.')
    return hashes


def read_gfx_hashes(path):
    return parse_gfx_hashes(Path(path).read_text(encoding='utf-8'), path)


def validate_gfx(directory, hash_file):
    directory = Path(directory).expanduser().resolve()
    for name, expected in read_gfx_hashes(hash_file).items():
        source = directory / name
        if not source.is_file() or sha256(source.read_bytes()) != expected:
            raise ValueError('Adafruit GFX 1.13.0 SHA-256 mismatch or missing source: ' + str(source))
    return directory


def discover_gfx(explicit=None, environ=None, repo=REPO, here=HERE, home=None):
    environ = os.environ if environ is None else environ
    home = Path.home() if home is None else Path(home)
    selected = explicit or environ.get('TANDO_WEB_GFX_DIR')
    if selected:
        return validate_gfx(selected, Path(here) / 'gfx.sha256')
    candidates = [Path(repo) / 'simulator/wokwi/.pio/libdeps/tando-s3-sim/Adafruit GFX Library']
    if environ.get('ARDUINO_DIRECTORIES_USER'):
        candidates.append(Path(environ['ARDUINO_DIRECTORIES_USER']) / 'libraries/Adafruit_GFX_Library')
    candidates.append(home / 'Documents/Arduino/libraries/Adafruit_GFX_Library')
    errors = []
    for candidate in candidates:
        if candidate.is_dir():
            try:
                return validate_gfx(candidate, Path(here) / 'gfx.sha256')
            except ValueError as error:
                errors.append(str(error))
    detail = (' ' + errors[0]) if errors else ''
    raise ValueError('Pinned Adafruit GFX 1.13.0 was not found. Pass --gfx-dir or set TANDO_WEB_GFX_DIR.' + detail)


def branch_identity(repo):
    try:
        branch = subprocess.run(['git', 'symbolic-ref', '--quiet', '--short', 'HEAD'],
                                cwd=repo, capture_output=True, text=True, check=False)
        if branch.returncode == 0 and branch.stdout.strip():
            return branch.stdout.strip()
        head = subprocess.run(['git', 'rev-parse', '--verify', 'HEAD'],
                              cwd=repo, capture_output=True, text=True, check=False)
        if head.returncode == 0:
            return 'detached:' + head.stdout.strip()
    except OSError:
        pass
    return None


def canonical_sources(repo):
    firmware = Path(repo) / 'firmware/tando_final_demo_15min'
    files = sorted(path for path in firmware.iterdir()
                   if path.is_file() and path.suffix in ('.ino', '.cpp', '.h')
                   and not path.name.endswith('.ino.cpp'))
    required = {'tando_final_demo_15min.ino'} | {name + '.cpp' for name in CORE_MODULES}
    if not required.issubset({path.name for path in files}):
        raise ValueError('The canonical firmware sketch is incomplete: ' + str(firmware))
    return files


def adapter_sources(here):
    here = Path(here)
    files = [here / 'wrapper.cpp', here / 'build.py', here / 'gfx.sha256']
    files += sorted(path for path in (here / 'adapters').rglob('*') if path.is_file())
    if not (here / 'adapters/dma_display.h').is_file():
        raise ValueError('The web platform adapter is missing.')
    return files


def validate_outputs(build_dir, standalone, repo, here, sdk, gfx):
    repo, here = Path(repo).resolve(), Path(here).resolve()
    generated = here / '.build'
    build_dir = Path(build_dir).expanduser().resolve()
    if within(repo, build_dir) or (within(build_dir, repo) and not within(build_dir, generated)):
        raise ValueError('The build directory must be simulator/web/.build or outside the repository; source output overlap is forbidden.')
    for source in (sdk, gfx):
        if within(build_dir, source) or within(source, build_dir):
            raise ValueError('The build directory must not overlap SDK/library inputs.')
    if standalone is not None:
        standalone = Path(standalone).expanduser().resolve()
        if standalone.suffix.lower() != '.html':
            raise ValueError('The standalone output must have an .html extension.')
        if within(standalone, repo) and not within(standalone, generated):
            raise ValueError('The standalone output must be in simulator/web/.build or outside the repository, never a source asset.')
        if any(within(standalone, source) or within(source, standalone) for source in (sdk, gfx)):
            raise ValueError('The standalone output must not overlap SDK/library inputs.')
    return build_dir, standalone


def own_build_directory(directory):
    marker = directory / '.tando-web-build'
    if directory.exists():
        if not directory.is_dir():
            raise ValueError('The build output is not a directory: ' + str(directory))
        if any(directory.iterdir()) and (not marker.is_file() or marker.read_text() != OWNER):
            raise ValueError('Use an empty or Tando-owned build directory; existing files will not be repurposed.')
    directory.mkdir(parents=True, exist_ok=True)
    marker.write_text(OWNER, encoding='utf-8')


def render_standalone(ui, binary, manifest):
    html = ui['index.html'].decode('utf-8')
    js = ui['app.js'].decode('utf-8')
    css = ui['style.css'].decode('utf-8')
    for name, data, mime in (
        ('preview.wasm', binary, 'application/wasm'),
        ('manifest.json', json.dumps(manifest).encode(), 'application/json'),
    ):
        uri = 'data:' + mime + ';base64,' + base64.b64encode(data).decode()
        js, count = re.subn(r"fetch\((['\"])" + re.escape(name) + r"\1\)",
                            lambda _match: 'fetch(' + json.dumps(uri) + ')', js)
        if count != 1:
            raise ValueError('Standalone export expected exactly one fetch for ' + name)
    html, styles = re.subn(r'<link\b[^>]*\bhref=[\"\']style\.css[\"\'][^>]*>',
                           lambda _match: '<style>' + css + '</style>', html)
    html, scripts = re.subn(r'<script\b[^>]*\bsrc=[\"\']app\.js[\"\'][^>]*>\s*</script>',
                            lambda _match: '<script type="module">' + js + '</script>', html)
    if styles != 1 or scripts != 1:
        raise ValueError('Standalone export could not inline the preview UI assets.')
    return '<!-- Generated Tando web preview; edit canonical C++ and rebuild. -->\n' + html


def publish_pair(staging, build_dir):
    names = ('preview.wasm', 'manifest.json')
    previous = {}
    for name in names:
        target = build_dir / name
        previous[name] = target.is_file()
        if previous[name]:
            shutil.copyfile(target, staging / ('previous-' + name))
    try:
        for name in names:
            os.replace(staging / name, build_dir / name)
    except OSError:
        for name in names:
            if previous[name]:
                os.replace(staging / ('previous-' + name), build_dir / name)
            elif (build_dir / name).exists():
                (build_dir / name).unlink()
        raise


def build_preview(wasi_sdk=None, gfx_dir=None, build_dir=None, standalone=None,
                  repo=REPO, here=HERE, environ=None, runner=None):
    repo, here = Path(repo).resolve(), Path(here).resolve()
    environ = os.environ if environ is None else environ
    runner = subprocess.run if runner is None else runner
    sdk = discover_sdk(wasi_sdk, environ)
    gfx = discover_gfx(gfx_dir, environ, repo, here)
    build_dir, standalone = validate_outputs(build_dir or here / '.build', standalone, repo, here, sdk, gfx)
    canonical = canonical_sources(repo)
    adapters = adapter_sources(here)
    shared = [repo / 'simulator/wokwi' / name for name in ('tracked_canvas.h', 'frame_delta.h')]
    library = [gfx / name for name in GFX_FILES]
    version = repo / 'VERSION'
    inputs = {path: path.read_bytes() for path in canonical + adapters + shared + library + [version]}
    # Validate the immutable bytes actually compiled, not only discovery-time files.
    pins = parse_gfx_hashes(inputs[here / 'gfx.sha256'].decode('utf-8'), here / 'gfx.sha256')
    for source in library:
        if sha256(inputs[source]) != pins[source.name]:
            raise ValueError('Adafruit GFX 1.13.0 SHA-256 mismatch in build snapshot: ' + str(source))
    ui = {name: (here / 'ui' / name).read_bytes() for name in ('index.html', 'app.js', 'style.css')} if standalone else None
    own_build_directory(build_dir)
    with tempfile.TemporaryDirectory(prefix='.building-', dir=str(build_dir)) as temporary:
        staging = Path(temporary)
        for source in canonical + shared:
            target = staging / source.relative_to(repo)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(inputs[source])
        for source in adapters:
            relative = source.relative_to(here)
            target = staging / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(inputs[source])
        dma = staging / 'simulator/wokwi/dma_display.h'
        dma.write_bytes(inputs[here / 'adapters/dma_display.h'])
        for source in library:
            target = staging / 'gfx' / source.name
            target.parent.mkdir(exist_ok=True)
            target.write_bytes(inputs[source])
        compiler = compiler_path(sdk)
        flags = ['-std=c++17', '-O2', '-DARDUINO=10800', '-DTANDO_WOKWI_MODE=1',
                 '-fno-exceptions', '-fno-rtti', '-I', str(staging / 'adapters'),
                 '-I', str(staging), '-I', str(staging / 'gfx')]
        sources = [staging / 'firmware/tando_final_demo_15min' / (name + '.cpp') for name in CORE_MODULES]
        sources += [staging / 'firmware/tando_final_demo_15min/tando_final_demo_15min.ino',
                    staging / 'gfx/Adafruit_GFX.cpp', staging / 'wrapper.cpp']
        objects = []
        for source in sources:
            obj = staging / (source.name + '.o')
            runner([str(compiler), *flags, '-x', 'c++', '-c', str(source), '-o', str(obj)], check=True)
            objects.append(obj)
        binary_path = staging / 'preview.wasm'
        runner([str(compiler), '-mexec-model=reactor', *[str(obj) for obj in objects],
                '-Wl,--export-memory', '-Wl,--initial-memory=16777216',
                '-Wl,--max-memory=67108864', '-o', str(binary_path)], check=True)
        if canonical_sources(repo) != canonical or adapter_sources(here) != adapters or any(path.read_bytes() != data for path, data in inputs.items()):
            raise ValueError('Build inputs changed while compiling; rebuild the current C++ sources. Previous output was retained.')
        if ui and any((here / 'ui' / name).read_bytes() != data for name, data in ui.items()):
            raise ValueError('UI inputs changed while compiling; rebuild before standalone export.')
        binary = binary_path.read_bytes()
        if not binary.startswith(b'\0asm\x01\0\0\0'):
            raise ValueError('The compiler did not produce a WebAssembly module.')
        hashes = {path.relative_to(repo).as_posix(): sha256(inputs[path]) for path in canonical}
        branch = branch_identity(repo)
        identity = 'branch:' + branch if branch else 'snapshot:' + sha256(json.dumps(hashes, sort_keys=True).encode())
        manifest = {
            'contract': 'tando-web-preview/v1', 'firmware_version': inputs[version].decode().strip(),
            'canonical_sources': hashes,
            'compiled_core_sources': [path.relative_to(staging).as_posix() for path in sources[:6]],
            'support_sources': {**{path.relative_to(repo).as_posix(): sha256(inputs[path]) for path in shared},
                                **{'Adafruit_GFX/' + path.name: sha256(inputs[path]) for path in library}},
            'adapter_sources': {path.relative_to(here).as_posix(): sha256(inputs[path]) for path in adapters},
            'version_sha256': sha256(inputs[version]), 'branch': branch or 'snapshot',
            'preview_id': sha256(identity.encode())[:12], 'wasm_sha256': sha256(binary),
            'wasm_bytes': len(binary), 'frame_target_ms': 16,
            'fidelity': 'Shared C++ behavior and graphics with direct RGB565 buffers; no ESP/SPI/electrical simulation.',
        }
        (staging / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        if standalone:
            html = render_standalone(ui, binary, manifest)
            standalone.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', suffix='.html',
                                             prefix='.tando-export-', dir=str(standalone.parent), delete=False) as output:
                output.write(html)
                export_path = Path(output.name)
            try:
                os.replace(export_path, standalone)
            finally:
                if export_path.exists():
                    export_path.unlink()
        publish_pair(staging, build_dir)
    print('Built web preview from {} unchanged canonical files: {}'.format(len(canonical), build_dir / 'preview.wasm'))
    if standalone:
        print('Standalone preview: ' + str(standalone))
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wasi-sdk', type=Path, help='WASI SDK directory (or WASI_SDK_PATH).')
    parser.add_argument('--gfx-dir', type=Path, help='Pinned Adafruit GFX 1.13.0 sources (or TANDO_WEB_GFX_DIR).')
    parser.add_argument('--build-dir', type=Path, help='Generated output directory; default: simulator/web/.build.')
    parser.add_argument('--standalone', type=Path, help='Also export a self-contained HTML file.')
    args = parser.parse_args(argv)
    try:
        build_preview(**vars(args))
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, 'Web preview build failed: {}\n'.format(error))


if __name__ == '__main__':
    main()
