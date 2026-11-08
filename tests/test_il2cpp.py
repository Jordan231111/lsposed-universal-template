"""Compile the shipped IL2CPP core unchanged against an opaque synthetic runtime."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get("TEMPLATE_TEST_ROOT", Path(__file__).resolve().parents[1]))


class Il2CppTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "il2cpp-checks"
        native = ROOT / "app/src/main/cpp"
        result = subprocess.run(["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-g",
                        "-fsanitize=address,undefined", "-I", str(native),
                        str(ROOT / "tests/cpp/il2cpp_runtime_checks.cpp"),
                        str(native / "il2cpp_runtime.cpp"), "-o", str(cls.binary)],
                       capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)

    def run_case(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


for case in ("missing_api", "readiness", "late_assembly", "assembly_nested", "threads",
             "signatures", "inheritance", "primitive_fields", "reference_fields",
             "static_initializer", "invocation", "roots", "lifetime", "calibration",
             "calibration_failures", "native_abi"):
    setattr(Il2CppTests, "test_" + case, lambda self, case=case: self.run_case(case))
