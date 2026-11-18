import base64
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from unittest.mock import patch


HERE = Path(__file__).resolve().parents[1]


class BuilderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location('web_builder', HERE / 'build.py')
        cls.builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.builder)

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.repo = self.root / 'repo'
        self.web = self.repo / 'simulator/web'
        self.web.mkdir(parents=True)
        self.sdk = self.root / 'sdk'
        (self.sdk / 'bin').mkdir(parents=True)
        (self.sdk / 'share/wasi-sysroot').mkdir(parents=True)
        (self.sdk / 'bin/clang++').write_text('compiler')
        self.gfx = self.root / 'gfx'
        self.gfx.mkdir()
        hashes = []
        for name in self.builder.GFX_FILES:
            content = ('pinned:' + name).encode()
            (self.gfx / name).write_bytes(content)
            hashes.append(hashlib.sha256(content).hexdigest() + '  ' + name)
        (self.web / 'gfx.sha256').write_text('\n'.join(hashes) + '\n')
        (self.repo / 'VERSION').write_text('0.10.0-rc.18\n')
        self.firmware = self.repo / 'firmware/tando_final_demo_15min'
        self.firmware.mkdir(parents=True)
        names = ['tando_final_demo_15min.ino']
        for name in ('Demo', 'CareRequests', 'Reactions', 'Eyes', 'SerialCommands', 'Touch', 'Rfid'):
            names.extend((name + '.cpp', name + '.h'))
        names.extend(('TandoConfig.h', 'TandoTypes.h'))
        for name in names:
            (self.firmware / name).write_text('// canonical ' + name + '\n')
        support = self.repo / 'simulator/wokwi'
        support.mkdir()
        for name in ('tracked_canvas.h', 'frame_delta.h'):
            (support / name).write_text('// shared ' + name)
        (self.web / 'adapters').mkdir()
        (self.web / 'adapters/dma_display.h').write_text('// platform adapter')
        (self.web / 'adapters/Arduino.h').write_text('// platform Arduino')
        (self.web / 'wrapper.cpp').write_text('// adapter wrapper')
        (self.web / 'build.py').write_text('// builder fixture')
        (self.web / 'ui').mkdir()
        (self.web / 'ui/index.html').write_text('<link rel="stylesheet" href="style.css"><script type="module" src="app.js"></script>')
        (self.web / 'ui/style.css').write_text('body { color: cyan; }')
        (self.web / 'ui/app.js').write_text("const wasm = fetch('preview.wasm'); const meta = fetch('manifest.json');")
        self.calls = []
        self.compiler_inputs = {}

    def fake_compiler(self, command, **kwargs):
        self.calls.append(command)
        if '-c' in command:
            source = Path(command[command.index('-c') + 1])
            if source.parent.name == 'tando_final_demo_15min':
                for staged in source.parent.iterdir():
                    self.compiler_inputs[staged.name] = staged.read_bytes()
        output = Path(command[command.index('-o') + 1])
        output.write_bytes(b'\0asm\x01\0\0\0' if output.suffix == '.wasm' else b'object')
        return subprocess.CompletedProcess(command, 0)

    def build(self, **kwargs):
        with patch.object(self.builder, 'branch_identity', return_value='experiment/eyes-blink'):
            return self.builder.build_preview(repo=self.repo, here=self.web,
                wasi_sdk=self.sdk, gfx_dir=self.gfx, environ={},
                runner=kwargs.pop('runner', self.fake_compiler), **kwargs)

    def test_sdk_environment_home_and_windows_compiler_discovery(self):
        self.assertEqual(self.builder.discover_sdk(None, {'WASI_SDK_PATH': str(self.sdk)}, self.root), self.sdk)
        (self.sdk / 'bin/clang++').rename(self.sdk / 'bin/clang++.exe')
        self.assertEqual(self.builder.compiler_path(self.sdk).name, 'clang++.exe')
        home = self.root / 'home'
        installed = home / '.wokwi/wasi-sdk'
        installed.parent.mkdir(parents=True)
        self.sdk.rename(installed)
        self.assertEqual(self.builder.discover_sdk(None, {}, home), installed)

    def test_invalid_explicit_sdk_does_not_silently_select_a_different_compiler(self):
        with self.assertRaisesRegex(ValueError, 'WASI'):
            self.builder.discover_sdk(self.root / 'missing', {'WASI_SDK_PATH': str(self.sdk)}, self.root)

    def test_gfx_tampering_is_rejected_before_compile(self):
        (self.gfx / 'gfxfont.h').write_text('wrong release')
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            self.build()
        self.assertFalse(self.calls)
        self.assertFalse((self.web / '.build/preview.wasm').exists())

    def test_gfx_changed_after_discovery_is_rejected_before_compile(self):
        discover = self.builder.discover_gfx
        def changed(*args, **kwargs):
            result = discover(*args, **kwargs)
            (self.gfx / 'Adafruit_GFX.cpp').write_text('changed after validated discovery')
            return result
        with patch.object(self.builder, 'discover_gfx', side_effect=changed):
            with self.assertRaisesRegex(ValueError, 'SHA-256'):
                self.build()
        self.assertFalse(self.calls)

    def test_output_cannot_overlap_canonical_or_wokwi_or_ui_sources(self):
        for directory in (self.repo, self.firmware / 'build', self.repo / 'simulator/wokwi/build', self.web / 'ui/build'):
            with self.subTest(directory=directory):
                with self.assertRaisesRegex(ValueError, 'output|Output|build directory'):
                    self.build(build_dir=directory)
        with self.assertRaisesRegex(ValueError, 'output|Output|standalone'):
            self.build(standalone=self.web / 'ui/index.html')

    def test_manifest_tracks_exact_sources_and_ignores_temporary_arduino_conversion(self):
        generated = self.firmware / 'tando_final_demo_15min.ino.cpp'
        generated.write_text('// transient PIO conversion')
        before = {p: p.read_bytes() for p in self.repo.rglob('*') if p.is_file()}
        manifest = self.build()
        self.assertEqual(manifest['contract'], 'tando-web-preview/v1')
        self.assertEqual(len(manifest['canonical_sources']), 17)
        self.assertEqual(len(manifest['compiled_core_sources']), 6)
        self.assertNotIn(str(generated.relative_to(self.repo)), manifest['canonical_sources'])
        self.assertEqual(manifest['branch'], 'experiment/eyes-blink')
        self.assertEqual(manifest['preview_id'], hashlib.sha256(b'branch:experiment/eyes-blink').hexdigest()[:12])
        for relative, digest in manifest['canonical_sources'].items():
            self.assertEqual(digest, hashlib.sha256((self.repo / relative).read_bytes()).hexdigest())
            source = self.repo / relative
            self.assertEqual(self.compiler_inputs[source.name], source.read_bytes(),
                             'The compiler must receive unchanged canonical bytes: ' + source.name)
        self.assertEqual(len(self.compiler_inputs), 17)
        for path, content in before.items():
            self.assertEqual(path.read_bytes(), content, str(path))
        self.assertEqual(len(self.calls), 9)  # Five core modules, entry, GFX, wrapper, link.
        self.assertFalse(list((self.web / '.build').glob('.building-*')))

    def test_failed_compile_keeps_previous_working_pair(self):
        self.build()
        build = self.web / '.build'
        before = {name: (build / name).read_bytes() for name in ('preview.wasm', 'manifest.json')}
        def fail(command, **kwargs):
            raise subprocess.CalledProcessError(1, command)
        with self.assertRaises(subprocess.CalledProcessError):
            self.build(runner=fail)
        self.assertEqual({name: (build / name).read_bytes() for name in before}, before)
        self.assertFalse(list(build.glob('.building-*')))

    def test_changed_input_during_compile_is_not_published(self):
        self.build()
        build = self.web / '.build'
        before = (build / 'manifest.json').read_bytes()
        changed = False
        def mutate(command, **kwargs):
            nonlocal changed
            result = self.fake_compiler(command, **kwargs)
            if not changed:
                changed = True
                (self.firmware / 'Eyes.cpp').write_text('// user edited while building')
            return result
        with self.assertRaisesRegex(ValueError, 'changed'):
            self.build(runner=mutate)
        self.assertEqual((build / 'manifest.json').read_bytes(), before)

    def test_adapter_change_during_compile_is_not_published(self):
        def mutate(command, **kwargs):
            result = self.fake_compiler(command, **kwargs)
            (self.web / 'wrapper.cpp').write_text('// adapter edited while building')
            return result
        with self.assertRaisesRegex(ValueError, 'changed'):
            self.build(runner=mutate)
        self.assertFalse((self.web / '.build/preview.wasm').exists())

    def test_nonempty_unowned_output_directory_is_not_repurposed(self):
        build = self.root / 'other-output'
        build.mkdir()
        (build / 'user-file.txt').write_text('keep me')
        with self.assertRaisesRegex(ValueError, 'owned|empty'):
            self.build(build_dir=build)
        self.assertEqual((build / 'user-file.txt').read_text(), 'keep me')

    def test_standalone_contains_exact_wasm_without_external_asset_dependencies(self):
        output = self.root / 'preview.html'
        manifest = self.build(standalone=output)
        html = output.read_text()
        embedded = re.search(r'data:application/wasm;base64,([A-Za-z0-9+/=]+)', html)[1]
        binary = base64.b64decode(embedded)
        self.assertEqual(hashlib.sha256(binary).hexdigest(), manifest['wasm_sha256'])
        self.assertNotIn('src="app.js"', html)
        self.assertNotIn('href="style.css"', html)
        self.assertIn('data:application/json;base64,', html)
        self.assertIn('color: cyan', html)


if __name__ == '__main__':
    unittest.main()
