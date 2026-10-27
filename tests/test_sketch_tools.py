import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile


REPO = Path(__file__).resolve().parents[1]
HELPER = REPO / "scripts" / "prepare_sketch.py"
SKETCH = "tando_final_demo_15min"


class SketchToolsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.files = {
            f"{SKETCH}.ino": '#include "Touch.h"\nvoid setup() {}\nvoid loop() {}\n',
            "Touch.h": "void updateTouch();\n",
            "Touch.cpp": '#include "Touch.h"\nvoid updateTouch() {}\n',
            "VERSION": "0.10.0-rc.9\n",
            "README.md": "Open the complete sketch folder.\n",
        }
        for name, content in self.files.items():
            (self.source / name).write_text(content)
        (self.source / "old-release.ino").write_text("not part of the sketch\n")
        self.destination = self.root / "output" / SKETCH

    def run_helper(self, command, destination, success=True):
        result = subprocess.run(
            ["python3", str(HELPER), command, str(self.source), str(destination)],
            capture_output=True,
            text=True,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        return result

    def test_stage_copies_every_source_and_removes_stale_sources(self):
        self.run_helper("stage", self.destination)
        for name, content in self.files.items():
            self.assertEqual((self.destination / name).read_text(), content)
        self.assertFalse((self.destination / "old-release.ino").exists())
        (self.destination / "removed.cpp").write_text("stale definition")
        (self.destination / "notes.txt").write_text("keep user notes")
        (self.source / "Touch.cpp").unlink()
        self.run_helper("stage", self.destination)
        self.assertFalse((self.destination / "removed.cpp").exists())
        self.assertFalse((self.destination / "Touch.cpp").exists())
        self.assertEqual((self.destination / "notes.txt").read_text(), "keep user notes")

    def test_package_contains_a_complete_matching_sketch(self):
        output = self.root / "tando-sketch.zip"
        self.run_helper("package", output)
        with zipfile.ZipFile(output) as archive:
            self.assertEqual(set(archive.namelist()), {f"{SKETCH}/{name}" for name in self.files})
            for name, content in self.files.items():
                self.assertEqual(archive.read(f"{SKETCH}/{name}").decode(), content)

    def test_stage_rejects_output_that_overlaps_source(self):
        for destination in (self.source, self.source / SKETCH, self.root):
            self.run_helper("stage", destination, success=False)
        for name, content in self.files.items():
            self.assertEqual((self.source / name).read_text(), content)

    def test_missing_primary_sketch_leaves_existing_output_untouched(self):
        self.destination.mkdir(parents=True)
        marker = self.destination / "Touch.cpp"
        marker.write_text("existing output")
        (self.source / f"{SKETCH}.ino").unlink()
        self.run_helper("stage", self.destination, success=False)
        self.assertEqual(marker.read_text(), "existing output")

    def test_package_rejects_overwriting_a_source_file(self):
        self.run_helper("package", self.source / "Touch.cpp", success=False)
        self.assertEqual((self.source / "Touch.cpp").read_text(), self.files["Touch.cpp"])

    def test_build_script_works_from_another_directory_and_passes_failures_through(self):
        fake_cli = self.root / "fake arduino-cli"
        fake_cli.write_text('#!/bin/sh\nexit 37\n')
        fake_cli.chmod(0o755)
        build_root = self.root / "build output"
        result = subprocess.run(
            ["bash", str(REPO / "scripts" / "build.sh")],
            cwd=self.root,
            env={**os.environ, "TANDO_BUILD_ROOT": str(build_root), "TANDO_ARDUINO_CLI": str(fake_cli)},
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 37, result.stderr)
        self.assertTrue((build_root / "sketch" / SKETCH / f"{SKETCH}.ino").is_file())


if __name__ == "__main__":
    unittest.main()
