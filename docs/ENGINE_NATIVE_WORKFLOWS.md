# Engine-native workflow notes

This template is engine-neutral. The default module ships one Java smoke hook, one native
ShadowHook smoke hook, an engine detector, and a generic feature overlay. Keep app-specific
offsets, generated metadata, and target names on a feature branch.

Use this document when an authorized target has a native-heavy runtime such as Unity IL2CPP,
Unreal, Cocos2d-x, Godot, Flutter, or a custom C/C++ core.

## Universal native checklist

1. Identify the process and ABI first.
   - Confirm the LSPosed module loads in the intended package and process.
   - Confirm `arm64-v8a` or `armeabi-v7a` before enabling ShadowHook.
   - Keep x86/x86_64 emulator runs Java-only unless your hook framework supports them.

2. Decide when the target library is actually loaded.
   - Java `Application.attach` often runs before Unity/Unreal native libraries are mapped.
   - Use `/proc/self/maps`, a bounded worker thread, ShadowHook pending hooks, or linker
     init callbacks before installing app-specific native hooks.
   - Log both "waiting for library" and "hook installed" with the target address.

3. Keep runtime control reversible.
   - Put every behavior behind a bool or numeric feature key.
   - Prefer per-feature toggles over an in-app master kill switch. LSPosed/Vector is the
     correct place to disable the entire module.
   - If settings are edited from the module APK but consumed inside the target process, use
     a provider or target-sandbox file bridge. Avoid depending on public external storage
     writes from the target UID.

4. Separate research artifacts from the template.
   - Put APK extracts, disassembler projects, generated C# dumps, screenshots, and logcat
     captures under `artifacts/` or another ignored directory.
   - Commit only small, intentional evidence files on app-specific branches.
   - Never put target-specific offsets into `main`.

5. Validate stability before adding more hooks.
   - Start with one hook, run the target, and check `AndroidRuntime`, fatal signals, native
     tombstones, framework hook errors, and anti-cheat telemetry.
   - If a launch crash appears, disable risky hooks in priority order and re-enable one at a
     time. Keep this bisection trail in branch-specific docs.

## Unity IL2CPP static workflow

Unity IL2CPP targets usually ship:

- `libil2cpp.so` under `lib/<abi>/`
- `global-metadata.dat` under `assets/bin/Data/Managed/Metadata/`
- sibling libraries such as `libunity.so`, `libmain.so`, analytics, crash, or Firebase libs

Recommended static pass:

1. Extract the APK and identify shipped ABIs.
2. Pair the matching `libil2cpp.so` and `global-metadata.dat`.
3. Generate at least two independent symbol views, for example:
   - Il2CppDumper: `script.json`, `dump.cs`, Ghidra/IDA scripts
   - Cpp2IL or Il2CppInspector-style output for cross-checking names and signatures
4. Search managed names broadly, then verify each candidate in native disassembly.
5. Import symbols into your disassembler if possible, but keep raw RVA notes too.

Important IL2CPP details:

- Dump RVAs are not process addresses. Runtime address is `libil2cpp_base + rva`.
- A method name match is not enough. Confirm the function shape and callers.
- Value types can be passed in registers, on the stack, or through hidden return buffers.
  Check the exact AArch64 ABI at the hooked site.
- Field names can be semantically inverted. A field named `speed` might actually store an
  interval or cooldown. Confirm with arithmetic and comparisons before deciding whether to
  multiply or divide.
- Obfuscated wrapper types, such as anti-cheat numeric wrappers, should be written through
  the game/helper conversion routine when possible. Raw field writes can desync hidden keys.
- Virtual and interface methods may be reached through vtables. Hooking the implementation
  can work, but call-site or vtable strategies may be needed for polymorphic paths.

## Candidate ranking

Rank candidates before implementing:

- High: gameplay caller xrefs, clear primitive/value-type shape, narrow class ownership, and
  a reversible strategy.
- Medium: good name and shape but broad side effects, UI callers, or version-sensitive fields.
- Low: name match only, no gameplay xrefs, ambiguous ABI, or heavy shared runtime surface.

Prefer these patch strategies in order:

- Wrapper: call original, adjust return value or field after original state is valid.
- Argument rewrite: change a parameter, then call original.
- State/timer adjustment: touch live state only after confirming the direction of comparisons.
- Constant return: only for simple getters or predicates with known side effects.

## Documentation expected on app branches

For each non-smoke hook, branch docs should record:

- managed method, RVA, and ABI
- backing field offsets, if used
- xrefs or caller evidence
- hook strategy and runtime setting key
- logcat evidence for install and at least one hit
- crash or bisection notes
- any dynamic verification still needed

This keeps the universal template clean while preserving enough detail for the app-specific
branch to be maintainable.
