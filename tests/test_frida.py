"""Exercise the actual Frida agent against the current module-instance API contract."""
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(os.environ.get("TEMPLATE_TEST_ROOT", Path(__file__).resolve().parents[1]))


class FridaTests(unittest.TestCase):
    def test_agent_contracts(self):
        result = subprocess.run(["node", str(ROOT / "tests/frida_checks.cjs"),
                                 str(ROOT / "scripts/frida/android_recon.js")],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
