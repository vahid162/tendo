import hashlib
import re
import subprocess
import unittest
from pathlib import Path


class TestBootstrap(unittest.TestCase):
    def test_documented_commands_match_the_committed_installer(self):
        repo = Path(__file__).resolve().parents[3]
        commands = []
        for path in (repo / 'README.md', repo / 'simulator/wokwi/README.md'):
            blocks = re.findall(r'```powershell\s*\n(.*?)\n```', path.read_text(encoding='utf-8'), re.S)
            commands.append(next(block for block in blocks if 'raw.githubusercontent.com/vahid162/tendo/' in block))
        self.assertEqual(commands[0], commands[1])
        command = commands[0]
        self.assertRegex(command, r'https://raw\.githubusercontent\.com/vahid162/tendo/[0-9a-f]{40}/simulator/wokwi/install\.ps1')
        self.assertNotRegex(command, r'(?i)Invoke-Expression|\biex\b|Set-ExecutionPolicy')
        blob = subprocess.check_output(['git', 'show', 'HEAD:simulator/wokwi/install.ps1'], cwd=repo)
        digest = hashlib.sha256(blob).hexdigest()
        self.assertIn(f".Hash -ne '{digest}'", command)
        self.assertLess(command.index('Get-FileHash'), command.index('& powershell.exe'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
