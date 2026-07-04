# RE tools

Reusable reverse-engineering tools for native Android targets. The Python scripts are host-side
static analyzers (no IDA/Ghidra needed); `memtool.c` is a device-side live-memory instrument.

## Static analysis (host) — set `RE_SO` to your target `.so`

```
export RE_SO=/path/to/extracted/lib/arm64-v8a/libyourgame.so
```

| tool | purpose |
|---|---|
| `deep.py dis 0x<rva> <n>` | ARM64 disassembler with LOAD/segment map + ADRP+ADD string comments |
| `deep.py vtbl 0x<rva>` · `vtbl.py 0x<vtRva> [n]` | resolve vtable slots via `.rela.dyn` RELATIVE addends (in-image value is 0; addend is the real RVA) |
| `deep.py u64 0x<rva>` · `deep.py off 0x<va>` | read a qword / map VA↔file-offset |
| `find_str_xref.py 0x<stringVA>` | find the ADRP+ADD site that references a string → its function |
| `rela.py xref 0x<funcRVA>` | reverse lookup: which vtable slot points to a function |
| `rela.py slot 0x<vtRva> <n>` | dump n vtable slots' addends |
| `fstart.py 0x<rva>` | walk back to a function prologue |

Find anchors with `strings -t x $RE_SO | grep …` — RTTI names (`NN<ClassName>`, where NN =
`strlen`), `__PRETTY_FUNCTION__` / assert literals. Those are what the runtime resolvers key on.

## Live memory (device, root) — `memtool.c`

Build once with the NDK and push:
```
$NDK/.../aarch64-linux-android30-clang -O2 memtool.c -o memtool
adb push memtool /data/local/tmp/ && adb shell chmod 755 /data/local/tmp/memtool
```
Run as root: `adb shell su -c "/data/local/tmp/memtool <cmd> <pid> …"`.

| command | purpose |
|---|---|
| `read <pid> <addr> <len>` · `ptrs <pid> <addr> <n>` | hexdump / dump qwords |
| `w32/w64/write <pid> <addr> <hex>` | poke a word / qword / raw bytes |
| `scan <pid> <hexval64> [max]` · `scanr … <lo> <hi>` · `scan32 <pid> <hexval32>` | value scans (find objects/vtables) |
| `sscan <pid> <string>` · `scanm <pid> <val> <mask>` | string / masked scans |
| `snap <pid> <file>` + `hunt <pid> <snap> <old> <new>` | cheat-engine old→new diff |
| `modiff <pid> <vtableAddr> <sec>` | **decrement detector**: snapshot every `MonitoredObfuscated`-style guard, play, report which value dropped (finds *the* counter without knowing its value) |
| `freeze <pid> <addr> <floor> <iters> <us>` | hold a plain-int value |
| `mofreeze <pid> <addr> <val> <iters> <us>` | hold an obfuscated `shadow0^shadow1` value (keeps shadows consistent) |

**Live-test a byte-patch before shipping:** write the instruction to `base+rva` over `/proc/pid/mem`
(COW copies the page; the game keeps running), watch the effect, then bake it into a `CodeFeature`.
`base` for an APK-embedded lib is the `base.apk` `r-xp 00d80000` region start (or the module's
logged `base=`).

See `../docs/NATIVE_MODDING_PLAYBOOK.md` for the end-to-end workflow.
