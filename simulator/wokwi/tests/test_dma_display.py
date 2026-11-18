"""Exercise the real DMA header against an interrupt/ownership protocol double."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class TestDmaDisplay(unittest.TestCase):
    def test_exact_wire_payload_and_failure_recovery(self):
        compiler = shutil.which('g++') or shutil.which('clang++')
        self.assertIsNotNone(compiler, 'Native C++ compiler is required for DMA protocol checks')
        fixture = Path(__file__).parent / 'dma_display/fixture.cpp'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('Arduino.h', 'SPI.h', 'driver/spi_master.h', 'esp_attr.h',
                         'esp_heap_caps.h', 'esp_system.h'):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text('// API declarations are defined in fixture.cpp.\n')
            binary = root / 'dma-test'
            subprocess.run([compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root), str(fixture), '-o', str(binary)],
                           check=True, capture_output=True, text=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn('PASS DMA wire', result.stdout)


if __name__ == '__main__':
    unittest.main()
