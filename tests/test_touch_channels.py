"""Exercise real Touch.cpp with simulated MPR121 samples, not physical sensitivity."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[1]
FIXTURE = REPO / "tests" / "touch_channels"


def compile_fixture(firmware, executable):
    subprocess.run(
        ["g++", "-std=c++17", "-Wall", "-Wextra", "-I", str(FIXTURE),
         "-I", str(firmware), str(FIXTURE / "fixture.cpp"), "-o", str(executable)],
        check=True, capture_output=True, text=True,
    )


@unittest.skipUnless(shutil.which("g++"), "Native touch checks need g++")
class TouchChannelsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.executable = Path(cls.temp.name) / "touch-channels"
        compile_fixture(REPO / "firmware" / "tando_final_demo_15min", cls.executable)

    def check_case(self, name):
        result = subprocess.run([str(self.executable), name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_all_pairs_require_20_ms_confirmation(self):
        self.check_case("pairs")

    def test_singles_and_unused_channels_do_not_qualify(self):
        self.check_case("single-unused")

    def test_continuous_hold_and_release_rearm(self):
        self.check_case("hold-rearm")

    def test_sleep_and_pending_sleep_suppress_touch(self):
        self.check_case("sleep")

    def test_diagnostics_identify_all_configured_electrodes(self):
        self.check_case("diagnostics")

    def test_i2c_and_measured_startup_settings_are_preserved(self):
        self.check_case("startup")


if __name__ == "__main__":
    unittest.main()
