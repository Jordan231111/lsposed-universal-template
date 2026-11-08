"""Compile the actual portable scanner functions with ASan, without an Android runtime.

Source extraction keeps the JNI/linker implementation out of the host build. The scanner function
bodies are compiled verbatim, including when TEMPLATE_TEST_ROOT points to the before snapshot.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get("TEMPLATE_TEST_ROOT", Path(__file__).resolve().parents[1]))


class NativeScannerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.executable = Path(cls.temp.name) / "scanner-checks"
        code = (ROOT / "app/src/main/cpp/native_utils.cpp").read_text()
        boundary = code[code.index("template<auto Function> struct JniBoundary;"):code.index("struct Mapping {")]
        decoders = code[code.index("struct AdrpDecode {"):code.index("std::vector<Mapping> read_maps()")]
        scanners = code[code.index("std::vector<uintptr_t> find_adrp_add_xrefs("):
                        code.index("uintptr_t find_string_va(")]
        patterns = code[code.index("struct Pattern {"):code.index("jlongArray native_find_module(")]
        patterns += code[code.index("uintptr_t pattern_scan("):code.index("bool register_natives(")]
        harness = r'''
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <memory>
#include <iostream>
#include <cstdlib>
#include <string>
#include <stdexcept>
struct JNIEnv {};
using jclass = void*;
'''+ boundary + decoders + scanners + patterns + r'''
int throwing_jni(JNIEnv*, jclass, int) { throw std::runtime_error("native allocation/resolver failure"); }
int returning_jni(JNIEnv*, jclass, int value) { return value; }
int main(int argc, char** argv) {
    std::string test = argv[1];
    if (test == "jni_boundary") {
        return JniBoundary<throwing_jni>::call(nullptr, nullptr, 7) == 0 &&
               JniBoundary<returning_jni>::call(nullptr, nullptr, 7) == 7 ? 0 : 1;
    }
    if (test == "partial_instruction") {
        auto bytes = std::make_unique<uint8_t[]>(5);
        uint32_t adrp = 0x90000000;
        std::memcpy(bytes.get(), &adrp, 4);
        uintptr_t start = reinterpret_cast<uintptr_t>(bytes.get());
        auto hits = find_adrp_add_xrefs(start, start + 5, start & ~uintptr_t(0xFFF), 16);
        return hits.empty() ? 0 : 1;
    }
    if (test == "adjacent_adrp") {
        alignas(4096) uint32_t words[] = {0x90000000, 0x90000001, 0x91000000, 0x91000021};
        uintptr_t start = reinterpret_cast<uintptr_t>(words);
        auto hits = find_adrp_add_xrefs(start, start + sizeof(words), start, 16);
        std::cout << "expected=2 actual=" << hits.size() << '\n';
        return hits.size() == 2 && hits[0] == start && hits[1] == start + 4 ? 0 : 1;
    }
    if (test == "backward_bound") {
        uint32_t words[] = {0xD503233F, 0xD503201F};
        uintptr_t address = reinterpret_cast<uintptr_t>(&words[1]);
        uintptr_t result = find_function_start(address, 1);
        std::cout << "lookback=1 returned_delta=" << (address - result) << '\n';
        return result == 0 ? 0 : 1;
    }
    if (test == "complete_pair") {
        alignas(4096) uint32_t words[] = {0x90000000, 0x91004000};
        uintptr_t start = reinterpret_cast<uintptr_t>(words);
        auto hits = find_adrp_add_xrefs(start, start + sizeof(words), start + 16, 16);
        return hits.size() == 1 && hits[0] == start ? 0 : 1;
    }
    if (test == "patterns") {
        uint8_t bytes[] = {0xAA,0xBB,0xCC,0xDD};
        uintptr_t start = reinterpret_cast<uintptr_t>(bytes);
        if(pattern_scan(start,start+4,"BB ?? DD")!=start+1) return 1;
        if(pattern_scan(start,start+4,"DD")!=start+3) return 1;
        if(pattern_scan(start,start+4,"? ? ? ?")!=start) return 1;
        if(pattern_scan(start,start+4,"AA BB CC DD EE")!=0) return 1;
        for(const char* input : {"", "A", "AZ", "AA B"}) {
            Pattern parsed;
            if(parse_pattern(input,parsed) || pattern_scan(start,start+4,input)!=0) return 1;
        }
        return 0;
    }
    return 2;
}
'''
        path = Path(cls.temp.name) / "scanner-checks.cpp"
        path.write_text(harness)
        result = subprocess.run(["clang++", "-std=c++20", "-fsanitize=address", "-g", str(path),
                                 "-o", str(cls.executable)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)

    def run_case(self, case):
        result = subprocess.run([self.executable, case], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


for case in ("partial_instruction", "adjacent_adrp", "backward_bound", "complete_pair", "patterns", "jni_boundary"):
    setattr(NativeScannerTests, "test_" + case, lambda self, case=case: self.run_case(case))


if __name__ == "__main__":
    unittest.main()
