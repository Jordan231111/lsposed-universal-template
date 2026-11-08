"""Reject invalid split sets and ambiguous signing configuration before producing artifacts."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(os.environ.get("TEMPLATE_TEST_ROOT", Path(__file__).resolve().parents[1]))
SPEC = importlib.util.spec_from_file_location("packaging_tool", ROOT / "tools/package-lspatch.py")
PACKAGING = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGING)


class PackagingTests(unittest.TestCase):
    def test_mixed_or_duplicate_split_sets(self):
        base = ("com.example.target", "1", "")
        for invalid in ([], [base, base], [("com.example.target", "1", "config.a")],
                        [base, ("com.other.target", "1", "config.a")],
                        [base, ("com.example.target", "2", "config.a")],
                        [base, ("com.example.target", "1", "../escape")]):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                PACKAGING.validate_identities(invalid)
        PACKAGING.validate_identities([base, ("com.example.target", "1", "config.arm64_v8a")])

    def test_signing_errors_preserve_existing_output(self):
        env = {key: value for key, value in os.environ.items() if not key.startswith("TEMPLATE_")}
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "existing.apks"
            output.write_bytes(b"previous verified bundle")
            for variables, flags, expected in (
                    ({}, [], "requires all five"),
                    ({"TEMPLATE_KS_PATH": "/missing"}, [], "requires all five"),
                    ({"TEMPLATE_KEY_ALIAS": "alias"}, ["--local-test-signing"], "Do not combine")):
                with self.subTest(variables=variables):
                    result = subprocess.run([sys.executable, str(ROOT / "tools/package-lspatch.py"),
                        "--module", "/missing-module.apk", "--output", str(output), *flags, "/missing-base.apk"],
                        env={**env, **variables}, capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(expected, result.stderr)
                    self.assertEqual(output.read_bytes(), b"previous verified bundle")
