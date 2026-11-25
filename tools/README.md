# Template tools

Run the tools from the repository root.

## LSPatch download (host)

```sh
LSPATCH_JAR="$(./tools/download-lspatch.sh)"
java -jar "$LSPATCH_JAR" --help
```

The downloader verifies fresh and cached JARs by SHA-256 and prints the absolute path to stdout.
The default cache is `artifacts/lspatch/`. Use `--debug` for the debug JAR or pass an output
directory after the optional flag. Remove a cached JAR that fails verification before retrying.
The [bundle packager](../docs/LSPATCH_NONROOT.md) calls the downloader automatically.

## Configure a module

The [configure command](../README.md#configure-and-build) updates module sources, metadata and
package references in the README and guides. It checks inputs and destination paths before writing.
An I/O failure during the write phase can still require restoring your checkout.

`--name` and `--author` must be nonempty single-line text. Names are escaped separately for Java,
Android XML and module metadata; Gradle's internal project name uses the Java package, so display
punctuation does not make an invalid build identifier. On subsequent runs, the current namespace
and native library name are detected from the source. `--old-package` explicitly checks the expected namespace;
`--native-lib` renames the CMake target and Java library constant together. The source file keeps
its name, `template_native.cpp`.

## Static analysis (host)

Only `deep.py dis` requires the Capstone Python package.
Set `RE_SO` to the input file.

```sh
export RE_SO="/path/to/lib/arm64-v8a/libtarget.so"
```

All commands below are run as `python3 tools/<script> ...`. Supported inputs are little-endian
ELF64 AArch64 executables/shared libraries. Addresses are **ELF virtual addresses**, before load
bias; `strings -t x` reports file offsets and must be converted with `deep.py va` first.

| Command | Result |
|---|---|
| `deep.py dis 0x<va> [count]` | Bounded ARM64 disassembly with ADRP/address annotations |
| `deep.py vtbl 0x<va> [count]` / `vtbl.py 0x<va> [count]` | Vtable slots from ordinary `.rela.dyn` RELATIVE addends |
| `deep.py u64 0x<va>` | Raw 8-byte file-backed value; does not apply relocations |
| `deep.py off 0x<va>` / `deep.py va 0x<offset>` | VA → file offset / file offset → VA |
| `find_str_xref.py 0x<string-va>` | ADRP+ADD/LDR candidates in executable LOAD segments |
| `fstart.py 0x<instruction-va>` | Prologue candidate within the executable segment and a 4 KiB backward bound |
| `rela.py xref 0x<function-va>` | RELATIVE relocation slots whose addend matches the address |
| `rela.py slot 0x<slot-va> [count]` / `rela.py read 0x<slot-va>` | Multiple/single RELATIVE relocation entries |

The tools share `elf_utils.py`; file-backed bounds are checked for the entire read. Counts must
be 1–4096. Relocation commands do not decode RELR, Android packed or symbol-based relocations.
An absent RELATIVE entry is reported explicitly and does not prove that the runtime pointer is zero.
Xrefs/prologues are heuristics: verify candidates against disassembly and the target ABI.

For a running test app, see the [Frida agent guide](../docs/FRIDA_EMULATOR_QUICKSTART.md).

Packaging tools read the [shared Android signing profile](../SIGNING.md).
