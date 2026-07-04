# Native modding playbook (works when there's no `dump.cs`)

This is the reusable methodology + toolkit this template ships for modding **native / C++ Android
games** — the hard case where you can't just dump IL2CPP metadata. It was proven end-to-end on a
hardened King title (moves-freeze, free boosters, anti-cheat bypass, all auto-resolving). For
managed engines see `docs/ENGINE_IL2CPP.md` and `docs/ENGINE_GODOT_LUA_COCOS.md` — but even those
fall back to the native toolkit here when symbols are stripped.

## The core idea: resolve by a *stable anchor*, then *patch the producer*

Two principles do most of the work:

1. **Never hardcode an offset.** App updates and even rebuilds move every address. Anchor on
   something that *doesn't* move — a **string** the function references, or an **RTTI class name** —
   and walk to the function at runtime. Then guard the patch by the expected opcode so a shifted
   anchor fails **closed** (feature disables) instead of corrupting code.
2. **Patch the producer, not the runtime value.** Chasing a live value is a trap when it's
   obfuscated, guarded, server-synced, or reallocated each update (see the moves saga in
   `WHY_THIS_WAS_HARD.md`). Find the *function that changes it* and neutralize that. One reliable
   line beats a fragile heap-scanning thread.

## Workflow

### 1. Get the target `.so`
`extractNativeLibs=false` games map the lib from **inside `base.apk`** (it shows as `base.apk`
`r-xp` regions in `/proc/pid/maps`, not `lib….so`). Pull and extract it:
```
adb shell su -c 'cp /data/app/~~*/<pkg>-*/base.apk /data/local/tmp/b.apk'
adb pull /data/local/tmp/b.apk && unzip -o b.apk 'lib/arm64-v8a/*.so' -d extracted
export RE_SO=$PWD/extracted/lib/arm64-v8a/libyourgame.so
```

### 2. Static RE (host) — find anchors + offsets
The `tools/` Python scripts read any ELF via `RE_SO`:
- `deep.py dis 0x<rva> <n>` — ARM64 disassembler with LOAD/segment map, ADRP+ADD string comments.
- `deep.py vtbl 0x<rva>` / `vtbl.py` — resolve vtable slots via `.rela.dyn` addends (in-image value
  is 0; the addend is the real RVA).
- `find_str_xref.py 0x<stringVA>` — find the ADRP+ADD site that references a string → its function.
- `rela.py xref 0x<funcRVA>` — reverse: which vtable slot points to a function.
- `strings -t x $RE_SO | grep …` — RTTI names (`NN<ClassName>`), `__PRETTY_FUNCTION__`/assert
  literals — your anchors.

Typical hunt: find the class by RTTI name → its vtable → the method slot; **or** find a unique
assert string a function prints → its containing function.

### 3. Live probing (device, root) — `tools/memtool.c`
Build once with the NDK and push (`aarch64-linux-android30-clang -O2 memtool.c -o memtool`). It's a
`/proc/pid/mem` instrument ("Frida without Frida") — read/write/scan/diff/freeze live memory to
*confirm* a theory before you ship a patch:
- `read/w32/w64/write` — peek/poke.
- `scan/scanr/scan32/sscan/scanm` — value/string/masked scans (find objects, vtables, values).
- `snap` + `hunt` — cheat-engine style old→new diff.
- `modiff <vtableAddr> <sec>` — **decrement detector**: snapshot every `MonitoredObfuscated<int>`-
  style guard, wait while you play, report which value dropped (finds *the* counter without knowing
  its value).
- `freeze` / `mofreeze` — hold a plain / obfuscated (`shadow0^shadow1`) value.

Live-test a byte-patch by writing the instruction to `base+rva` over `/proc/pid/mem` (COW) and
watching the game *before* baking it into the module.

### 4. Bake it in — auto-resolving, toggleable (see `template_native.cpp`)
The native core gives you three toolkits:

- **Patcher.** `write_code(addr,word)` (mprotect→memcpy→i-cache flush) and guarded
  `patch_instruction(addr,expected,replacement)`. ARM64 encoders: `MOV_W0_1` (return true),
  `MOV_W0_0` (return false), `A64_RET`, `A64_NOP`, `MOV_W(rd,imm)`.
- **Resolvers** (`native_utils` + `template_native.cpp`), all version-independent:
  - `resolve_by_string_xref(info, "AssertString", expectedPrologue)` → function by a string it uses.
  - `resolve_rtti_vtable(info, "24CSomeClass")` → vtable by RTTI name (`24` = `strlen("CSomeClass")`).
  - `vtable_slot(vt, idx)`, `first_bl_target(fn, lo, hi, n)` — walk from a vtable/method to the
    concrete callee.
  - `find_module_info(lib)` derives text/rodata/data bounds from **program headers** (no hardcoded
    sections); `find_ptr_in_range` walks vtables/RTTI by value (RELATIVE relocs are pre-applied, so
    a slot literally holds the runtime pointer — no reloc parsing).
- **Feature framework.** A `CodeFeature{ name, enabled, resolve, nwords, on[] }` resolves at init,
  captures the original bytes, and `feature_apply(f, on)` writes the replacement (ON) or restores
  the captured original (OFF) — so mod-menu toggles genuinely **apply *and* revert**. Unresolved /
  guard-mismatch ⇒ the feature no-ops (fail-closed).

Add a feature in ~6 lines: write a `resolve_x()` that returns the address, then append
`CodeFeature{ "x", {true}, resolve_x, 1, {A64_RET, 0}, … }` to `g_features[]` and set `TARGET_LIB`.

### 5. Verify durably
Relaunch the game (ASLR moves the base every time) and confirm the module re-resolves and re-applies
by itself — read the bytes back at `base+rva`. On this emulator the base is the `base.apk`
`r-xp 00d80000` region start; the module logs `feature X: resolved @…`.

## Gotchas learned the hard way
- **APK-embedded lib** → resolve base with `dl_iterate_phdr` (`dlpi_addr`), not by lib name in maps.
- **ASLR** every launch → everything in RVA space, add base at patch time.
- **MTE-tagged heap pointers** (top byte tag) → mask `& 0x00FFFFFFFFFFFFFF` before deref, or read via
  `/proc/pid/mem` (ignores tags).
- **Stripped binary** → no dynsym for game functions; RTTI names + assert strings are your anchors.
- **Obfuscated/guarded values** (`shadow0^shadow1`, recreated per-update) → don't chase them; patch
  the producer. Anti-tamper *value* detectors (not code checksums) are the usual guard — neutralize
  the detector's predicate once (RTTI→vtable→Detect→first BL) and every guarded value is safe.
- **No `.text` integrity check** is common even in hardened games → byte-patches are invisible; you
  usually don't need to hook at all.
- **Server-authoritative state** (inventory counts, currency) reverts on sync → prefer the game's
  own mechanic (a "free" flag) over forcing the number.

## When to hook (ShadowHook) vs byte-patch
- **Byte-patch** for "force a constant / skip a check" — 95% of cheats. No trampoline, nothing to
  detect, trivially revertible. This is the default here.
- **ShadowHook** (`shadowhook_hook_func_addr` on a *resolved* code pointer) only when you must
  **intercept** — read/modify arguments or the return, run your own logic, or trace during RE. The
  `getpid` demo in `template_native.cpp` is a smoke-test that hooking works on the device; a real
  hook targets a resolved address, not an exported symbol name.
