import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


class TestPreviewBuildValidation(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('web_run', Path(__file__).parents[1] / 'run.py')
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name)
        self.web = self.repo / 'simulator/web'
        self.build = self.web / '.build'
        self.build.mkdir(parents=True)
        self.source = self.repo / 'firmware/tando_final_demo_15min/Eyes.cpp'
        self.source.parent.mkdir(parents=True)
        self.source.write_text('canonical eye source')
        self.adapter = self.web / 'wrapper.cpp'
        self.adapter.write_text('platform adapter')
        self.shared = self.repo / 'simulator/wokwi/frame_delta.h'
        self.shared.parent.mkdir(parents=True)
        self.shared.write_text('shared renderer helper')
        self.version = self.repo / 'VERSION'
        self.version.write_text('0.10.0-rc.18\n')
        self.wasm = self.build / 'preview.wasm'
        self.wasm.write_bytes(b'\0asm\1\0\0\0')
        self.manifest = {
            'contract': 'tando-web-preview/v1',
            'canonical_sources': {str(self.source.relative_to(self.repo)): self.digest(self.source)},
            'adapter_sources': {'wrapper.cpp': self.digest(self.adapter)},
            'support_sources': {'simulator/wokwi/frame_delta.h': self.digest(self.shared)},
            'version_sha256': self.digest(self.version),
            'wasm_sha256': self.digest(self.wasm),
        }
        self.save()

    @staticmethod
    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def save(self):
        (self.build / 'manifest.json').write_text(json.dumps(self.manifest))

    def validate(self):
        return self.module.validate_build(self.repo, self.web, self.build)

    def test_exact_sources_and_binary_are_accepted(self):
        self.assertEqual(self.validate()['wasm_sha256'], self.manifest['wasm_sha256'])

    def test_editing_cpp_requires_a_rebuild(self):
        self.source.write_text('new eye design')
        with self.assertRaisesRegex(ValueError, 'rebuild'):
            self.validate()

    def test_editing_adapter_requires_a_rebuild(self):
        self.adapter.write_text('new platform adapter')
        with self.assertRaisesRegex(ValueError, 'rebuild'):
            self.validate()

    def test_corrupt_wasm_is_not_served(self):
        self.wasm.write_bytes(b'wrong artifact')
        with self.assertRaisesRegex(ValueError, 'rebuild'):
            self.validate()

    def test_shared_renderer_changes_require_a_rebuild(self):
        self.shared.write_text('new geometry helper')
        with self.assertRaisesRegex(ValueError, 'rebuild'):
            self.validate()

    def test_version_metadata_changes_require_a_rebuild(self):
        self.version.write_text('0.10.0-rc.19\n')
        with self.assertRaisesRegex(ValueError, 'rebuild'):
            self.validate()

    def test_manifest_cannot_read_outside_its_source_root(self):
        self.manifest['canonical_sources'] = {'../../outside.txt': '0' * 64}
        self.save()
        with self.assertRaisesRegex(ValueError, 'path'):
            self.validate()

    def test_missing_or_unknown_manifest_fails_with_a_build_instruction(self):
        self.manifest['contract'] = 'unknown'
        self.save()
        with self.assertRaisesRegex(ValueError, 'rebuild'):
            self.validate()
        (self.build / 'manifest.json').unlink()
        with self.assertRaisesRegex(ValueError, 'build'):
            self.validate()


if __name__ == '__main__':
    unittest.main()
