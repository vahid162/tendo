#!/usr/bin/env python3
"""Build the shared C++ preview and open it on a local web server."""
import argparse
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import sys
import webbrowser


HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def contained(root, relative):
    path = (root / relative).resolve()
    if path == root.resolve() or root.resolve() not in path.parents:
        raise ValueError('Invalid source path in preview manifest')
    return path


def validate_build(repo, web, build):
    """Refuse stale code so a preview cannot silently show an older eye design."""
    try:
        manifest = json.loads((build / 'manifest.json').read_text(encoding='utf-8'))
        if manifest.get('contract') != 'tando-web-preview/v1':
            raise ValueError('Unknown preview format; rebuild with build.py')
        if digest(build / 'preview.wasm') != manifest['wasm_sha256']:
            raise ValueError('Preview binary does not match its manifest; rebuild with build.py')
        if digest(repo / 'VERSION') != manifest['version_sha256']:
            raise ValueError('Version metadata changed; rebuild with build.py')
        for field, root in (('canonical_sources', repo), ('adapter_sources', web)):
            if not isinstance(manifest.get(field), dict) or not manifest[field]:
                raise ValueError('Missing source identity; rebuild with build.py')
            for relative, expected in manifest[field].items():
                path = contained(root, relative)
                if digest(path) != expected:
                    raise ValueError('Source changed: {}; rebuild with build.py'.format(relative))
        support = manifest.get('support_sources', {})
        if not isinstance(support, dict):
            raise ValueError('Invalid shared-source identity; rebuild with build.py')
        for relative, expected in support.items():
            if relative.startswith('simulator/'):
                if digest(contained(repo, relative)) != expected:
                    raise ValueError('Shared source changed: {}; rebuild with build.py'.format(relative))
        canonical = repo / 'firmware/tando_final_demo_15min'
        actual = {p.relative_to(repo).as_posix() for p in canonical.iterdir()
                  if p.suffix in ('.ino', '.cpp', '.h') and not p.name.endswith('.ino.cpp')}
        recorded = {Path(p).as_posix() for p in manifest['canonical_sources']}
        if actual != recorded:
            raise ValueError('Firmware file set changed; rebuild with build.py')
        # A different branch should also get a different browser-storage profile.
        if manifest.get('branch'):
            try:
                top = subprocess.run(['git', '-C', str(repo), 'rev-parse', '--show-toplevel'],
                                     capture_output=True, text=True, check=True)
                if Path(top.stdout.strip()).resolve() == repo.resolve():
                    current = subprocess.run(['git', '-C', str(repo), 'branch', '--show-current'],
                                             capture_output=True, text=True, check=True).stdout.strip()
                    if current and current != manifest['branch']:
                        raise ValueError('Branch changed; rebuild with build.py')
            except (OSError, subprocess.CalledProcessError):
                pass  # Source archives need no Git installation.
        return manifest
    except (OSError, KeyError, json.JSONDecodeError, TypeError) as error:
        raise ValueError('Preview build is missing or invalid; run build.py ({})'.format(error)) from error


def handler_for(repo, web, build):
    class Handler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(web / 'ui'), **kwargs)

        def do_GET(self):
            route = self.path.split('?', 1)[0]
            generated = {'/preview.wasm': ('preview.wasm', 'application/wasm'),
                         '/manifest.json': ('manifest.json', 'application/json')}
            if route in generated:
                try:
                    validate_build(repo, web, build)
                    name, mime = generated[route]
                    data = (build / name).read_bytes()
                except (ValueError, OSError) as error:
                    self.send_error(409, str(error))
                    return
                self.send_response(200)
                self.send_header('Content-Type', mime)
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)
                return
            super().do_GET()

        def end_headers(self):
            self.send_header('Cache-Control', 'no-store')
            super().end_headers()

    return Handler


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8787)
    parser.add_argument('--no-open', action='store_true')
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--wasi-sdk', type=Path)
    parser.add_argument('--gfx-dir', type=Path)
    parser.add_argument('--build-dir', type=Path, default=HERE / '.build')
    args = parser.parse_args(argv)
    if not 0 <= args.port <= 65535:
        parser.error('port must be 0..65535')
    build = args.build_dir.resolve()
    if not args.no_build:
        command = [sys.executable, str(HERE / 'build.py'), '--build-dir', str(build)]
        for flag, value in (('--wasi-sdk', args.wasi_sdk), ('--gfx-dir', args.gfx_dir)):
            if value is not None:
                command.extend([flag, str(value)])
        result = subprocess.run(command)
        if result.returncode:
            return result.returncode
    try:
        validate_build(REPO, HERE, build)
        server = ThreadingHTTPServer(('127.0.0.1', args.port), handler_for(REPO, HERE, build))
    except (ValueError, OSError) as error:
        parser.error(str(error))
    url = 'http://127.0.0.1:{}/'.format(server.server_port)
    print('Tando web preview: {}'.format(url), flush=True)
    print('Keep this terminal open. Ctrl+C stops the server.', flush=True)
    if not args.no_open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
