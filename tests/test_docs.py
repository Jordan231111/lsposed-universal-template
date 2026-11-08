"""Compile maintained examples and reject broken local documentation links."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from urllib.parse import unquote

ROOT = Path(os.environ.get("TEMPLATE_TEST_ROOT", Path(__file__).resolve().parents[1]))


class DocumentationTests(unittest.TestCase):
    def test_il2cpp_example_compiles_against_production_header(self):
        doc = (ROOT / "docs/ENGINE_IL2CPP.md").read_text()
        blocks = re.findall(r"```cpp\n(.*?)```", doc, re.S)
        self.assertTrue(blocks)
        with tempfile.TemporaryDirectory() as folder:
            for index, code in enumerate(blocks):
                source = Path(folder) / f"example-{index}.cpp"
                source.write_text(code)
                result = subprocess.run(["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                    "-fsyntax-only", "-I", str(ROOT / "app/src/main/cpp"), str(source)],
                    capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_local_markdown_links_exist(self):
        documents = [*ROOT.glob("*.md"), *ROOT.glob("docs/*.md"), ROOT / "tools/README.md", ROOT / "tests/README.md"]
        for document in documents:
            for link in re.findall(r"\]\(([^)]+)\)", document.read_text()):
                if re.match(r"[a-zA-Z]+:", link) or link.startswith("#"):
                    continue
                path = unquote(link.split("#", 1)[0].strip("<>"))
                self.assertTrue((document.parent / path).exists(), f"{document.name}: {link}")


if __name__ == "__main__":
    unittest.main()
