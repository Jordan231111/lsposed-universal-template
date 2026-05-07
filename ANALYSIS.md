# Static Analysis Report: firemodmenu.apk

Target APK: `/Users/jordan/Downloads/firemodmenu.apk`
Workspace: `/Users/jordan/Downloads/lsposed-universal-template-analysis`
Scope: static analysis only. No app execution, ADB, emulator, Frida, rooted device, or dynamic confirmation was used.

## Executive Summary

The bundled mod menu patches three Unity IL2CPP methods in `libil2cpp.so`. The mod does not encode these final `libil2cpp.so` RVAs as simple `base + immediate` constants. Instead, `libliteapks.so` loads `libil2cpp.so`, resolves IL2CPP reflection/export APIs with `dlsym`, resolves method pointers by class name, method name, and argument count, then installs inline hooks on the returned native method pointers.

No Xposed/LSPosed Java hook entrypoint was found. The Java layer starts the overlay/menu and loads native libraries; the target game logic hooks are native inline hooks.

## Summary Table

| Hex offset | Owning library | Resolved function | Patch type | Confidence |
|---:|---|---|---|---|
| `0x2AC0170` | `libil2cpp.so` | `BattleHero.IsImmune()` | Inline hook | High |
| `0x3FFD898` | `libil2cpp.so` | `GooglePlay.GooglePlayLicense.Check(Action<License> callback)` | Inline hook | High |
| `0x3FFD8F0` | `libil2cpp.so` | `GooglePlay.GooglePlayLicense.Check(GooglePlayLicenseConfig config, Action<License> callback)` | Inline hook | High |

## APK Entry and Native Components

- Package: `com.HolydayStudios.Firestone`
- Manifest application class: `com.android.support.killsign` at `artifacts/apktool/AndroidManifest.xml:62`
- Launch activity: `com.unity3d.player.UnityPlayerActivity` at `artifacts/apktool/AndroidManifest.xml:83`
- `UnityPlayerActivity.onCreate()` calls `Main.Start(this)` before normal Unity initialization: `artifacts/jadx/sources/com/unity3d/player/UnityPlayerActivity.java:26-27`
- `com.android.support.Main` loads `libliteapks.so`: `artifacts/jadx/sources/com/android/support/Main.java:13-16`
- `com.android.support.menuprotect` loads `libmenuliteapks.so`: `artifacts/jadx/sources/com/android/support/menuprotect.java:5-9`

Custom or relevant native libraries in `lib/arm64-v8a/`:

- `libliteapks.so`: main hook logic. Imports `dlopen`, `dlsym`, `mprotect`, `memcpy`, `memmove`, and exports `JNI_OnLoad` at `0x85fb0`.
- `libmenuliteapks.so`: menu/protector native glue. Imports `dlopen`, `dlsym`, `mprotect`, `memcpy`, `memmove`, but no target IL2CPP class/method strings were found.
- `libprotectliteapks.so`: protector/support library. No target IL2CPP strings were found.
- `libmain.so`, `libunity.so`, `libil2cpp.so`: normal Unity native components.

## Hooking Mechanism

`libliteapks.so` contains the actual target resolution and hook installation path.

1. IL2CPP loader/resolver

   At `libliteapks.so:0x874e4`, the code repeatedly calls `dlopen("libil2cpp.so", 4)`, then resolves IL2CPP exports with `dlsym`. Relevant string evidence from `.rodata`:

   - `libil2cpp.so` at `0x480ef`
   - `il2cpp_class_from_name` at `0x47ba2`
   - `il2cpp_class_get_method_from_name` at `0x485bd`

   Disassembly evidence:

   - `0x87510`: `bl ... <dlopen@plt>`
   - `0x87530`, `0x87548`, etc.: repeated `bl ... <dlsym@plt>`
   - `0x87540`: stores resolved `il2cpp_class_from_name`
   - `0x87558`: stores resolved `il2cpp_class_get_method_from_name`

2. Module base resolver

   `libliteapks.so:0x8731c` builds `/proc/self/maps`, opens it, scans with `fgets` and `strstr`, and parses the mapping base with `strtoul`. This supplies the loaded base for target libraries.

3. IL2CPP method resolver

   `libliteapks.so:0x87b34` takes a class descriptor, method name, and argument count:

   - calls `0x8731c` to recover the module base
   - calls resolved `il2cpp_class_get_method_from_name`
   - loads the native method pointer with `ldr x1, [x8]`
   - returns `(module_base, methodPointer)`

   `libliteapks.so:0x87b2c` returns the method pointer field from that pair.

4. Inline hook installer

   `libliteapks.so:0x8db98` is an inline hook installer with the practical signature:

   ```c
   int hook(void *target, void *replacement, void **original_storage);
   ```

   It receives the target in `x0`, replacement in `x1`, and original/trampoline storage in `x2`. It changes page permissions through helper `0x8e340`, allocates hook/trampoline state, writes the hook, and stores the original callable pointer when `x2` is non-null.

   Related evidence:

   - `0x8db98`: exported obfuscated symbol `WbVjgSYTC7nWTn3gNguwd0yn`
   - `0x8dbac`: `mov x22, x1` saves replacement
   - `0x8dbb0`: `mov x21, x2` saves original storage
   - `0x8dbd0`: calls `0x8e340`, which calls `mprotect`
   - `0x8dc64-0x8dc68`: stores original/trampoline into `[x21]`

5. Raw byte patch helper present but not used for the three findings

   `libliteapks.so:0x8e364` changes target pages to RWX, calls `memcpy`, restores RX permissions, and flushes the instruction cache. No direct xrefs from the target hook installation path were found for this helper. The three concrete target changes are installed through the inline hook installer at `0x8db98`.

## Per-offset Details

### 1. `libil2cpp.so` offset `0x2AC0170` - `BattleHero.IsImmune()`

Resolved managed method:

- `artifacts/tools/Il2CppDumper/out/dump.cs:14776-14777`
- `artifacts/tools/Il2CppDumper/out/script.json:16546-16549`
- `script.json` address: decimal `44826992`, hex `0x2AC0170`

IL2CPP dump entry:

```csharp
// RVA: 0x2AC0170 Offset: 0x2ABC170 VA: 0x2AC0170
private bool IsImmune() { }
```

Original prologue at `libil2cpp.so` RVA `0x2AC0170`, file offset `0x2ABC170`:

```text
02abc170: fe 0f 1e f8 f4 4f 01 a9 b4 4b 01 d0 f3 03 00 aa
02abc180: 88 4e 72 39 c8 00 00 37 00 2d 01 b0 00 bc 44 f9
```

Original instructions at the hook target:

```asm
2ac0170: f81e0ffe  str x30, [sp, #-32]!
2ac0174: a9014ff4  stp x20, x19, [sp, #16]
2ac0178: d0014bb4  adrp x20, 5436000
2ac017c: aa0003f3  mov x19, x0
2ac0180: 39724e88  ldrb w8, [x20, #3219]
2ac0184: 370000c8  tbnz w8, #0, 2ac019c
```

Hook resolution evidence in `libliteapks.so`:

- `.rodata` string `BattleHero` at `0x47996`
- method name `IsImmune` is stack-built at `0x7d958-0x7d974`
- `0x7d978-0x7d984`: passes class descriptor, method string, and arg count `0` into resolver `0x87b34`
- `0x7d99c-0x7d9a0`: extracts the method pointer through `0x87b2c`
- `0x7d9a8`: `adr x1, 0x7d5f4` replacement
- `0x7d9b0`: `adr x2, 0x10a318` original/trampoline storage
- `0x7d9b4`: calls inline hook installer `0xfd990 <WbVjgSYTC7nWTn3gNguwd0yn@plt>`

Replacement function at `libliteapks.so:0x7d5f4`:

```asm
7d5f4: adrp x8, 10a000
7d5f8: ldrb w8, [x8, #776]     ; global flag at 0x10a308
7d5fc: cbz w8, 7d608
7d600: mov w0, #1
7d604: ret
7d608: adrp x8, 10a000
7d60c: ldr x1, [x8, #792]      ; original pointer at 0x10a318
7d610: br x1
```

Inferred effect:

- When global flag `0x10a308` is nonzero, `BattleHero.IsImmune()` returns `true`.
- When the flag is zero, the replacement tail-branches to the original method pointer stored at `0x10a318`.
- `libliteapks.so:0x7e7a4-0x7e7b8` writes the global flag from a native boolean path, likely a menu feature toggle. The exact UI label was not recovered because menu strings are native/protected/obfuscated.

Confidence: High. Class string, method string construction, argument count, IL2CPP method metadata, replacement behavior, and hook call all agree.

### 2. `libil2cpp.so` offset `0x3FFD898` - `GooglePlay.GooglePlayLicense.Check(Action<License> callback)`

Resolved managed method:

- `artifacts/tools/Il2CppDumper/out/dump.cs:968848-968850`
- `artifacts/tools/Il2CppDumper/out/script.json:1255972-1255975`
- `script.json` address: decimal `67098776`, hex `0x3FFD898`

IL2CPP dump entry:

```csharp
// RVA: 0x3FFD898 Offset: 0x3FF9898 VA: 0x3FFD898
public static void Check(Action<License> callback) { }
```

Original prologue at `libil2cpp.so` RVA `0x3FFD898`, file offset `0x3FF9898`:

```text
03ff9898: fe 57 be a9 f4 4f 01 a9 f4 a1 00 b0 15 86 00 b0
03ff98a8: f3 03 00 aa 88 a2 72 39 b5 c2 40 f9 c8 00 00 37
```

Original instructions at the hook target:

```asm
3ffd898: a9be57fe  stp x30, x21, [sp, #-32]!
3ffd89c: a9014ff4  stp x20, x19, [sp, #16]
3ffd8a0: b000a1f4  adrp x20, 543a000
3ffd8a4: b0008615  adrp x21, 50be000
3ffd8a8: aa0003f3  mov x19, x0
3ffd8ac: 3972a288  ldrb w8, [x20, #3240]
```

Hook resolution evidence in `libliteapks.so`:

- `.rodata` string `GooglePlay` at `0x480e4`
- `.rodata` string `GooglePlayLicense` at `0x47f54`
- method name `Check` is stack-built at `0x7da94-0x7daac`
- `0x7dab0-0x7dabc`: passes class descriptor, method string, and arg count `1` into resolver `0x87b34`
- `0x7dad4-0x7dad8`: extracts method pointer through `0x87b2c`
- `0x7dae0`: `adr x1, 0x7d618` replacement
- `0x7dae8`: `adr x2, 0x10a328` original/trampoline storage
- `0x7daec`: calls inline hook installer `0xfd990 <WbVjgSYTC7nWTn3gNguwd0yn@plt>`

Replacement function at `libliteapks.so:0x7d618`:

```asm
7d618: ret
```

Inferred effect:

- The one-argument license check returns immediately, suppressing its normal body.
- Because the managed method returns `void`, a bare `ret` is a plausible "do nothing" hook.

Confidence: High. The class, method name, arg count `1`, IL2CPP metadata, replacement address, and hook call all match.

### 3. `libil2cpp.so` offset `0x3FFD8F0` - `GooglePlay.GooglePlayLicense.Check(GooglePlayLicenseConfig config, Action<License> callback)`

Resolved managed method:

- `artifacts/tools/Il2CppDumper/out/dump.cs:968852-968854`
- `artifacts/tools/Il2CppDumper/out/script.json:1255978-1255981`
- `script.json` address: decimal `67098864`, hex `0x3FFD8F0`

IL2CPP dump entry:

```csharp
// RVA: 0x3FFD8F0 Offset: 0x3FF98F0 VA: 0x3FFD8F0
public static void Check(GooglePlayLicenseConfig config, Action<License> callback) { }
```

Original prologue at `libil2cpp.so` RVA `0x3FFD8F0`, file offset `0x3FF98F0`:

```text
03ff98f0: fe 57 be a9 f4 4f 01 a9 f5 a1 00 b0 f3 03 01 aa
03ff9900: f4 03 00 aa a8 a6 72 39 28 01 00 37 00 86 00 b0
```

Original instructions at the hook target:

```asm
3ffd8f0: a9be57fe  stp x30, x21, [sp, #-32]!
3ffd8f4: a9014ff4  stp x20, x19, [sp, #16]
3ffd8f8: b000a1f5  adrp x21, 543a000
3ffd8fc: aa0103f3  mov x19, x1
3ffd900: aa0003f4  mov x20, x0
3ffd904: 3972a6a8  ldrb w8, [x21, #3241]
```

Hook resolution evidence in `libliteapks.so`:

- `.rodata` string `GooglePlay` at `0x480e4`
- `.rodata` string `GooglePlayLicense` at `0x47f54`
- method name `Check` is stack-built at `0x7da38-0x7da50`
- `0x7da54-0x7da60`: passes class descriptor, method string, and arg count `2` into resolver `0x87b34`
- `0x7da78-0x7da7c`: extracts method pointer through `0x87b2c`
- `0x7da84`: `adr x1, 0x7d614` replacement
- `0x7da8c`: `adr x2, 0x10a320` original/trampoline storage
- `0x7da90`: calls inline hook installer `0xfd990 <WbVjgSYTC7nWTn3gNguwd0yn@plt>`

Replacement function at `libliteapks.so:0x7d614`:

```asm
7d614: ret
```

Inferred effect:

- The two-argument license check returns immediately, suppressing its normal body.
- Because the managed method returns `void`, a bare `ret` is a plausible "do nothing" hook.

Confidence: High. The class, method name, arg count `2`, IL2CPP metadata, replacement address, and hook call all match.

## Java-side / LSPosed / Xposed Findings

No Java hook targets were found.

Searches for these patterns returned no app hook implementation:

- `IXposedHookLoadPackage`
- `findAndHookMethod`
- `deoptimizeMethod`
- `xposed_init`
- LSPosed/Xposed module assets

The only `xposed`-like strings found were unrelated third-party anti-hook detection strings in bundled dependencies. The Xposed API documentation says an app-specific Xposed module normally implements `IXposedHookLoadPackage` and registers hooks when a package is loaded; that pattern is absent here.

## Negative Hook-framework Evidence

No strings or imports identifying these named hook frameworks were found in the mod library path:

- Dobby / `DobbyHook`
- Substrate / `MSHookFunction`
- Whale
- xHook / `xhook_register`
- Shadowhook
- bhook
- SandHook
- And64InlineHook

The hook installer in `libliteapks.so` is obfuscated/custom or statically incorporated under obfuscated symbols. Its behavior is still recognizable as an inline hook implementation because it accepts target/replacement/original-storage, changes code page permissions, and creates a trampoline/original pointer.

## Open Questions and Low-confidence Items

- The exact runtime bytes written by the inline hook installer were not dynamically observed. Static evidence identifies the original target prologues and the installer call sites. The final branch/trampoline encoding and overwritten instruction count are generated at runtime by `libliteapks.so:0x8db98`.
- The menu label controlling `BattleHero.IsImmune()` was not recovered. Static analysis shows the replacement is gated by global byte `0x10a308`, and `0x7e7b8` writes that flag from a native boolean path, but the UI string is not visible in Java or native strings.
- The license-check hooks are bare `ret` stubs. Static inference is that they suppress the license check methods; dynamic confirmation would be needed to describe exact game UI/network side effects.
- Il2CppDumper emitted a protection warning, but it still generated `dump.cs` and `script.json`, and the resolved method names/signatures match the mod's IL2CPP lookup strings and argument counts. Confidence remains high for the three listed RVAs.

## Feature Hooks

This section supersedes the earlier initial-pass notes below. The target APK only ships `arm64-v8a`, so every RVA here is for `artifacts/apktool/lib/arm64-v8a/libil2cpp.so` unless another library is named. Static cross-checks used Il2CppDumper, Il2CppInspectorRedux, Cpp2IL, `gobjdump`, and a direct AArch64 branch/xref scanner over `libil2cpp.so`.

Important bottom-up result: none of the three requested feature families are confirmed mod-menu hooks. The bundled mod menu has exactly three target IL2CPP hooks: `BattleHero.IsImmune()` at `0x2AC0170` and two `GooglePlayLicense.Check(...)` methods at `0x3FFD898` / `0x3FFD8F0`. Repeated string, DEX, and native xref sweeps found no mod-menu UI strings or hook-install paths for `Speed`, `Game Speed`, `OHK`, `One Hit`, `Attack Speed`, `AtkSpd`, `Damage`, or `Multiplier`. The tables below therefore classify the requested features as additional IL2CPP candidates, not mod-menu-confirmed offsets.

### Easy-Wins (Currency / Multiplier / Unlock)

Pre-pass result: the IL2CPP dump exposes at least three High-confidence, cheap runtime hooks before any speed/OHK/attack-speed instrumentation is needed. These are higher-priority implementation targets because they are narrow, obvious in logcat, and avoid BigDouble/ObscuredFloat mutation unless explicitly noted.

| Category | Status | RVA in libil2cpp.so | Managed method / field | Suggested patch strategy | Confidence | What it does in-game |
|---|---|---:|---|---|---|---|
| Currency affordability | Additional candidate, implemented | `0x2BA5E48` | `CentralCurrencyHandler::HaveCurrency(CurrencyCode Currency, double Quantity)` | Constant-return `true` while `free_currency` is enabled; tail-call original when disabled | High | Makes high-level normal-currency affordability checks pass. |
| Currency affordability | Additional candidate, implemented | `0x2BA5ED8` | `Currency::HaveCurrency(double RequestedCost)` | Constant-return `true` while `free_currency` is enabled; tail-call original when disabled | High | Makes concrete normal-currency balance checks pass, including checks reached outside the central helper. |
| Big-number currency affordability | Additional candidate, not installed after launch bisection | `0x2AC2D98` | `BigCurrency::HaveCurrency(ObscuredBigDouble RequestedCost)` | Candidate constant-return `true`, but function-entry return-skip is disabled after static-to-device validation crashed at `libil2cpp.so+0x2ac2dbc`; use a typed proxy or call-site patch before re-enabling | High static / Low current implementation | Would make large-number currency checks pass for gold/firestone-style `ObscuredBigDouble` balances, but needs a safer ABI strategy. |
| Reward multiplier | Additional candidate | `0x28DC220` | `BattleMainStageManager::CalculateWaveGold(int CurrentStageNumber, int CurrentWaveNumber)` | Return-value multiplier on `ObscuredBigDouble` | Medium | Multiplies per-wave gold calculation. Not implemented first because it requires `ObscuredBigDouble` return construction. |
| Offline reward | Additional candidate | `0x29CB530` | `OfflineCalculations::CalculateOfflineRewards(double OfflineTime, ...)` | Argument rewrite on `OfflineTime` or return-flow multiplier | Medium | Increases offline progress/reward calculations. Not implemented first because it is coroutine/IEnumerator-shaped. |
| Critical loot | Additional candidate | `0x28DDBDC` / `0x28DDCC4` | `BattleMainStageManager::CalculateCriticalLootChance()` / `CalculateCriticalLootBonus()` | Post-original field-force on `ObscuredDouble` chance/bonus fields | Medium | Raises critical loot chance/bonus. Not implemented first because the obvious fields are `ObscuredDouble`. |
| Hero god-mode | Confirmed mod-menu validated, implemented independently | `0x2AC0170` | `BattleHero::IsImmune()` | Constant-return `true` while `god_mode` is enabled; tail-call original when disabled | High | Makes hero immunity checks pass. Reimplemented independently through Shadowhook, not copied from the mod menu. |

Disassembly sanity check for Easy-Wins:

```asm
2ba5e48: fc1d0fe8  str d8, [sp, #-48]!      ; CentralCurrencyHandler.HaveCurrency
2ba5eb8: fd401c00  ldr d0, [x0, #56]
2ba5ec4: 1e682000  fcmp d0, d8
2ba5ec8: 1a9fb7e0  cset w0, ge

2ba5ed8: fd401c01  ldr d1, [x0, #56]        ; Currency.HaveCurrency
2ba5edc: 1e602020  fcmp d1, d0
2ba5ee0: 1a9fb7e0  cset w0, ge
2ba5ee4: d65f03c0  ret

2ac2d98: d10303ff  sub sp, sp, #0xc0        ; BigCurrency.HaveCurrency
2ac2da4: 910163e0  add x0, sp, #0x58
2ac2da8: 52800b02  mov w2, #0x58
2ac2dac: 948cc5c9  bl memcpy
```

Implementation files: `app/src/main/cpp/hooks/easy_wins.cpp` installs the two normal Free Currency hooks. The `BigCurrency` candidate remains documented because the method is high-confidence statically, but the CPU-context return-skip version was removed after a launch crash at the method's `memcpy` block (`0x2AC2DBC`), indicating the intercept did not safely bypass the original body on-device.

### BigDouble ABI

`BreakInfinity.BigDouble` is a 16-byte value type:

```csharp
public double MA; // 0x0
public long EX;  // 0x8
```

The LSPosed module mirrors this as:

```cpp
struct BigDouble {
    double mantissa;
    int64_t exponent;
};
```

For `BattleController::ApplyDamage(...)`, the generated AArch64 body at `0x27C4E10` copies the `BigDouble AppliedDamage` value from the argument register pair before normal damage routing. The module constructs lethal damage directly as `{ configured_damage_multiplier, 308 }` and only applies it after resolving `TargetCharacterBattleLogic + 0x20` to its `BattleCharacter` and checking `BattleCharacter::IsHero()` at `0x29E2610`.

### ObscuredFloat ABI

`CodeStage.AntiCheat.ObscuredTypes.ObscuredFloat` is a 24-byte value type:

```csharp
private int currentCryptoKey;      // 0x0
private int hiddenValue;           // 0x4
private ACTkByte4 hiddenValueOldByte4; // 0x8
private bool inited;               // 0xC
private float fakeValue;           // 0x10
private bool fakeValueActive;      // 0x14
```

The current value is `FloatBits(hiddenValue ^ currentCryptoKey)`. For writes, the module prefers the game helper `ObscuredFloat.op_Implicit(float)` at `0x211761C`, which returns a correctly initialized wrapper through the AArch64 indirect-result register. Only if that helper is unavailable does it fall back to direct XOR construction. Attack-speed total fields use this ABI at `BattleCharacter.attackSpeedTotal + 0x178`; this keeps the obfuscation key and wrapper layout coherent instead of writing a raw float into the field.

### Game Speed / Movement / Animation

| Feature | Status | RVA in libil2cpp.so | Managed method (from IL2CPP dump) | Backing field offset (if any) | Patch strategy(ies) | Mod-menu patch bytes (orig -> new, if confirmed) | Mod-menu UI string (if confirmed) | Why it works | Why this candidate vs. others | Confidence |
|---|---|---:|---|---|---|---|---|---|---|---|
| Game speed multiplier | Additional candidate | `0x4A0EAA4` | `UnityEngine.Time::set_timeScale(float)` | Unity engine static, not exposed in dump | Argument rewrite / wrapper multiplier | None observed | None | Unity routes global time scaling through this setter. Rewriting `s0` before the native tail call forces a global time scale. | Highest-leverage global timing hook; broader side effects than gameplay-only hooks. | High |
| Game speed / physics timing | Additional candidate | `0x4A0E9F4` | `UnityEngine.Time::get_fixedDeltaTime()` | Unity engine static, not exposed in dump | Return-value multiplier / constant override | None observed | None | Fixed timestep consumers read through this icall wrapper. Scaling the return affects physics/root-motion timing. | Useful complement to `timeScale`; less direct for combat speed in this game. | Medium |
| Wave / run transition speed | Additional candidate | `0x2AC99DC` | `TeamLogic::ApplyWaveTransitionSpeedModifierToDuration(float)` | Static `TeamLogic.<WaveTransitionSpeedMultiplier>k__BackingField` at `0xC` | Return-value multiplier / divisor rewrite | None observed | None | The method divides transition durations by `WaveTransitionSpeedMultiplier`, directly shortening team run and wave-transition movement. | More gameplay-local than global `Time.timeScale`; avoids UI/ad timing side effects. | High |
| Wave speed setter | Additional candidate | `0x2AC9814` | `TeamLogic::set_WaveTransitionSpeedMultiplier(float)` | Static backing field `0xC` | Argument rewrite / force configured multiplier | None observed | None | The setter stores incoming `s0` to the static speed-multiplier backing field. | Cleaner persistent wave-speed state hook than patching every caller. | Medium |
| Per-entity special animation / movement speed | Additional candidate | `0x2B78918` | `CharacterAnimationController::PlaySpecialAnimationSequence(..., float animationTimeScale)` | Argument `animationTimeScale`; controller fields include movement/animation data | Argument rewrite / wrapper multiplier | None observed | None | Team movement calls this method during `Run`, `RunOutOfScreen`, and recalibration paths; the final float controls sequence animation time scale. | Good per-entity animation-speed hook when global time is too broad. | Medium |
| Spine animation speed | Additional candidate | `0x436AAC8` | `Spine.AnimationState::set_TimeScale(float)` | `AnimationState.TimeScale` field at native object offset `0x6C` | Argument rewrite / constant store | None observed | None | The method is a two-instruction setter storing `s0` into the Spine animation state. | Library-level animation hook; broad across all Spine animations, so side effects are likely. | Medium |

### One-hit Kill / Damage / HP

| Feature | Status | RVA in libil2cpp.so | Managed method (from IL2CPP dump) | Backing field offset (if any) | Patch strategy(ies) | Mod-menu patch bytes (orig -> new, if confirmed) | Mod-menu UI string (if confirmed) | Why it works | Why this candidate vs. others | Confidence |
|---|---|---:|---|---|---|---|---|---|---|---|
| One-hit kill | Additional candidate | `0x27C4E00` / `0x27C4E10` | `BattleController::ApplyDamage(CharacterBattleLogic, BigDouble, ...)` | Damage argument is `BigDouble`; no direct field | Argument rewrite to lethal `AppliedDamage`; wrapper can inspect target side | None observed | None | Direct branch scan found 107 calls into the overload family from normal attacks, abilities, guardians, and enemy/hero battle logic. Rewriting damage here covers most combat paths. | Most central damage choke point; better coverage than ability-specific hooks. | High |
| Enemy-only one-hit kill | Additional candidate | `0x29E5D48` | `EnemyBattleLogic::OnDamageApplication()` | `CharacterBattleLogic.battleCharacter` at `0x20`; inherited battle controller at `0x120` | Rewrite outgoing damage before it calls `BattleController.ApplyDamage` | None observed | None | This enemy-specific damage-application method calls the central `ApplyDamage` path at `0x29E5F70`. Hooking here can avoid damaging hero-side logic. | Safer than global `ApplyDamage` if the goal is enemies only; less coverage for non-standard enemy damage paths. | High |
| Enemy HP setter | Additional candidate | `0x29E5628` | `BattleEnemy::SetCurrentHealth(BigDouble)` | `BattleCharacter.currentHealth` at `0x70`; enemy max/current baseline at `0x18` | Argument rewrite to zero/minimum; byte patch around field copy | None observed | None | The override copies the incoming `BigDouble` into `this + 0x70`, then clamps against enemy baseline health. | Direct HP state hook; useful for forcing death after any damage event, but must avoid spawn/init paths. | Medium |
| Damage receive core | Additional candidate | `0x29E1AC8` | `BattleCharacter::ReceiveDamage(BigDouble, out AttackResult)` | `currentHealth` `0x70`; `DamageReceivedMultipliers` `0x308` | Multiplier wrap / force lethal damage / class-filtered wrapper | None observed | None | The function multiplies incoming damage by `DamageReceivedMultipliers` and computes the `AttackResult`; `BattleHero.ReceiveDamage` directly calls it after immunity checks. | Semantically exact damage-receive point, but virtual dispatch means direct xrefs undercount enemy use. | Medium |
| Low enemy max HP | Additional candidate | `0x29E59D8` | `BattleMainEnemy::CalculateMaximumHealthTotal()` | `maximumHealthInitial` `0x320`; `totalHealthMultiplier` `0x400`; resulting max health near `this + 0x18` | Constant override / multiplier reduction | None observed | None | The method multiplies enemy base health by total health multipliers and stores the resulting max/current baseline. | Makes enemies naturally one-shot by reducing max HP; more side effects on UI/progression display. | Medium |

### Attack Speed / Cooldown / Fire-rate

| Feature | Status | RVA in libil2cpp.so | Managed method (from IL2CPP dump) | Backing field offset (if any) | Patch strategy(ies) | Mod-menu patch bytes (orig -> new, if confirmed) | Mod-menu UI string (if confirmed) | Why it works | Why this candidate vs. others | Confidence |
|---|---|---:|---|---|---|---|---|---|---|---|
| Attack speed multiplier | Additional candidate | `0x2AC82E8` | `CharacterBattleLogic::FillAttackSpeedTimer(bool Delay)` | `attackSpeedTimer` `0x38`; reads `BattleCharacter.attackSpeedTotal` `0x178` | Force timer full / zero delay / return-value style wrapper | None observed | None | The method reads `battleCharacter + 0x178`, divides delay by three when requested, and stores a new timer at `this + 0x38`. | Best runtime attack cadence hook; one direct caller from `TeamLogic.FillAttackGauge`. | High |
| Hero attack speed total | Additional candidate | `0x2AC2AF8` | `BattleMainHero::CalculateAttackSpeedTotal()` | `BattleCharacter.attackSpeedTotal` `0x178`; `AttackSpeedMultipliers` `0x2E0` | Multiplier wrap / forced field store | None observed | None | The method combines hero attack-speed sources and eventually writes the computed `ObscuredFloat` to `this + 0x178`. | Better for player-only attack speed than timer patching; recalculation-driven and less likely to affect enemies. | High |
| Enemy attack speed total | Additional candidate | `0x29E5B94` | `BattleMainEnemy::CalculateAttackSpeedTotal()` | `attackSpeedInitial` `0x3D0`; `BattleCharacter.attackSpeedTotal` `0x178` | Constant override / multiplier reduction | None observed | None | The method loads enemy initial attack speed, divides by a multiplier helper, and writes to `this + 0x178`. | Useful for enemy slow/debuff mods; not a player attack-speed multiplier by itself. | Medium |
| Persistent hero basic attack speed | Additional candidate | `0x2B34FB0` | `Hero::CalculateBasicAttackSpeed()` | `Hero.attackSpeedInitial` `0x308`; `Hero.attackSpeedBasic` `0x3F0` | Multiplier wrap / forced field store | None observed | None | The method computes the persistent hero stat and is called by `HeroDeck.CalculateAttackSpeedForAllHeroes`. | Higher in the stat pipeline; may persist across recalculations but will not cover temporary battle-only buffs. | Medium |
| All-heroes stat recalculation | Additional candidate | `0x2B1E1A4` | `HeroDeck::CalculateAttackSpeedForAllHeroes()` | Iterates hero list; delegates to `Hero.CalculateBasicAttackSpeed()` | Wrapper after loop / call-through then scale each hero | None observed | None | The direct xref map shows this iterates all heroes and calls `Hero.CalculateBasicAttackSpeed()` at `0x2B1E1DC`. | Broad all-hero stat recalculation hook; less precise than `BattleMainHero.CalculateAttackSpeedTotal`. | Medium |

### Feature-hook Evidence Details

#### `0x4A0EAA4` - `UnityEngine.Time::set_timeScale(float)`

IL2CPP metadata:

```text
dump.cs:713197-713200
// RVA: 0x4A0EA7C ... public static float get_timeScale() { }
// RVA: 0x4A0EAA4 ... public static void set_timeScale(float value) { }

script.json:
0x4A0EAA4 UnityEngine.Time$$set_timeScale |
void UnityEngine_Time__set_timeScale (float value, const MethodInfo* method);
```

Original disassembly:

```asm
4a0eaa4: fc1e0fe8  str d8, [sp, #-32]!
4a0eaa8: a9014ffe  stp x30, x19, [sp, #16]
4a0eaac: 1e204008  fmov s8, s0
4a0eab0: 900051b3  adrp x19, 5442000
4a0eab4: f9471660  ldr x0, [x19, #3624]
4a0eacc: a9414ffe  ldp x30, x19, [sp, #16]
4a0ead0: 1e204100  fmov s0, s8
4a0ead4: fc4207e8  ldr d8, [sp], #32
4a0ead8: d61f0000  br x0
```

Static xrefs: 18 direct callers, including debug/time commands, loading/error paths, ad callbacks, and screenshot utilities. That breadth is why the hook is powerful and side-effect-heavy.

Suggested strategy: inline-hook the method and rewrite incoming `s0` to the desired multiplier before chaining to the original native setter. A raw `RET` patch would only freeze the current value and is less useful.

Needs dynamic verification: exact gameplay tick side effects, because several non-combat systems call this setter too.

#### `0x4A0E9F4` - `UnityEngine.Time::get_fixedDeltaTime()`

IL2CPP metadata:

```text
dump.cs:713188
// RVA: 0x4A0E9F4 ... public static float get_fixedDeltaTime() { }

script.json:
0x4A0E9F4 UnityEngine.Time$$get_fixedDeltaTime |
float UnityEngine_Time__get_fixedDeltaTime (const MethodInfo* method);
```

Original disassembly:

```asm
4a0e9f4: a9bf4ffe  stp x30, x19, [sp, #-16]!
4a0e9f8: 900051b3  adrp x19, 5442000
4a0e9fc: f9470660  ldr x0, [x19, #3592]
4a0ea00: b50000a0  cbnz x0, 4a0ea14
4a0ea14: a8c14ffe  ldp x30, x19, [sp], #16
4a0ea18: d61f0000  br x0
```

Static xrefs: two direct callers (`TMPro.Examples.CameraController.LateUpdate`, `Spine.Unity.SkeletonRootMotionBase.PhysicsUpdate`). The direct-call count is lower than `timeScale`, but Unity native/managed wrappers may reach it indirectly.

Suggested strategy: hook and scale the returned float. This is a physics/root-motion candidate, not the best single global speed hook.

#### `0x2AC99DC` - `TeamLogic::ApplyWaveTransitionSpeedModifierToDuration(float)`

IL2CPP metadata:

```text
dump.cs:15676,15720,15728
private static float <WaveTransitionSpeedMultiplier>k__BackingField; // 0xC
// RVA: 0x2AC9814 ... private static void set_WaveTransitionSpeedMultiplier(float value) { }
// RVA: 0x2AC99DC ... private float ApplyWaveTransitionSpeedModifierToDuration(float baseDuration) { }

script.json:
0x2AC99DC TeamLogic$$ApplyWaveTransitionSpeedModifierToDuration |
float TeamLogic__ApplyWaveTransitionSpeedModifierToDuration (TeamLogic_o* __this, float baseDuration, const MethodInfo* method);
```

Original disassembly:

```asm
2ac99dc: fc1e0fe8  str d8, [sp, #-32]!
2ac99e8: 1e204008  fmov s8, s0
2ac9a20: f9400288  ldr x8, [x20]
2ac9a24: f9405d08  ldr x8, [x8, #184]
2ac9a28: bd400100  ldr s0, [x8]
2ac9a2c: 1e282000  fcmp s0, s8
2ac9a80: f9405c08  ldr x8, [x0, #184]
2ac9a84: bd400d00  ldr s0, [x8, #12]
2ac9a88: 1e201908  fdiv s8, s8, s0
```

Static xrefs: direct calls from `TeamLogic.get_RemainingRunTime +0x48` and `TeamLogic.UpdateRunState +0x8C`.

Suggested strategy: hook this method and divide `baseDuration` by a larger multiplier, or force `WaveTransitionSpeedMultiplier` through the setter at `0x2AC9814`. This is a movement/wave-speed hook, not a global time hook.

#### `0x2AC9814` - `TeamLogic::set_WaveTransitionSpeedMultiplier(float)`

IL2CPP metadata:

```text
dump.cs:15676,15719-15720
private static float <WaveTransitionSpeedMultiplier>k__BackingField; // 0xC
// RVA: 0x2AC9814 ... private static void set_WaveTransitionSpeedMultiplier(float value) { }
```

Original disassembly:

```asm
2ac9814: fc1e0fe8  str d8, [sp, #-32]!
2ac9820: 1e204008  fmov s8, s0
...
2ac9860: f9405c08  ldr x8, [x0, #184]
2ac986c: bd000d08  str s8, [x8, #12]
2ac9874: d65f03c0  ret
```

Suggested strategy: rewrite incoming `s0` to the desired value before the `str s8, [x8, #12]`. The field offset in the IL2CPP dump is static `0xC`, and the generated code writes to `#12`.

#### `0x2B78918` - `CharacterAnimationController::PlaySpecialAnimationSequence(...)`

IL2CPP metadata:

```text
dump.cs:162233
// RVA: 0x2B78918 ... public float PlaySpecialAnimationSequence(..., float animationTimeScale = 1) { }

script.json:
0x2B78918 CharacterAnimationController$$PlaySpecialAnimationSequence |
float CharacterAnimationController__PlaySpecialAnimationSequence (..., float animationTimeScale, const MethodInfo* method);
```

Original disassembly:

```asm
2b78918: 6db93bef  stp d15, d14, [sp, #-112]!
2b78934: 1e2040eb  fmov s11, s7
2b78938: 1e2040cc  fmov s12, s6
2b78948: 3945f6c8  ldrb w8, [x22, #381]
2b7898c: b4001273  cbz x19, 2b78bd8
2b78990: 1e2041e0  fmov s0, s15
2b789ac: 947a6644  bl 4a122bc
```

Static xrefs: `TeamLogic.Run`, `TeamLogic.RunOutOfScreen`, `TeamLogic.MoveToRecaliberedPosition`, and `TeamLogic.RecaliberOppositeTeamOnRevive`.

Suggested strategy: hook and multiply the `animationTimeScale` argument. It is a per-entity animation/movement candidate, especially for run and wave transition sequences.

#### `0x436AAC8` - `Spine.AnimationState::set_TimeScale(float)`

IL2CPP metadata:

```text
dump.cs:830518
// RVA: 0x436AAC8 ... public void set_TimeScale(float value) { }

script.json:
0x436AAC8 Spine.AnimationState$$set_TimeScale |
void Spine_AnimationState__set_TimeScale (Spine_AnimationState_o* __this, float value, const MethodInfo* method);
```

Original disassembly:

```asm
436aac8: bd006c00  str s0, [x0, #108]
436aacc: d65f03c0  ret
```

Suggested strategy: rewrite `s0` before the store. This is mechanically simple but broad; static direct-call scan did not find direct `BL` xrefs because many property calls are inlined or dispatched through generated wrappers.

#### `0x27C4E00` / `0x27C4E10` - `BattleController::ApplyDamage(...)`

IL2CPP metadata:

```text
dump.cs:10549-10552
// RVA: 0x27C4E00 ... public void ApplyDamage(..., bool ForceAllow = False, int heroCode = -1) { }
// RVA: 0x27C4E10 ... public void ApplyDamage(..., bool IsCritical, bool ForceAllow, int heroCode = -1) { }

script.json:
0x27C4E00 BattleController$$ApplyDamage |
void BattleController__ApplyDamage (..., BigDouble AppliedDamage, bool ForceAllow, int32_t heroCode, ...);
0x27C4E10 BattleController$$ApplyDamage |
void BattleController__ApplyDamage (..., BigDouble AppliedDamage, bool IsCritical, bool ForceAllow, int32_t heroCode, ...);
```

Original disassembly:

```asm
27c4e00: 2a0503e6  mov w6, w5
27c4e04: 2a0403e5  mov w5, w4
27c4e08: 2a1f03e4  mov w4, wzr
27c4e0c: 14000001  b 27c4e10
27c4e10: fc190fea  str d10, [sp, #-112]!
27c4e1c: a9026ffe  stp x30, x27, [sp, #32]
27c4e50: aa0303f7  mov x23, x3
27c4e54: aa0203f8  mov x24, x2
27c4e58: aa0103f4  mov x20, x1
```

Static xrefs: 107 direct calls to `0x27C4E00` and 4 to `0x27C4E10`. Callers include `EnemyBattleLogic.OnDamageApplication`, `HeroBattleLogic.OnDamageApplication`, guardian damage routines, and many basic/special ability coroutines.

Suggested strategy: inline-hook the overload body and replace or scale the `BigDouble AppliedDamage` argument (`x2/x3` carry the value in the generated ABI here). A robust one-hit implementation should inspect `TargetCharacterBattleLogic` so hero-side incoming damage is not also made lethal.

Needs dynamic verification: exact `BigDouble` construction helper for a lethal value is static-discovered but not runtime-confirmed.

#### `0x29E5D48` - `EnemyBattleLogic::OnDamageApplication()`

IL2CPP metadata:

```text
dump.cs:14577
// RVA: 0x29E5D48 ... protected override void OnDamageApplication() { }

script.json:
0x29E5D48 EnemyBattleLogic$$OnDamageApplication |
void EnemyBattleLogic__OnDamageApplication (EnemyBattleLogic_o* __this, const MethodInfo* method);
```

Original disassembly:

```asm
29e5d48: d10483ff  sub sp, sp, #0x120
29e5d60: aa0003f3  mov x19, x0
29e5dbc: f9407a68  ldr x8, [x19, #240]
29e5de0: f9401260  ldr x0, [x19, #32]
29e5dec: a95b8509  ldp x9, x1, [x8, #440]
29e5df0: d63f0120  blr x9
...
29e5f70: 97f77ba4  bl 27c4e00
```

Suggested strategy: hook before the outgoing `BattleController.ApplyDamage` call and replace the computed `BigDouble` damage. This is enemy-specific, making it a safer OHK point than the global controller if target filtering is hard.

#### `0x29E5628` - `BattleEnemy::SetCurrentHealth(BigDouble)`

IL2CPP metadata:

```text
dump.cs:14471
// RVA: 0x29E5628 ... public override void SetCurrentHealth(BigDouble Value) { }
```

Original disassembly:

```asm
29e5628: d104c3ff  sub sp, sp, #0x130
29e5634: aa0003f3  mov x19, x0
29e563c: aa0103e0  mov x0, x1
29e5648: 948c29b2  bl 4cefd10
29e564c: 9101c260  add x0, x19, #0x70
29e5650: 9102e3e1  add x1, sp, #0xb8
29e5658: 94903b9e  bl 4df44d0 <memcpy@plt>
29e5690: 9101c260  add x0, x19, #0x70
29e5694: 91006261  add x1, x19, #0x18
```

Suggested strategy: hook and force `Value` to zero/minimum for enemy instances. The body clearly copies a `BigDouble` into `currentHealth` at `this + 0x70`, then clamps against `this + 0x18`.

Needs dynamic verification: whether this setter runs during enemy initialization; if so, a class/state guard is required.

#### `0x29E1AC8` - `BattleCharacter::ReceiveDamage(BigDouble, out AttackResult)`

IL2CPP metadata:

```text
dump.cs:14274
// RVA: 0x29E1AC8 ... public virtual BigDouble ReceiveDamage(BigDouble Damage, out AttackResult AttackResult) { }
```

Original disassembly:

```asm
29e1ac8: a9bc7bfd  stp x29, x30, [sp, #-64]!
29e1af4: aa0103f5  mov x21, x1
29e1b24: b90002df  str wzr, [x22]
29e1b28: 948c387a  bl 4cefd10
29e1b2c: f9418661  ldr x1, [x19, #776]
29e1b34: 94000038  bl 29e1c14
```

Suggested strategy: hook and scale the incoming `Damage`, or force a lethal `BigDouble` after checking that `__this` is an enemy. Direct branch scan only saw `BattleHero.ReceiveDamage` call it directly; enemy use is likely through inherited virtual dispatch.

#### `0x29E59D8` - `BattleMainEnemy::CalculateMaximumHealthTotal()`

IL2CPP metadata:

```text
dump.cs:14499
// RVA: 0x29E59D8 ... public override void CalculateMaximumHealthTotal() { }
```

Original disassembly:

```asm
29e59d8: d107c3ff  sub sp, sp, #0x1f0
29e5a18: 910c8261  add x1, x19, #0x320
29e5a20: 94903aac  bl 4df44d0 <memcpy@plt>
29e5a28: 91100261  add x1, x19, #0x400
29e5a30: 94903aa8  bl 4df44d0 <memcpy@plt>
29e5a88: 91006260  add x0, x19, #0x18
```

Suggested strategy: force the computed maximum health to a low value. This creates one-hit behavior indirectly but can desynchronize UI/progression displays if used after enemies are already spawned.

#### `0x2AC82E8` - `CharacterBattleLogic::FillAttackSpeedTimer(bool)`

IL2CPP metadata:

```text
dump.cs:15073-15075,15225
private ObscuredBool isAttackSpeedTimerPaused; // 0x2C
private ObscuredFloat attackSpeedTimer; // 0x38
// RVA: 0x2AC82E8 ... public void FillAttackSpeedTimer(bool Delay) { }
```

Original disassembly:

```asm
2ac82e8: d10203ff  sub sp, sp, #0x80
2ac82f8: f9401008  ldr x8, [x0, #32]
2ac8300: 9105e109  add x9, x8, #0x178
2ac8304: f940c508  ldr x8, [x8, #392]
2ac8324: 97d93cc2  bl 211762c
2ac8330: 36000234  tbz w20, #0, 2ac8374
2ac835c: 1e211001  fmov s1, #3.0
2ac8364: 1e211801  fdiv s1, s0, s1
2ac8374: 1e213900  fsub s0, s8, s1
2ac8394: 3c838260  stur q0, [x19, #56]
2ac8398: f9002668  str x8, [x19, #72]
```

Static xrefs: one direct call from `TeamLogic.FillAttackGauge +0x9C`.

Suggested strategy: hook and force the resulting `attackSpeedTimer` (`this + 0x38`) to a full/ready value, or zero the delay subtraction path. This directly changes attack cadence.

#### `0x2AC2AF8` - `BattleMainHero::CalculateAttackSpeedTotal()`

IL2CPP metadata:

```text
dump.cs:14222,14235,14861
public ObscuredFloat attackSpeedTotal; // 0x178
public Dictionary<string, float> AttackSpeedMultipliers; // 0x2E0
// RVA: 0x2AC2AF8 ... public override void CalculateAttackSpeedTotal() { }
```

Original disassembly:

```asm
2ac2af8: d101c3ff  sub sp, sp, #0x70
2ac2b2c: f9419268  ldr x8, [x19, #800]
2ac2b3c: bd43f109  ldr s9, [x8, #1008]
2ac2b50: aa1f03e0  mov x0, xzr
2ac2b5c: aa1f03e1  mov x1, xzr
2ac2b80: 97d952ab  bl 211762c
2ac2b84: f9417261  ldr x1, [x19, #736]
```

Suggested strategy: hook after original calculation and multiply the `ObscuredFloat` stored at `this + 0x178`, or force the output field directly. This targets heroes rather than enemies.

Needs dynamic verification: exact final store is later in the function than the shown prologue, but the `attackSpeedTotal` field and multiplier dictionary are confirmed in the dump.

#### `0x29E5B94` - `BattleMainEnemy::CalculateAttackSpeedTotal()`

IL2CPP metadata:

```text
dump.cs:14448,14505
protected readonly ObscuredFloat attackSpeedInitial; // 0x3D0
// RVA: 0x29E5B94 ... public override void CalculateAttackSpeedTotal() { }
```

Original disassembly:

```asm
29e5b94: d10183ff  sub sp, sp, #0x60
29e5ba0: 3dc0f400  ldr q0, [x0, #976]
29e5ba4: f941f008  ldr x8, [x0, #992]
29e5bc8: 1e204008  fmov s8, s0
29e5bcc: 97fff012  bl 29e1c14
29e5bd0: 1e201900  fdiv s0, s8, s0
29e5be8: 9105e268  add x8, x19, #0x178
29e5bf0: f900c669  str x9, [x19, #392]
29e5bf8: 3d800100  str q0, [x8]
```

Suggested strategy: force low enemy attack speed or multiply/divide the computed value before the store to `this + 0x178`. This is an enemy debuff hook, not a player attack-speed buff.

#### `0x2B34FB0` - `Hero::CalculateBasicAttackSpeed()`

IL2CPP metadata:

```text
dump.cs:154251-154255,154367
public float attackSpeedInitial; // 0x308
public float attackSpeedBasic; // 0x3F0
// RVA: 0x2B34FB0 ... public void CalculateBasicAttackSpeed() { }
```

Original disassembly:

```asm
2b34fb0: 6dbc23e9  stp d9, d8, [sp, #-64]!
2b34ffc: 1e6e1000  fmov d0, #1.0
2b35000: bd430a69  ldr s9, [x19, #776]
2b35038: f9400c00  ldr x0, [x0, #24]
2b35040: 528000a1  mov w1, #0x5
2b35050: 9434a37d  bl 385de44
```

Static xrefs: one direct call from `HeroDeck.CalculateAttackSpeedForAllHeroes +0x38`.

Suggested strategy: hook after original and multiply `attackSpeedBasic` (`this + 0x3F0`). This affects persistent hero stats and should survive later battle recalculation.

#### `0x2B1E1A4` - `HeroDeck::CalculateAttackSpeedForAllHeroes()`

IL2CPP metadata:

```text
dump.cs:154704
// RVA: 0x2B1E1A4 ... public void CalculateAttackSpeedForAllHeroes() { }

script.json:
0x2B1E1A4 HeroDeck$$CalculateAttackSpeedForAllHeroes |
void HeroDeck__CalculateAttackSpeedForAllHeroes (HeroDeck_o* __this, const MethodInfo* method);
```

Original disassembly:

```asm
2b1e1a4: a9be57fe  stp x30, x21, [sp, #-32]!
2b1e1ac: f9400813  ldr x19, [x0, #16]
2b1e1c8: 6b08029f  cmp w20, w8
2b1e1d0: f8747aa0  ldr x0, [x21, x20, lsl #3]
2b1e1dc: 94005b75  bl 2b34fb0
2b1e1e4: 91000694  add x20, x20, #0x1
```

Suggested strategy: hook the loop or chain original and then scale each hero's attack-speed field. This is broad and stable for all heroes, but less precise than the battle-character hook.

### Mod-menu Cross-reference Result

No requested feature row is confirmed because the mod-menu bottom-up pass found no corresponding hook-install path:

- `libliteapks.so` has exactly three direct calls to its hook installer PLT stub at `0xFD990`: installers at `0x7D9B4`, `0x7DA90`, and `0x7DAEC`.
- Those installers resolve `BattleHero.IsImmune()` (`0x2AC0170`) and `GooglePlay.GooglePlayLicense.Check(...)` (`0x3FFD898`, `0x3FFD8F0`), not speed/OHK/attack-speed methods.
- Native feature-string sweeps over `libliteapks.so`, `libmenuliteapks.so`, `libprotectliteapks.so`, and `libmain.so` did not find the requested UI terms in ASCII or UTF-16.
- DEX/JADX sweeps found the Android support menu loader, but no Xposed entrypoint and no Java hook path for these features.

### Cross-tool Output Diff

- Il2CppDumper produced `script.json`, `dump.cs`, Ghidra/IDA scripts, and dummy DLLs. Every tabled RVA above is present in `script.json`; field offsets are from `dump.cs`.
- Il2CppInspectorRedux built with .NET 10 and exported `artifacts/tools/Il2CppInspectorRedux/out/il2cpp.cs`, `il2cpp.json`, `il2cpp.py`, headers, and dummy DLLs. It confirmed the same method names and addresses for the core candidates listed above. The process hung after `SUCCESS: Export finished`, so it was killed after outputs were written.
- Cpp2IL built with .NET 10 and exported `artifacts/tools/Cpp2IL/out/DiffableCs`. It confirmed the same C# surface for `BattleController`, `BattleCharacter`, `BattleEnemy`, `BattleMainHero`, `BattleMainEnemy`, `CharacterBattleLogic`, `TeamLogic`, `Hero`, `HeroDeck`, `CharacterAnimationController`, and Unity `Time`.
- Original Il2CppInspector built, but its CLI project defaults to `win-x64` and could not run on this macOS host without a compatible .NET Core 3.1 runtime/apphost. Redux was used as the successful Il2CppInspector-family cross-check.

### Coverage Gaps

- Confirmed speed/OHK/attack-speed mod-menu offsets: none. The mod-menu does not statically expose any hook or raw patch for these features.
- Mod-menu UI claims for the requested terms: none found in native strings, DEX, smali, or JADX output. The visible Java menu appears to load native features dynamically, but the native feature strings for these terms are absent.
- Exact BigDouble one-hit payload bytes: needs dynamic verification or a deeper static reconstruction of the `BreakInfinity.BigDouble` constructors/helpers. The central offsets are clear, but the safest lethal payload is type-specific rather than a simple `MOV W0; RET`.
- Virtual/interface dispatch: direct branch scanning undercounts virtual calls to `BattleCharacter.ReceiveDamage`, `BattleMainHero.CalculateAttackSpeedTotal`, and `BattleMainEnemy.CalculateAttackSpeedTotal`. Field/method semantics are confirmed by metadata and disassembly, but call frequency needs runtime tracing or fuller vtable reconstruction.
- Ghidra GUI was not available on this Mac (`ghidraRun` / `analyzeHeadless` not found). Disassembly evidence was produced with GNU objdump/radare2-compatible ELF metadata and cross-checked against Il2CppDumper/Redux/Cpp2IL output.

## Implementation Mapping

Module branch: `feature/firestone-hooks`. Built debug APK: `app/build/outputs/apk/debug/app-debug.apk`.

Implementation exclusion: the LSPosed module intentionally does not hook or modify Google Play, Play Store license, billing, sign-in, or login paths. The earlier `GooglePlayLicense.Check(...)` offsets belong only to the historical mod-menu static-analysis notes and are not installed by `libfirestonehooks.so`.

| Feature | Source mapping | Runtime settings keys | Implemented behavior |
|---|---|---|---|
| Settings bridge | `app/src/main/java/com/template/lsposed/ui/LauncherActivity.java:44`, `app/src/main/java/com/template/lsposed/FirestoneSettings.java:25`, `app/src/main/java/com/template/lsposed/ModuleEntry.java:121` | JSON file `/data/data/com.HolydayStudios.Firestone/files/firestonehooks.json`; public fallback `/sdcard/Android/media/com.firestone.hooks/firestonehooks.json` | Standalone module Activity writes provider-backed JSON and a public fallback; target process syncs normalized settings to the game sandbox every 500 ms and uses safe defaults if neither source is visible. |
| Native loader | `app/src/main/java/com/template/lsposed/NativeBridge.java:90`, `app/src/main/cpp/firestonehooks.cpp:89`, `app/src/main/cpp/firestonehooks.cpp:56` | `native_hooks` plus legacy `enabled=true` normalization | Loads `libfirestonehooks.so`, waits for `libil2cpp.so`, installs hooks once, and starts the native settings refresh thread. The old in-app master off state is ignored; global disablement belongs in LSPosed Manager. |
| RVA resolution / hook helper | `app/src/main/cpp/hooks/common.cpp:55`, `app/src/main/cpp/hooks/common.cpp:79` | N/A | Resolves `libil2cpp.so` base from `/proc/self/maps`, then installs Shadowhook function hooks by `base + RVA`. |
| Easy-Win: free currency | `app/src/main/cpp/hooks/easy_wins.cpp:29`, `app/src/main/cpp/hooks/easy_wins.cpp:39`, `app/src/main/cpp/hooks/easy_wins.cpp:51`, `app/src/main/cpp/hooks/easy_wins.cpp:59` | `free_currency` | Returns `true` from central and normal currency affordability checks; startup logs confirm the Free Currency toggle is active. The high-confidence `BigCurrency` candidate is documented but not installed after launch bisection. |
| God-mode | `app/src/main/cpp/hooks/god_mode.cpp:25`, `app/src/main/cpp/hooks/god_mode.cpp:37` | `god_mode` | Returns `true` from `BattleHero.IsImmune()` when enabled; otherwise calls original. |
| Game speed | `app/src/main/cpp/hooks/game_speed.cpp:45`, `app/src/main/cpp/hooks/game_speed.cpp:79` | `game_speed`, `game_speed_multiplier`, `wave_speed`, `wave_speed_multiplier` | Scales `Time.set_timeScale`, `Time.get_fixedDeltaTime`, wave transition duration/setter, and Spine `AnimationState.TimeScale`. |
| One-hit kill | `app/src/main/cpp/hooks/one_hit_kill.cpp:41`, `app/src/main/cpp/hooks/one_hit_kill.cpp:50`, `app/src/main/cpp/hooks/one_hit_kill.cpp:84` | `one_hit_kill`, `damage_multiplier` | Rewrites `BigDouble AppliedDamage` to a lethal value only when `TargetCharacterBattleLogic` resolves to non-hero. |
| Attack speed | `app/src/main/cpp/hooks/attack_speed.cpp`, `app/src/main/java/com/template/lsposed/ui/OverlayController.java`, `app/src/main/java/com/template/lsposed/ui/LauncherActivity.java` | `attack_speed`, `attack_speed_multiplier`, `attack_speed_battle_stat`, `attack_speed_idle_timer`, `attack_speed_attack_timer`, `attack_speed_roster_stat`, `slow_enemies`, `enemy_attack_speed_multiplier` | Player/team path now has four switchable strategies: battle interval scaling at `BattleMainHero.CalculateAttackSpeedTotal()`, ally roster stat scaling at `Hero.CalculateBasicAttackSpeed()`, idle gauge priming at `CharacterBattleLogic.UpdateIdleState()` / `FillAttackSpeedTimer()`, and attack action timer acceleration at `CharacterBattleLogic.UpdateAttackState()`. The in-game overlay exposes an `Attack speed` slider from `1x` to `20x` plus per-path toggles. |

### Attack-speed follow-up

The original `x2.0` limit observed in the in-game menu was a UI exposure issue, not a native hook
limit. The standalone settings Activity already wrote `attack_speed_multiplier`, but the injected
overlay only rendered the game-speed slider. The native `FillAttackSpeedTimer` hook also ignored
that multiplier and only removed the delayed/randomized refill argument. The current UI and native
clamps allow `1x..20x`.

Static disassembly confirmed `attackSpeedTotal` and `attackSpeedBasic` are attack intervals, not
"speed stat points". Smaller is faster here. `BattleMainHero.CalculateAttackSpeedTotal()` divides
the hero base interval by multipliers, then `UpdateIdleState()` waits until `attackSpeedTimer`
reaches that interval. Therefore dividing `Hero.attackSpeedBasic` by `10` or `20` does not weaken
the hero; it shortens the delay between attacks.

```text
0x2AC2BA8  fdiv s0, s9, s0
0x2AC2BBC  str q0, [x19 + 0x178]   ; BattleMainHero.attackSpeedTotal

0x2AC8374  fsub s0, s8, s1
0x2AC8394  stur q0, [x19 + 0x38]   ; CharacterBattleLogic.attackSpeedTimer
```

The first implementation treated `attack_speed_multiplier` as an interval divisor only at
`BattleMainHero.CalculateAttackSpeedTotal()` and `CharacterBattleLogic.FillAttackSpeedTimer()`.
That was not enough visually because the live attack loop has two additional gates:
`UpdateIdleState()` increments `attackSpeedTimer` until it reaches `attackSpeedTotal`, and
`UpdateAttackState()` increments a separate attack animation/action timer until the hit sequence
finishes.

The current implementation therefore exposes four independently testable attack-speed paths:

| Toggle | RVA | Managed method / field | Behavior |
|---|---:|---|---|
| `attack_speed_battle_stat` | `0x2AC2AF8` | `BattleMainHero.CalculateAttackSpeedTotal()` / `BattleCharacter.attackSpeedTotal` `+0x178` | Divides the battle interval when roster stat has not already supplied the multiplier. |
| `attack_speed_roster_stat` | `0x2B34FB0` | `Hero.CalculateBasicAttackSpeed()` / `Hero.attackSpeedBasic` `+0x3F0` | Divides every ally hero's persistent basic attack interval; verified across 24 hero objects. |
| `attack_speed_idle_timer` | `0x2AC71F0`, `0x2AC82E8` | `CharacterBattleLogic.UpdateIdleState()` and `FillAttackSpeedTimer()` / `attackSpeedTimer` `+0x38` | Primes the live idle gauge to `attackSpeedTotal * (1 - 1 / multiplier)`, so `10x` leaves 10% of the wait and `20x` leaves 5%. |
| `attack_speed_attack_timer` | `0x2AC708C` | `CharacterBattleLogic.UpdateAttackState()` / attack action timer `+0x68` | Multiplies the in-progress attack action timer so the attack completes faster after it starts. |

Runtime verification at `20x` produced:

```text
AttackSpeed.CharacterBattleLogic.UpdateIdleState hook installed rva=0x2ac71f0
AttackSpeed.CharacterBattleLogic.UpdateAttackState hook installed rva=0x2ac708c
AttackSpeed.Hero.CalculateBasicAttackSpeed hook installed rva=0x2b34fb0
attack-speed BattleMainHero.CalculateAttackSpeedTotal #1 ... target=20.00x
attack-speed Hero.CalculateBasicAttackSpeed roster #1 ... attackSpeedBasic 2.3000->0.1150 x20.00
attack-speed UpdateIdleState gauge #1 ... timer 0.0000->0.0783
attack-speed UpdateAttackState timer #1 ... timer 0.0150->0.3002
```

Runtime feature status from logcat: free-currency affordability hooks install and fire on currency
checks; god-mode fires on `BattleHero.IsImmune`; OHK fires when enabled on enemy damage paths;
attack speed now fires on player hero stat and timer paths at `20x`; game-speed hooks install, but
observed gameplay impact remains mostly visual/broad timing and is intentionally left in place for
learning.

Build verification completed locally:

```text
./gradlew assembleDebug -> BUILD SUCCESSFUL
./gradlew lint -> BUILD SUCCESSFUL
```

ADB verification completed on 2026-05-07 against `com.HolydayStudios.Firestone` on the connected rooted LSPosed emulator:

```text
adb install -r app/build/outputs/apk/debug/app-debug.apk -> Success
LSPosed loaded com.firestone.hooks for com.HolydayStudios.Firestone
JNI_OnLoad firestonehooks observed
libil2cpp.so base resolved
EasyWin.CentralCurrencyHandler.HaveCurrency hook installed
EasyWin.Currency.HaveCurrency hook installed
easy-win FreeCurrency toggle active; affordability hooks installed
native hook install complete result=0
BattleHero.IsImmune hit #1..#5 observed
No CRASH/FATAL/SIGSEGV/SIGABRT markers in filtered launch log
```

Artifacts:

```text
artifacts/logcat_launch.txt
artifacts/logcat_launch_full.txt
artifacts/firestone_launch.png
artifacts/logcat_attack_speed_paths_x10.txt
artifacts/firestone_attack_speed_paths_x10.png
artifacts/logcat_attack_speed_paths_x20.txt
artifacts/firestone_attack_speed_paths_x20.png
artifacts/firestone_attack_speed_menu_x20.png
```

Device hygiene note: a stale earlier module package, `com.jordan.firestone.lsposed`, was installed and scoped before the final run. It was uninstalled, then the emulator was rebooted so LSPosed reloaded only `com.firestone.hooks` for this target.

## Feature Hooks (Superseded Initial Pass)

Static result: no mod-menu patch path was found for game speed multiplier, one-hit kill, or attack speed multiplier in this APK. The only target-game hooks installed by the mod menu are the three already documented above: `BattleHero.IsImmune()` and the two `GooglePlay.GooglePlayLicense.Check(...)` overloads.

The requested gameplay features do have plausible candidate methods in `libil2cpp.so`, but none of those candidate RVAs are referenced by `libliteapks.so`, `libmenuliteapks.so`, or `libprotectliteapks.so` as hook targets, byte-patch targets, or raw little-endian RVA constants.

### Feature Summary

| Feature | RVA in libil2cpp.so | Managed method (from IL2CPP dump) | Patch bytes (orig -> new) | Patch technique | Mod-menu UI string that led here | Confidence |
|---|---:|---|---|---|---|---|
| Game speed multiplier | Not found as a mod-menu patch | Candidate methods exist, e.g. `UnityEngine.Time.set_timeScale` at `0x4A0EAA4`, `UnityEngine.Time.get_fixedDeltaTime` at `0x4A0E9F4`, `TeamLogic.set_WaveTransitionSpeedMultiplier` at `0x2AC9814` | No write observed; candidate originals remain unreferenced by the mod patch path | None found | None. ASCII and UTF-16 string scans found no `Speed`, `Game Speed`, `timeScale`, or `fixedDeltaTime` UI string in the mod libraries | High confidence absence in static mod code |
| One-hit kill | Not found as a mod-menu patch | Candidate methods exist, e.g. `BattleController.ApplyDamage` at `0x27C4E00`/`0x27C4E10`, `BattleCharacter.ReceiveDamage` at `0x29E1AC8`, `BattleHero.ReceiveDamage` at `0x2ABFFC0` | No write observed; candidate originals remain unreferenced by the mod patch path | None found | None. String scans found no `OHK`, `One Hit`, `Damage`, `TakeDamage`, `ApplyDamage`, or related feature label in the mod libraries | High confidence absence in static mod code |
| Attack speed multiplier | Not found as a mod-menu patch | Candidate methods exist, e.g. `BattleCharacter.CalculateAttackSpeedTotal` at `0x29E1DFC`, `BattleMainHero.CalculateAttackSpeedTotal` at `0x2AC2AF8`, `CharacterBattleLogic.FillAttackSpeedTimer` at `0x2AC82E8` | No write observed; candidate originals remain unreferenced by the mod patch path | None found | None. String scans found no `Attack Speed`, `AtkSpd`, `Cooldown`, or related feature label in the mod libraries | High confidence absence in static mod code |

### Exhaustion Trail

The mod-menu hook inventory is complete for the visible static code path:

- `libliteapks.so` has exactly three direct calls to the inline hook installer `0xfd990 <WbVjgSYTC7nWTn3gNguwd0yn@plt>`:
  - `0x7d9b4`: hooks resolved `BattleHero.IsImmune()` with replacement `0x7d5f4`.
  - `0x7da90`: hooks resolved `GooglePlay.GooglePlayLicense.Check(config, callback)` with replacement `0x7d614`.
  - `0x7daec`: hooks resolved `GooglePlay.GooglePlayLicense.Check(callback)` with replacement `0x7d618`.
- `libliteapks.so` has exactly four calls to the IL2CPP method resolver `0x87b34`:
  - `0x7d8cc`: resolves `BattleCharacter.IsHero()` and stores the pointer as support state.
  - `0x7d984`: resolves `BattleHero.IsImmune()`.
  - `0x7da60`: resolves `GooglePlay.GooglePlayLicense.Check` with arg count `2`.
  - `0x7dabc`: resolves `GooglePlay.GooglePlayLicense.Check` with arg count `1`.
- `libliteapks.so` has a raw byte patch helper at `0x8e364`, but its direct calls are inside the hook/trampoline implementation:
  - `0x8aaf0`: internal patch application wrapper.
  - `0x8cc6c`: internal trampoline/code-range patch path.
  - `0x8dd18`: restore/remove-hook style path for a previously installed hook.
  These do not carry `libil2cpp.so` gameplay RVAs or feature strings.
- `libmenuliteapks.so` contains generic `dlopen`, `dlsym`, `/proc/self/maps`, `mprotect`, and `memcpy` code, but no visible `libil2cpp`, Unity class, gameplay method, speed, damage, health, cooldown, or attack strings. Its `mprotect` calls are in native loader/protector code, not in a discovered target-game patch caller.
- `libprotectliteapks.so` has no target IL2CPP strings and no discovered hook installation path.
- Raw little-endian searches for representative candidate RVAs were negative in the mod libraries, including `0x4A0EAA4`, `0x4A0E9F4`, `0x27C4E00`, `0x27C4E10`, `0x29E1AC8`, `0x2ABFFC0`, `0x29E1DFC`, `0x2AC2AF8`, `0x2AC82E8`, `0x2AC82D4`, and `0x2AC9814`.

### Game Speed Multiplier

No installing path was found.

Mod-menu code path checked:

```text
libliteapks.so:
  no feature strings: Speed, Game Speed, timeScale, fixedDeltaTime
  no hook installer call resolving UnityEngine.Time or TeamLogic speed methods
  no raw byte patch helper call carrying candidate time/speed RVAs

libmenuliteapks.so / libprotectliteapks.so:
  no libil2cpp/time/speed target strings
  no candidate RVA constants
```

IL2CPP candidate methods from `script.json`:

```text
0x4A0EAA4 UnityEngine.Time$$set_timeScale
  void UnityEngine_Time__set_timeScale(float value, const MethodInfo* method);

0x4A0E9F4 UnityEngine.Time$$get_fixedDeltaTime
  float UnityEngine_Time__get_fixedDeltaTime(const MethodInfo* method);

0x2AC9814 TeamLogic$$set_WaveTransitionSpeedMultiplier
  void TeamLogic__set_WaveTransitionSpeedMultiplier(float value, const MethodInfo* method);
```

Matching `dump.cs` signal:

- `artifacts/tools/Il2CppDumper/out/dump.cs:713188`: `UnityEngine.Time.get_fixedDeltaTime()`
- `artifacts/tools/Il2CppDumper/out/dump.cs:713200`: `UnityEngine.Time.set_timeScale(float value)`
- `artifacts/tools/Il2CppDumper/out/dump.cs:15720`: `TeamLogic.set_WaveTransitionSpeedMultiplier(float value)`

Original disassembly of the strongest global-speed candidate:

```asm
0000000004a0eaa4:
4a0eaa4: fc1e0fe8  str d8, [sp, #-32]!
4a0eaa8: a9014ffe  stp x30, x19, [sp, #16]
4a0eaac: 1e204008  fmov s8, s0
4a0eab0: 900051b3  adrp x19, 5442000
4a0eab4: f9471660  ldr x0, [x19, #3624]
4a0eab8: b50000a0  cbnz x0, 4a0eacc
```

Patch bytes:

```text
orig -> new: no mod-menu write observed for this RVA or related speed candidates.
```

Reasoning:

The IL2CPP dump contains legitimate game-speed candidates, but the mod menu never resolves their names through `il2cpp_class_get_method_from_name`, never references their RVAs as constants, and never passes their addresses into the inline hook or raw patch helpers. Static analysis therefore does not support a game-speed patch in this APK.

Needs dynamic verification:

Only if the native protector decrypts additional code or data at runtime outside the static artifact. No static evidence for such a game-speed target was found.

### One-hit Kill

No installing path was found.

Mod-menu code path checked:

```text
libliteapks.so:
  no feature strings: OHK, One Hit, Damage, TakeDamage, ApplyDamage, HP, Health
  no hook installer call resolving BattleController.ApplyDamage, BattleCharacter.ReceiveDamage, or BattleHero.ReceiveDamage
  no raw byte patch helper call carrying candidate damage/health RVAs

libmenuliteapks.so / libprotectliteapks.so:
  no libil2cpp/damage/health target strings
  no candidate RVA constants
```

IL2CPP candidate methods from `script.json`:

```text
0x27C4E00 BattleController$$ApplyDamage
  void BattleController__ApplyDamage(..., CharacterBattleLogic* TargetCharacterBattleLogic, BigDouble AppliedDamage, bool ForceAllow, int32_t heroCode, ...);

0x27C4E10 BattleController$$ApplyDamage
  void BattleController__ApplyDamage(..., CharacterBattleLogic* TargetCharacterBattleLogic, BigDouble AppliedDamage, bool IsCritical, bool ForceAllow, ...);

0x29E1AC8 BattleCharacter$$ReceiveDamage
  BigDouble BattleCharacter__ReceiveDamage(BattleCharacter* __this, BigDouble Damage, int32_t* AttackResult, ...);

0x2ABFFC0 BattleHero$$ReceiveDamage
  BigDouble BattleHero__ReceiveDamage(BattleHero* __this, BigDouble Damage, int32_t* AttackResult, ...);
```

Matching `dump.cs` signal:

- `artifacts/tools/Il2CppDumper/out/dump.cs:9802`: `BattleController.ApplyDamage(...)`
- `artifacts/tools/Il2CppDumper/out/dump.cs:9805`: `BattleController.ApplyDamage(...)` overload
- `artifacts/tools/Il2CppDumper/out/dump.cs:14274`: `BattleCharacter.ReceiveDamage(...)`
- `artifacts/tools/Il2CppDumper/out/dump.cs:14774`: `BattleHero.ReceiveDamage(...)`

Original disassembly of the strongest central damage candidate:

```asm
00000000027c4e00:
27c4e00: 2a0503e6  mov w6, w5
27c4e04: 2a0403e5  mov w5, w4
27c4e08: 2a1f03e4  mov w4, wzr
27c4e0c: 14000001  b 27c4e10
27c4e10: fc190fea  str d10, [sp, #-112]!
27c4e14: 6d00a3e9  stp d9, d8, [sp, #8]
27c4e18: f9000ffd  str x29, [sp, #24]
27c4e1c: a9026ffe  stp x30, x27, [sp, #32]
```

Patch bytes:

```text
orig -> new: no mod-menu write observed for this RVA or related damage/health candidates.
```

Reasoning:

One-hit kill implementations would normally hook or patch damage application, HP setters, receive-damage paths, or enemy health calculations. Those methods are present in the IL2CPP dump, but none are referenced by the mod menu's hook resolver, hook installer, raw byte patch helper, strings, or raw RVA constants. The existing `BattleHero.IsImmune()` hook affects hero immunity/god-mode behavior, not one-hit kill.

Needs dynamic verification:

Only if the native protector decrypts additional code or data at runtime outside the static artifact. No static evidence for an OHK target was found.

### Attack Speed Multiplier

No installing path was found.

Mod-menu code path checked:

```text
libliteapks.so:
  no feature strings: Attack Speed, AtkSpd, cooldown, AttackInterval, animationSpeed
  no hook installer call resolving attack-speed or cooldown methods
  no raw byte patch helper call carrying candidate attack-speed RVAs

libmenuliteapks.so / libprotectliteapks.so:
  no libil2cpp/attack-speed/cooldown target strings
  no candidate RVA constants
```

IL2CPP candidate methods from `script.json`:

```text
0x29E1DFC BattleCharacter$$CalculateAttackSpeedTotal
  void BattleCharacter__CalculateAttackSpeedTotal(BattleCharacter* __this, ...);

0x2AC2AF8 BattleMainHero$$CalculateAttackSpeedTotal
  void BattleMainHero__CalculateAttackSpeedTotal(BattleMainHero* __this, ...);

0x29E5B94 BattleMainEnemy$$CalculateAttackSpeedTotal
  void BattleMainEnemy__CalculateAttackSpeedTotal(BattleMainEnemy* __this, ...);

0x2AC82E8 CharacterBattleLogic$$FillAttackSpeedTimer
  void CharacterBattleLogic__FillAttackSpeedTimer(CharacterBattleLogic* __this, bool Delay, ...);
```

Matching `dump.cs` signal:

- `artifacts/tools/Il2CppDumper/out/dump.cs:14292`: `BattleCharacter.CalculateAttackSpeedTotal()`
- `artifacts/tools/Il2CppDumper/out/dump.cs:14861`: `BattleHero.CalculateAttackSpeedTotal()`
- `artifacts/tools/Il2CppDumper/out/dump.cs:15225`: `CharacterBattleLogic.FillAttackSpeedTimer(bool Delay)`

Original disassembly of an attack-speed timer candidate:

```asm
0000000002ac82e8:
2ac82e8: d10203ff  sub sp, sp, #0x80
2ac82ec: fd0033e8  str d8, [sp, #96]
2ac82f0: f90037fe  str x30, [sp, #104]
2ac82f4: a9074ff4  stp x20, x19, [sp, #112]
2ac82f8: f9401008  ldr x8, [x0, #32]
2ac82fc: b4000568  cbz x8, 2ac83a8
2ac8300: 9105e109  add x9, x8, #0x178
2ac8304: f940c508  ldr x8, [x8, #392]
```

Patch bytes:

```text
orig -> new: no mod-menu write observed for this RVA or related attack-speed/cooldown candidates.
```

Reasoning:

Attack speed multiplier patches would normally affect attack-speed totals, cooldown timers, or animation/timer fill logic. The IL2CPP dump has several candidate methods, but the mod menu does not resolve, reference, or patch them statically. The only feature-like gameplay hook found is `BattleHero.IsImmune()`, controlled by a boolean flag at `libliteapks.so:0x10a308`.

Needs dynamic verification:

Only if the native protector decrypts additional code or data at runtime outside the static artifact. No static evidence for an attack-speed target was found.

### Feature Hooks Tooling Log (Continuation)

Commands run after the initial report, in order:

1. `pwd`
2. `git status --short`
3. `rg -n "Speed|Game Speed|timeScale|fixedDeltaTime|OHK|One Hit|OneHit|Attack Speed|AtkSpd|Atk Speed|Damage|Multiplier|Cooldown|cooldown|Health|HP|Kill|TakeDamage|ApplyDamage|Animator|animationSpeed" artifacts/jadx artifacts/apktool ANALYSIS.md`
4. `for f in artifacts/apktool/lib/arm64-v8a/libliteapks.so artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so artifacts/apktool/lib/arm64-v8a/libprotectliteapks.so; do printf '\n== %s ==\n' "$f"; strings -a -n 3 "$f" | rg -i 'Speed|Game Speed|timeScale|fixedDeltaTime|OHK|One Hit|OneHit|Attack Speed|AtkSpd|Atk Speed|Damage|Multiplier|Cooldown|Health|HP|Kill|TakeDamage|ApplyDamage|Animator|animationSpeed|move|attack|cooldown|time'; done`
5. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libliteapks.so | rg -n 'fd990|WbVjgSYTC7nWTn3gNguwd0yn|8e364|AMMU4EjfeJ2JvcEHoJke2iGl|87b34|87b2c|874e4|88128|dlopen@plt|dlsym@plt|mprotect@plt'`
6. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so | rg -n 'mprotect|dlopen|dlsym|memcpy|RegisterNatives|JNI_OnLoad|bl.*[0-9a-f]+ <.*mprotect|bl.*[0-9a-f]+ <.*dlopen|bl.*[0-9a-f]+ <.*dlsym'`
7. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libprotectliteapks.so | rg -n 'mprotect|dlopen|dlsym|memcpy|RegisterNatives|JNI_OnLoad|bl.*[0-9a-f]+ <.*mprotect|bl.*[0-9a-f]+ <.*dlopen|bl.*[0-9a-f]+ <.*dlsym'`
8. `nm -D artifacts/apktool/lib/arm64-v8a/*.so | rg 'Dobby|MSHook|xhook|shadowhook|ShadowHook|A64Hook|InlineHook|bhook|Mshook|hook|Hook|RegisterNatives|Java_'`
9. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libliteapks.so | rg -n '8731c|87440|8cc6c|8aaf0|8dd18|fda30|AMMU4Ejfe'`
10. `gobjdump -d --start-address=0x8aa40 --stop-address=0x8ab30 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
11. `gobjdump -d --start-address=0x8cb80 --stop-address=0x8cd20 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
12. `gobjdump -d --start-address=0x8dcd0 --stop-address=0x8dd30 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
13. `rg -n -i 'timeScale|fixedDeltaTime|set_timeScale|set_fixedDeltaTime|Animator\$\$set_speed|set_speed|SpeedMultiplier|GameSpeed|game speed|moveSpeed|movementSpeed|runSpeed|walkSpeed' artifacts/tools/Il2CppDumper/out/dump.cs | head -120`
14. `rg -n -i 'one.?hit|OHK|instant.?kill|TakeDamage|ApplyDamage|ReceiveDamage|DealDamage|DamageTaken|DamageReceived|currentHp|CurrentHP|hit.?point|health|set_hp|set_health|Kill\(|Die\(' artifacts/tools/Il2CppDumper/out/dump.cs | head -180`
15. `rg -n -i 'attack.?speed|atk.?spd|AttackInterval|cooldown|attack.?cooldown|set_cooldown|animationSpeed|Animator\$\$set_speed|set_attack|AttackDelay|AttackTimer|reload' artifacts/tools/Il2CppDumper/out/dump.cs | head -180`
16. `rg -n -i 'timeScale|fixedDeltaTime|Animator\$\$set_speed|SpeedMultiplier|moveSpeed|TakeDamage|ApplyDamage|ReceiveDamage|DealDamage|currentHp|health|attack.?speed|AttackInterval|cooldown|animationSpeed|AttackDelay' artifacts/tools/Il2CppDumper/out/script.json | head -220`
17. `rabin2 -zz artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so | rg -i 'libil2cpp|libunity|il2cpp|Battle|Hero|Enemy|Damage|Speed|Attack|Health|Time|Google|License|mprotect|dlopen|dlsym|/proc/self/maps|maps'`
18. `gobjdump -d --start-address=0xb800 --stop-address=0xc060 artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so`
19. `gobjdump -d --start-address=0x8580 --stop-address=0x8700 artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so`
20. `rg -n 'libil2cpp|BattleHero|GooglePlayLicense|IsImmune|Check|BattleCharacter|Damage|Speed|Attack' artifacts/apktool/smali_classes4/com/android/support artifacts/jadx/sources/com/android/support`
21. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libliteapks.so | rg -n 'mov[kz]?\s+[wx][0-9]+, #0x(2ac0170|3ffd898|3ffd8f0|29e|2ab|2ac|3ff|447a|3f80|4120|42c8|461c|447a0000|3f800000|0000803f|00007a44)|#0x2ac|#0x3ffd|#0x447a|#0x3f80|#0x4120|#0x42c8'`
22. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so | rg -n 'mov[kz]?\s+[wx][0-9]+, #0x(2ac0170|3ffd898|3ffd8f0|29e|2ab|2ac|3ff|447a|3f80|4120|42c8|461c|447a0000|3f800000|0000803f|00007a44)|#0x2ac|#0x3ffd|#0x447a|#0x3f80|#0x4120|#0x42c8'`
23. `xxd -p artifacts/apktool/lib/arm64-v8a/libliteapks.so | tr -d '\n' | rg -o '70c02a0[0-9a-f]{2}|98d8ff03|f0d8ff03|00007a44|0000803f|00002041|0000c842' | head -80`
24. `xxd -p artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so | tr -d '\n' | rg -o '70c02a0[0-9a-f]{2}|98d8ff03|f0d8ff03|00007a44|0000803f|00002041|0000c842' | head -80`
25. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libliteapks.so | rg -n '87ba8|87be8|8731c <|bl\s+87ba8|bl\s+87b34|bl\s+87b2c'`
26. `gobjdump -d --start-address=0x87ba8 --stop-address=0x87c40 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
27. `rabin2 -zz artifacts/apktool/lib/arm64-v8a/libliteapks.so | rg -i 'Battle|Hero|Google|License|il2cpp|unity|speed|damage|attack|health|cooldown|time|animator|timescale|fixed'`
28. `rabin2 -zz artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so | rg -i 'Battle|Hero|Google|License|il2cpp|unity|speed|damage|attack|health|cooldown|time|animator|timescale|fixed'`
29. `node - <<'NODE' ...`
30. `node - <<'NODE' ...`
31. `for f in artifacts/apktool/lib/arm64-v8a/libliteapks.so artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so artifacts/apktool/lib/arm64-v8a/libprotectliteapks.so; do echo "== $f =="; for p in a4eaa004 f4e9a004 004e7c02 104e7c02 c81a9e02 c0ffab02 fc1d9e02 f82aac02 945b9e02 e882ac02 d482ac02 1498ac02; do if xxd -p "$f" | tr -d '\n' | rg -q "$p"; then echo "found $p"; fi; done; done`
32. `for f in artifacts/apktool/lib/arm64-v8a/libliteapks.so artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so; do echo "== $f =="; strings -a -el -n 3 "$f" | rg -i 'Speed|Damage|Attack|Health|One|Hit|OHK|Multiplier|Cooldown|timeScale|fixedDeltaTime' || true; done`
33. `for f in artifacts/apktool/lib/arm64-v8a/libliteapks.so artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so; do echo "== $f =="; strings -a -eb -n 3 "$f" | rg -i 'Speed|Damage|Attack|Health|One|Hit|OHK|Multiplier|Cooldown|timeScale|fixedDeltaTime' || true; done`
34. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libliteapks.so | rg -n '0x4a0eaa4|0x4a0e9f4|0x27c4e00|0x27c4e10|0x29e1ac8|0x2abffc0|0x29e1dfc|0x2ac2af8|0x2ac82e8|0x2ac82d4|0x2ac9814'`
35. `which gstrings || true`
36. `find /opt/homebrew/bin /usr/local/bin -name 'gstrings' -o -name 'strings' 2>/dev/null | sed -n '1,20p'`
37. `/opt/homebrew/opt/binutils/bin/gstrings -a -el -n 3 artifacts/apktool/lib/arm64-v8a/libliteapks.so artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so | rg -i 'Speed|Damage|Attack|Health|One|Hit|OHK|Multiplier|Cooldown|timeScale|fixedDeltaTime'`
38. `/opt/homebrew/opt/binutils/bin/gstrings -a -eb -n 3 artifacts/apktool/lib/arm64-v8a/libliteapks.so artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so | rg -i 'Speed|Damage|Attack|Health|One|Hit|OHK|Multiplier|Cooldown|timeScale|fixedDeltaTime'`
39. `/opt/homebrew/opt/binutils/bin/gstrings -a -el -n 3 artifacts/apktool/lib/arm64-v8a/libprotectliteapks.so | rg -i 'Speed|Damage|Attack|Health|One|Hit|OHK|Multiplier|Cooldown|timeScale|fixedDeltaTime'`
40. `/opt/homebrew/opt/binutils/bin/gstrings -a -eb -n 3 artifacts/apktool/lib/arm64-v8a/libprotectliteapks.so | rg -i 'Speed|Damage|Attack|Health|One|Hit|OHK|Multiplier|Cooldown|timeScale|fixedDeltaTime'`
41. `gobjdump -d --start-address=0x4a0eaa4 --stop-address=0x4a0ead0 artifacts/apktool/lib/arm64-v8a/libil2cpp.so`
42. `gobjdump -d --start-address=0x27c4e00 --stop-address=0x27c4e40 artifacts/apktool/lib/arm64-v8a/libil2cpp.so`
43. `gobjdump -d --start-address=0x29e1dfc --stop-address=0x29e1e40 artifacts/apktool/lib/arm64-v8a/libil2cpp.so`
44. `gobjdump -d --start-address=0x2ac82e8 --stop-address=0x2ac8330 artifacts/apktool/lib/arm64-v8a/libil2cpp.so`

## Online References Consulted

- [Dobby](https://github.com/jmpews/Dobby): used as a comparison point for inline hook framework signatures and ARM64 support.
- [xHook](https://github.com/iqiyi/xHook): used as a comparison point for GOT/PLT hook patterns; no `xhook_register`-style pattern was found in the mod code.
- [Xposed `IXposedHookLoadPackage` API](https://api.xposed.info/reference/de/robv/android/xposed/IXposedHookLoadPackage.html): used to validate absence of normal Java/Xposed module entrypoints.
- [Il2CppDumper](https://github.com/Perfare/Il2CppDumper): used to map IL2CPP metadata and method RVAs from `libil2cpp.so` plus `global-metadata.dat`.
- [Il2CppInspectorRedux](https://github.com/LukeFZ/Il2CppInspectorRedux): used as the Il2CppInspector-family cross-check for C# stubs, disassembler metadata, headers, and dummy DLLs.
- [Il2CppInspector](https://github.com/djkaty/Il2CppInspector): cloned and built; original CLI target/runtime was not runnable on this macOS host, so Redux output was used instead.
- [Cpp2IL](https://github.com/SamboyCoding/Cpp2IL): used to generate diffable C# output and cross-check the IL2CPP method surface.
- [Unity `Time.timeScale`](https://docs.unity3d.com/ScriptReference/Time-timeScale.html), [Unity `Time.fixedDeltaTime`](https://docs.unity3d.com/ScriptReference/Time-fixedDeltaTime.html), and [Unity `Animator.speed`](https://docs.unity3d.com/ScriptReference/Animator-speed.html): used to validate timing/animation candidate semantics.

## Tooling Log

Shell commands run, in order:

1. `pwd`
2. `git clone https://github.com/Jordan231111/lsposed-universal-template /Users/jordan/Downloads/lsposed-universal-template-analysis`
3. `which apktool`
4. `which jadx`
5. `which r2`
6. `mkdir -p artifacts`
7. `file /Users/jordan/Downloads/firemodmenu.apk`
8. `unzip -l /Users/jordan/Downloads/firemodmenu.apk`
9. `aapt dump badging /Users/jordan/Downloads/firemodmenu.apk`
10. `apktool d -f /Users/jordan/Downloads/firemodmenu.apk -o artifacts/apktool`
11. `jadx -d artifacts/jadx /Users/jordan/Downloads/firemodmenu.apk`
12. `find artifacts/apktool/lib -type f -maxdepth 3 -print`
13. `find artifacts/apktool/assets -maxdepth 5 -type f -print`
14. `find artifacts/apktool/original -type f -print`
15. `rg -n "xposed|Xposed|findAndHookMethod|deoptimizeMethod|loadLibrary|System\.load|native_init|java_init|LSPosed|lspatch|Hook" artifacts/apktool artifacts/jadx`
16. `sed -n '1,220p' artifacts/jadx/sources/com/android/support/Main.java`
17. `sed -n '1,220p' artifacts/jadx/sources/com/android/support/menuprotect.java`
18. `sed -n '1,260p' artifacts/jadx/sources/com/android/support/killsign.java`
19. `sed -n '1,220p' artifacts/apktool/AndroidManifest.xml`
20. `sed -n '260,620p' artifacts/jadx/sources/com/android/support/killsign.java`
21. `sed -n '1,260p' artifacts/jadx/sources/com/android/support/Launcher.java`
22. `rg -n "com\.android\.support|CheckOverlayPermission|StartWithoutPermission|Start\(" artifacts/jadx/sources artifacts/apktool/smali*`
23. `nm -D artifacts/apktool/lib/arm64-v8a/libliteapks.so`
24. `nm -D artifacts/apktool/lib/arm64-v8a/libliteapks.so | rg "Java_|JNI_OnLoad|RegisterNatives|dlopen|dlsym|mprotect|memcpy|memmove"`
25. `nm -D artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so | rg "Java_|JNI_OnLoad|RegisterNatives|dlopen|dlsym|mprotect|memcpy|memmove"`
26. `nm -D artifacts/apktool/lib/arm64-v8a/libprotectliteapks.so | rg "Java_|JNI_OnLoad|RegisterNatives|dlopen|dlsym|mprotect|memcpy|memmove"`
27. `strings -a -n 4 artifacts/apktool/lib/arm64-v8a/libliteapks.so | rg -n "libil2cpp|libunity|Dobby|MSHook|Whale|xhook|shadow|bhook|And64|SandHook|mprotect|dlopen|dlsym|il2cpp|offset|0x[0-9A-Fa-f]{4,}|Feature|Toggle|hack|mod|menu|Firestone|Holyday|Update|Damage|Gold|Gem|Premium|VIP|Coin|Reward|Ads"`
28. `strings -a -n 4 artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so | rg -n "libil2cpp|libunity|Dobby|MSHook|Whale|xhook|shadow|bhook|And64|SandHook|mprotect|dlopen|dlsym|il2cpp|offset|0x[0-9A-Fa-f]{4,}|Feature|Toggle|hack|mod|menu|Firestone|Holyday|Update|Damage|Gold|Gem|Premium|VIP|Coin|Reward|Ads"`
29. `strings -a -n 4 artifacts/apktool/lib/arm64-v8a/libprotectliteapks.so | rg -n "libil2cpp|libunity|Dobby|MSHook|Whale|xhook|shadow|bhook|And64|SandHook|mprotect|dlopen|dlsym|il2cpp|offset|0x[0-9A-Fa-f]{4,}|Feature|Toggle|hack|mod|menu|Firestone|Holyday|Update|Damage|Gold|Gem|Premium|VIP|Coin|Reward|Ads"`
30. `readelf -d artifacts/apktool/lib/arm64-v8a/libliteapks.so`
31. `readelf -d artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so`
32. `r2 -q -e scr.color=false -A -c 's sym.JNI_OnLoad; pdf' artifacts/apktool/lib/arm64-v8a/libliteapks.so`
33. `r2 -q -e scr.color=false -e bin.cache=true -e bin.relocs.apply=true -A -c 's 0x84e64; pdf; s 0x85938; pdf; s 0x85c24; pdf' artifacts/apktool/lib/arm64-v8a/libliteapks.so`
34. `r2 -q -e scr.color=false -e bin.cache=true -e bin.relocs.apply=true -A -c 's 0x84e64; pdr' artifacts/apktool/lib/arm64-v8a/libliteapks.so`
35. `rabin2 -zz artifacts/apktool/lib/arm64-v8a/libliteapks.so | rg "libil2cpp|il2cpp_| at offset|offset|libunity|Dobby|MSHook|mprotect|dlopen|dlsym"`
36. `r2 -q -e scr.color=false -e bin.cache=true -e bin.relocs.apply=true -A -c 'axt @ 0x480ef; axt @ 0x4816f; axt @ 0x485bd; axt @ 0x4722f; axt @ 0x47ba2' artifacts/apktool/lib/arm64-v8a/libliteapks.so`
37. `r2 -q -e scr.color=false -e bin.cache=true -e bin.relocs.apply=true -A -c 's 0x874e4; pdf; s 0xfaffc; pdf' artifacts/apktool/lib/arm64-v8a/libliteapks.so`
38. `r2 -q -e scr.color=false -e bin.cache=true -e bin.relocs.apply=true -A -c 's 0xfaffc; pdr' artifacts/apktool/lib/arm64-v8a/libliteapks.so`
39. `r2 -q -e scr.color=false -e bin.cache=true -e bin.relocs.apply=true -A -c 'axt @ sym.imp.mprotect; axt @ sym.imp.dlopen; axt @ sym.imp.dlsym; axt @ sym.imp.memcpy' artifacts/apktool/lib/arm64-v8a/libliteapks.so`
40. `r2 -q -e scr.color=false -e bin.cache=true -e bin.relocs.apply=true -A -c 's 0x8e340; pdf; s sym.AMMU4EjfeJ2JvcEHoJke2iGl; pdf' artifacts/apktool/lib/arm64-v8a/libliteapks.so`
41. `r2 -q -e scr.color=false -e bin.cache=true -e bin.relocs.apply=true -A -c 'axt @ sym.AMMU4EjfeJ2JvcEHoJke2iGl; axt @ 0x8e364' artifacts/apktool/lib/arm64-v8a/libliteapks.so`
42. `which llvm-objdump`
43. `which objdump`
44. `which gobjdump`
45. `which aarch64-linux-android-objdump`
46. `r2 -q -e scr.color=false -e bin.cache=true -e bin.relocs.apply=true -A -c 's 0x88000; pD 0x2000' artifacts/apktool/lib/arm64-v8a/libliteapks.so`
47. `which ghidraRun`
48. `which analyzeHeadless`
49. `python3 -c "import capstone; print(capstone.__version__)"`
50. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libliteapks.so | rg -n "8e364|mprotect|dlopen|dlsym|libil2cpp|bl.*8e"`
51. `r2 -q -e scr.color=false -e bin.cache=true -e bin.relocs.apply=true -A -c 's 0x8a700; pdf; s 0x8d19c; pdf; s 0x8db80; pdf' artifacts/apktool/lib/arm64-v8a/libliteapks.so`
52. `gobjdump -d --start-address=0x8a6e0 --stop-address=0x8a7c0 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
53. `gobjdump -d --start-address=0x8d1c0 --stop-address=0x8d250 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
54. `gobjdump -d --start-address=0x8db90 --stop-address=0x8dc40 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
55. `r2 -q -e scr.color=false -e bin.cache=true -e bin.relocs.apply=true -A -c 'axt @ 0x88128; axt @ fcn.00088128; axt @ 0x874e4; axt @ fcn.000874e4' artifacts/apktool/lib/arm64-v8a/libliteapks.so`
56. `r2 -q -e scr.color=false -e bin.cache=true -e bin.relocs.apply=true -A -c 's 0x7d290; pdf' artifacts/apktool/lib/arm64-v8a/libliteapks.so`
57. `gobjdump -d --start-address=0x7d780 --stop-address=0x7d850 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
58. `gobjdump -d --start-address=0x7d840 --stop-address=0x7db50 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
59. `r2 -q -e scr.color=false -e bin.cache=true -e bin.relocs.apply=true -A -c 's 0x87b34; pdf; s 0x87b2c; pdf; s 0x87294; pdf' artifacts/apktool/lib/arm64-v8a/libliteapks.so`
60. `r2 -q -e scr.color=false -e bin.cache=true -e bin.relocs.apply=true -A -c 's 0x8731c; pdf' artifacts/apktool/lib/arm64-v8a/libliteapks.so`
61. `gobjdump -d --start-address=0x7daf0 --stop-address=0x7de80 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
62. `rabin2 -zz artifacts/apktool/lib/arm64-v8a/libliteapks.so | awk '$2 >= "0x00047000" && $2 <= "0x00048fff" {print}'`
63. `which Il2CppDumper`
64. `which il2cppdumper`
65. `which Cpp2IL`
66. `which dotnet`
67. `git clone --depth 1 https://github.com/Perfare/Il2CppDumper artifacts/tools/Il2CppDumper`
68. `dotnet publish artifacts/tools/Il2CppDumper/Il2CppDumper/Il2CppDumper.csproj -c Release -o artifacts/tools/Il2CppDumper/out`
69. `dotnet publish artifacts/tools/Il2CppDumper/Il2CppDumper/Il2CppDumper.csproj -f net8.0 -c Release -o artifacts/tools/Il2CppDumper/out`
70. `dotnet artifacts/tools/Il2CppDumper/out/Il2CppDumper.dll artifacts/apktool/lib/arm64-v8a/libil2cpp.so artifacts/apktool/assets/bin/Data/Managed/Metadata/global-metadata.dat artifacts/il2cppdump`
71. `find artifacts/il2cppdump -maxdepth 2 -type f -print | head -50`
72. `ls artifacts/tools/Il2CppDumper/out`
73. `sed -n '1,120p' artifacts/tools/Il2CppDumper/out/Il2CppDumper.runtimeconfig.json`
74. `find artifacts/tools/Il2CppDumper -name config.json -print`
75. `gobjdump -d --start-address=0x7d5c0 --stop-address=0x7d640 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
76. `rg -n "BattleHero|BattleCharacter|IsImmune|IsHero|GooglePlayLicense|GooglePlay|Check" artifacts/tools/Il2CppDumper/out/dump.cs`
77. `rg -n "BattleHero|BattleCharacter|IsImmune|IsHero|GooglePlayLicense|GooglePlay|Check" artifacts/tools/Il2CppDumper/out/script.json`
78. `rabin2 -zz artifacts/apktool/lib/arm64-v8a/libliteapks.so | rg "BattleHero|BattleCharacter|IsImmune|IsHero|GooglePlay|GooglePlayLicense|Check"`
79. `rg -n '"Name": "BattleHero\$\$IsImmune|"Name": "BattleCharacter\$\$IsHero|"Name": "GooglePlayLicense\$\$Check|"Name": "GooglePlay\.GooglePlayLicense\$\$Check|GooglePlayLicense\$\$Check' artifacts/tools/Il2CppDumper/out/script.json`
80. `sed -n '14350,14380p' artifacts/tools/Il2CppDumper/out/dump.cs`
81. `sed -n '14760,14790p' artifacts/tools/Il2CppDumper/out/dump.cs`
82. `sed -n '968840,968860p' artifacts/tools/Il2CppDumper/out/dump.cs`
83. `sed -n '16066,16082p' artifacts/tools/Il2CppDumper/out/script.json`
84. `sed -n '16540,16554p' artifacts/tools/Il2CppDumper/out/script.json`
85. `sed -n '1255966,1255986p' artifacts/tools/Il2CppDumper/out/script.json`
86. `readelf -l artifacts/apktool/lib/arm64-v8a/libil2cpp.so | sed -n '1,90p'`
87. `gobjdump -d --start-address=0x2ac0170 --stop-address=0x2ac01b0 artifacts/apktool/lib/arm64-v8a/libil2cpp.so`
88. `gobjdump -d --start-address=0x3ffd898 --stop-address=0x3ffd8d8 artifacts/apktool/lib/arm64-v8a/libil2cpp.so`
89. `gobjdump -d --start-address=0x3ffd8f0 --stop-address=0x3ffd930 artifacts/apktool/lib/arm64-v8a/libil2cpp.so`
90. `xxd -g 1 -l 32 -s 0x2abb170 artifacts/apktool/lib/arm64-v8a/libil2cpp.so && xxd -g 1 -l 32 -s 0x3ff9898 artifacts/apktool/lib/arm64-v8a/libil2cpp.so && xxd -g 1 -l 32 -s 0x3ff98f0 artifacts/apktool/lib/arm64-v8a/libil2cpp.so`
91. `xxd -g 1 -l 32 -s 0x2abc170 artifacts/apktool/lib/arm64-v8a/libil2cpp.so`
92. `gobjdump -d --start-address=0x7d8e0 --stop-address=0x7db10 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
93. `gobjdump -d --start-address=0x7d5e8 --stop-address=0x7d628 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
94. `gobjdump -d --start-address=0x8731c --stop-address=0x874fc artifacts/apktool/lib/arm64-v8a/libliteapks.so`
95. `gobjdump -d --start-address=0x87b20 --stop-address=0x87bc8 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
96. `gobjdump -d --start-address=0x7d780 --stop-address=0x7d8f0 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
97. `rabin2 -zz artifacts/apktool/lib/arm64-v8a/libliteapks.so | rg 'BattleCharacter|BattleHero|GooglePlayLicense|GooglePlay|libil2cpp|il2cpp_class_get_method_from_name|il2cpp_class_from_name'`
98. `gobjdump -d --start-address=0x874e4 --stop-address=0x876a8 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
99. `gobjdump -d --start-address=0x8db98 --stop-address=0x8dcc8 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
100. `gobjdump -d --start-address=0x8e340 --stop-address=0x8e450 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
101. `nm -D artifacts/apktool/lib/arm64-v8a/libliteapks.so | rg 'mprotect|dlopen|dlsym|memcpy|memmove|JNI_OnLoad'`
102. `rg -n 'findAndHookMethod|deoptimizeMethod|IXposedHook|IXposedHookLoadPackage|xposed_init|assets/xposed|System\.loadLibrary\("liteapks"\)|System\.loadLibrary\("menuliteapks"\)' artifacts/jadx/sources artifacts/apktool`
103. `r2 -q -e scr.color=false -e bin.cache=true -e bin.relocs.apply=true -A -c 'axt @ 0x10a308; axt @ 0x10a310; axt @ 0x10a318; axt @ 0x10a320; axt @ 0x10a328' artifacts/apktool/lib/arm64-v8a/libliteapks.so`
104. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libliteapks.so | rg -n '10a308|10a310|10a318|10a320|10a328|#776|#784|#792|#800|#808'`
105. `rg -n 'Feature|Toggle|IsImmune|immune|god|God|damage|Damage|license|License|Check|Google|BattleHero|BattleCharacter|Ares|menu' artifacts/jadx/sources/com/android/support artifacts/apktool/smali_classes2/com/android/support artifacts/apktool/smali/com/android/support`
106. `nm -D artifacts/apktool/lib/arm64-v8a/libliteapks.so | rg 'Java_com_android_support|Java_'`
107. `sed -n '1,140p' artifacts/jadx/sources/com/android/support/Preferences.java`
108. `sed -n '1,130p' artifacts/jadx/sources/com/android/support/Menu.java`
109. `sed -n '360,430p' artifacts/jadx/sources/com/android/support/Menu.java`
110. `strings -a -n 4 artifacts/apktool/lib/arm64-v8a/libliteapks.so | rg -n 'changeFeature|loadPref|featureList|getFeature|IsImmune|Battle|Google|License|God|Menu|Switch|CheckBox|StartWithoutPermission|Start\('`
111. `strings -a -n 4 artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so | rg -n 'changeFeature|loadPref|featureList|getFeature|IsImmune|Battle|Google|License|God|Menu|Switch|CheckBox|StartWithoutPermission|Start\('`
112. `rabin2 -zz artifacts/apktool/lib/arm64-v8a/libliteapks.so | rg 'changeFeature|loadPref|featureList|getFeature|IsImmune|Battle|Google|License|God|Menu|Switch|CheckBox'`
113. `rabin2 -zz artifacts/apktool/lib/arm64-v8a/libmenuliteapks.so | rg 'changeFeature|loadPref|featureList|getFeature|IsImmune|Battle|Google|License|God|Menu|Switch|CheckBox'`
114. `gobjdump -d --start-address=0x7e720 --stop-address=0x7e7f0 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
115. `gobjdump -d --start-address=0xd5960 --stop-address=0xd5c00 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
116. `gobjdump -d --start-address=0xd5f20 --stop-address=0xd6190 artifacts/apktool/lib/arm64-v8a/libliteapks.so`
117. `for f in artifacts/apktool/lib/arm64-v8a/*.so; do printf '%s\n' "$f"; strings -a -n 4 "$f" | rg 'libil2cpp|libunity|il2cpp_|Dobby|MSHook|Whale|xhook|shadowhook|bhook|SandHook|And64InlineHook|mprotect|dlopen|dlsym' || true; done`
118. `find artifacts/apktool -path '*xposed*' -o -path '*Xposed*' -o -name 'xposed_init' -o -name '*module*' | sed -n '1,120p'`
119. `find artifacts/apktool/lib/arm64-v8a -maxdepth 1 -type f -name '*.so' -print -exec sh -c 'printf "  "; nm -D "$1" 2>/dev/null | rg "dlopen|dlsym|mprotect|memcpy|memmove|JNI_OnLoad" || true' sh {} \;`
120. `strings -a -n 4 artifacts/apktool/lib/arm64-v8a/libmain.so | rg -n 'libil2cpp|libunity|il2cpp_|Dobby|MSHook|Whale|xhook|shadowhook|bhook|SandHook|And64InlineHook|mprotect|dlopen|dlsym|hook|patch|offset|Battle|Google|License|IsImmune|Check'`
121. `nm -D artifacts/apktool/lib/arm64-v8a/libmain.so | rg 'JNI_OnLoad|dlopen|dlsym|mprotect|Java_'`
122. `rabin2 -zz artifacts/apktool/lib/arm64-v8a/libmain.so | rg 'libil2cpp|libunity|il2cpp_|Dobby|MSHook|mprotect|dlopen|dlsym|Battle|Google|License|Check'`
123. `nl -ba artifacts/tools/Il2CppDumper/out/dump.cs | sed -n '14362,14372p'`
124. `nl -ba artifacts/tools/Il2CppDumper/out/dump.cs | sed -n '14772,14782p'`
125. `nl -ba artifacts/tools/Il2CppDumper/out/dump.cs | sed -n '968846,968858p'`
126. `nl -ba artifacts/tools/Il2CppDumper/out/script.json | sed -n '16544,16551p;1255972,1255982p'`
127. `nl -ba artifacts/jadx/sources/com/android/support/Main.java | sed -n '1,28p'`
128. `nl -ba artifacts/jadx/sources/com/android/support/menuprotect.java | sed -n '1,18p'`
129. `nl -ba artifacts/jadx/sources/com/unity3d/player/UnityPlayerActivity.java | sed -n '18,34p'`
130. `nl -ba artifacts/apktool/AndroidManifest.xml | sed -n '1,30p'`
131. `rg -n 'application|UnityPlayerActivity|Main' artifacts/apktool/AndroidManifest.xml`
132. `git status --short`
133. `ls -l ANALYSIS.md`
134. `rg -n '^#|0x2AC0170|0x3FFD898|0x3FFD8F0|Summary Table|Tooling Log|Open Questions|Evidence|No Java' ANALYSIS.md`
135. `git status --short`
136. `wc -l ANALYSIS.md`
137. `find artifacts/apktool/lib -maxdepth 2 -type f -name '*.so' | sort`
138. `ls -lh artifacts/apktool/assets/bin/Data/Managed/Metadata/global-metadata.dat artifacts/apktool/lib/arm64-v8a/libil2cpp.so`
139. `which r2; which rz || true; which ghidraRun || true; which analyzeHeadless || true; which Il2CppDumper || true; which il2cppinspector || true; which Il2CppInspector || true; which cpp2il || true; dotnet --version`
140. `brew --version`
141. `dotnet tool search Cpp2IL --take 10`
142. `dotnet tool search Il2CppInspector --take 10`
143. `dotnet tool search Il2Cpp --take 20`
144. `git clone --recursive --depth 1 https://github.com/LukeFZ/Il2CppInspectorRedux artifacts/tools/Il2CppInspectorRedux`
145. `git clone --recursive --depth 1 https://github.com/djkaty/Il2CppInspector artifacts/tools/Il2CppInspector`
146. `git clone --recursive --depth 1 https://github.com/SamboyCoding/Cpp2IL artifacts/tools/Cpp2IL`
147. `find artifacts/tools/Il2CppInspectorRedux -maxdepth 2 -type f \( -name '*.sln' -o -name '*.csproj' -o -name 'README*' \) | sort | head -50`
148. `find artifacts/tools/Il2CppInspector -maxdepth 2 -type f \( -name '*.sln' -o -name '*.csproj' -o -name 'README*' \) | sort | head -50`
149. `find artifacts/tools/Cpp2IL -maxdepth 2 -type f \( -name '*.sln' -o -name '*.csproj' -o -name 'README*' \) | sort | head -50`
150. `rg -n "<TargetFramework|<TargetFrameworks|<OutputType" artifacts/tools/Il2CppInspectorRedux/*.sln artifacts/tools/Il2CppInspectorRedux/**/*.csproj`
151. `rg -n "<TargetFramework|<TargetFrameworks|<OutputType" artifacts/tools/Il2CppInspector/*.sln artifacts/tools/Il2CppInspector/**/*.csproj`
152. `rg -n "<TargetFramework|<TargetFrameworks|<OutputType" artifacts/tools/Cpp2IL/**/*.csproj`
153. `dotnet build artifacts/tools/Il2CppInspectorRedux/Il2CppInspector.Redux.CLI/Il2CppInspector.Redux.CLI.csproj -c Release`
154. `dotnet build artifacts/tools/Il2CppInspector/Il2CppInspector.CLI/Il2CppInspector.CLI.csproj -c Release`
155. `dotnet build artifacts/tools/Cpp2IL/Cpp2IL/Cpp2IL.csproj -c Release`
156. `brew info dotnet || true`
157. `brew install dotnet`
158. `/opt/homebrew/bin/dotnet --version`
159. `DOTNET_ROOT=/opt/homebrew/opt/dotnet/libexec /opt/homebrew/bin/dotnet build artifacts/tools/Il2CppInspectorRedux/Il2CppInspector.Redux.CLI/Il2CppInspector.Redux.CLI.csproj -c Release`
160. `DOTNET_ROOT=/opt/homebrew/opt/dotnet/libexec /opt/homebrew/bin/dotnet build artifacts/tools/Cpp2IL/Cpp2IL/Cpp2IL.csproj -c Release`
161. `DOTNET_ROOT=/opt/homebrew/opt/dotnet/libexec /opt/homebrew/bin/dotnet publish artifacts/tools/Il2CppInspector/Il2CppInspector.CLI/Il2CppInspector.CLI.csproj -c Release -r osx-arm64 --self-contained true -p:RuntimeIdentifier=osx-arm64 -p:PublishSingleFile=false`
162. `DOTNET_ROOT=/opt/homebrew/opt/dotnet/libexec /opt/homebrew/bin/dotnet artifacts/tools/Il2CppInspectorRedux/Il2CppInspector.Redux.CLI/bin/Release/net10.0/Il2CppInspector.Redux.CLI.dll --help`
163. `DOTNET_ROOT=/opt/homebrew/opt/dotnet/libexec /opt/homebrew/bin/dotnet artifacts/tools/Il2CppInspectorRedux/Il2CppInspector.Redux.CLI/bin/Release/net10.0/Il2CppInspector.Redux.CLI.dll process --help`
164. `rm -rf artifacts/tools/Il2CppInspectorRedux/out && DOTNET_ROOT=/opt/homebrew/opt/dotnet/libexec /opt/homebrew/bin/dotnet artifacts/tools/Il2CppInspectorRedux/Il2CppInspector.Redux.CLI/bin/Release/net10.0/Il2CppInspector.Redux.CLI.dll process artifacts/apktool/lib/arm64-v8a/libil2cpp.so artifacts/apktool/assets/bin/Data/Managed/Metadata/global-metadata.dat -o artifacts/tools/Il2CppInspectorRedux/out --output-csharp-stub --output-dummy-dlls --output-disassembler-metadata --disassembler ghidra`
165. `DOTNET_ROOT=/opt/homebrew/opt/dotnet/libexec /opt/homebrew/bin/dotnet artifacts/tools/Cpp2IL/Cpp2IL/bin/Release/net10.0/Cpp2IL.dll --help`
166. `DOTNET_ROOT=/opt/homebrew/opt/dotnet/libexec /opt/homebrew/bin/dotnet artifacts/tools/Cpp2IL/Cpp2IL/bin/Release/net10.0/Cpp2IL.dll --list-output-formats`
167. `strings artifacts/apktool/lib/arm64-v8a/libunity.so | rg -m 10 '^[0-9]{4}\.[0-9]\.[0-9]{1,2}f[0-9]' || true`
168. `rm -rf artifacts/tools/Cpp2IL/out && DOTNET_ROOT=/opt/homebrew/opt/dotnet/libexec /opt/homebrew/bin/dotnet artifacts/tools/Cpp2IL/Cpp2IL/bin/Release/net10.0/Cpp2IL.dll --force-binary-path artifacts/apktool/lib/arm64-v8a/libil2cpp.so --force-metadata-path artifacts/apktool/assets/bin/Data/Managed/Metadata/global-metadata.dat --force-unity-version 6000.0.47f1 --output-as diffable-cs --output-to artifacts/tools/Cpp2IL/out --low-memory-mode`
169. `nl -ba artifacts/tools/Il2CppDumper/out/dump.cs | sed -n '14190,14370p'`
170. `nl -ba artifacts/tools/Il2CppDumper/out/dump.cs | sed -n '14430,14900p'`
171. `nl -ba artifacts/tools/Il2CppDumper/out/dump.cs | sed -n '14980,15240p'`
172. `nl -ba artifacts/tools/Il2CppDumper/out/dump.cs | sed -n '712900,713230p'`
173. `rg -n 'class CharacterAnimationController|SetAnimationTimeScale|AnimationTimeScale|set_WaveTransitionSpeedMultiplier|WaveTransitionSpeedMultiplier|class TeamLogic' artifacts/tools/Il2CppDumper/out/dump.cs`
174. `rg -n 'TimeScale|timeScale|animationTimeScale|AttackSpeed|attackSpeed|Cool(down|Down)|cooldown|interval|Rate|Haste|MovementSpeed|moveSpeed|RunDuration|CustomRunDuration' artifacts/tools/Il2CppDumper/out/dump.cs | head -300`
175. `node - <<'NODE' ... selected method lookup from artifacts/tools/Il2CppDumper/out/script.json ... NODE`
176. `node - <<'NODE' ... broad candidate regex search over ScriptMethod entries ... NODE`
177. `rabin2 -I artifacts/apktool/lib/arm64-v8a/libil2cpp.so && rabin2 -S artifacts/apktool/lib/arm64-v8a/libil2cpp.so | head -40`
178. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x2ac2af8 --stop-address=0x2ac2b90`
179. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x29e5b94 --stop-address=0x29e5c40`
180. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x29e1ac8 --stop-address=0x29e1b80`
181. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x27c4e10 --stop-address=0x27c4ed0`
182. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x2ac82e8 --stop-address=0x2ac83b8`
183. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x4a0eaa4 --stop-address=0x4a0eb40`
184. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x4a0e9f4 --stop-address=0x4a0ea40`
185. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x2ac9814 --stop-address=0x2ac98d0`
186. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x2ac99dc --stop-address=0x2ac9a90`
187. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x2b78918 --stop-address=0x2b78a00`
188. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x436aac8 --stop-address=0x436ab30`
189. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x436b85c --stop-address=0x436b8d0`
190. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x2b34fb0 --stop-address=0x2b35080`
191. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x2b1e1a4 --stop-address=0x2b1e260`
192. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x2ac2290 --stop-address=0x2ac2350`
193. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x29e5aac --stop-address=0x29e5b40`
194. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x29e5628 --stop-address=0x29e56e0`
195. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x29e5d48 --stop-address=0x29e5e30`
196. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x29e59d8 --stop-address=0x29e5a90`
197. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x2abffc0 --stop-address=0x2ac0060`
198. `gobjdump -d artifacts/apktool/lib/arm64-v8a/libil2cpp.so --start-address=0x29e186c --stop-address=0x29e1920`
199. `node - <<'NODE' ... AArch64 B/BL direct branch scanner and method-name mapper ... NODE`
200. `ps aux | rg 'Il2CppInspector|dotnet.*Redux|36971' || true`
201. `kill 40278 && sleep 1; ps -p 40278 -o pid=,stat=,command= || true`
202. `kill -9 40278 || true`
203. `find artifacts/tools/Il2CppInspectorRedux/out -maxdepth 3 -type f | sed -n '1,80p'`
204. `find artifacts/tools/Cpp2IL/out -maxdepth 3 -type f | sed -n '1,80p'`
205. `find artifacts/tools/Il2CppDumper/out -maxdepth 2 -type f | sort | sed -n '1,80p'`
206. `rg -n 'class (BattleController|BattleCharacter|BattleEnemy|BattleMainEnemy|BattleMainHero|CharacterBattleLogic|TeamLogic|HeroDeck|Hero|CharacterAnimationController)|ApplyDamage\(|ReceiveDamage\(|SetCurrentHealth\(|CalculateAttackSpeedTotal\(|FillAttackSpeedTimer\(|ApplyWaveTransitionSpeedModifierToDuration\(|set_timeScale|get_fixedDeltaTime|PlaySpecialAnimationSequence\(|CalculateBasicAttackSpeed\(' artifacts/tools/Cpp2IL/out/DiffableCs -S | head -200`
207. `rg -n 'class (BattleController|BattleCharacter|BattleEnemy|BattleMainEnemy|BattleMainHero|CharacterBattleLogic|TeamLogic|HeroDeck|Hero|CharacterAnimationController)|ApplyDamage\(|ReceiveDamage\(|SetCurrentHealth\(|CalculateAttackSpeedTotal\(|FillAttackSpeedTimer\(|ApplyWaveTransitionSpeedModifierToDuration\(|set_timeScale|get_fixedDeltaTime|PlaySpecialAnimationSequence\(|CalculateBasicAttackSpeed\(' artifacts/tools/Il2CppInspectorRedux/out/cs/il2cpp.cs -S | head -200`
208. `rg -n 'BattleController..ApplyDamage|BattleCharacter..ReceiveDamage|BattleMainHero..CalculateAttackSpeedTotal|TeamLogic..ApplyWaveTransitionSpeedModifierToDuration|UnityEngine.Time..set_timeScale|CharacterBattleLogic..FillAttackSpeedTimer' artifacts/tools/Il2CppInspectorRedux/out/il2cpp.json artifacts/tools/Il2CppInspectorRedux/out/il2cpp.py artifacts/tools/Il2CppInspectorRedux/out/il2cpp.h -S | head -100`
209. `rg -n '^## |^# ' ANALYSIS.md`
210. `sed -n '1088,1105p' ANALYSIS.md`
211. `tail -120 ANALYSIS.md`

Additional non-shell tools/actions used:

- Web search/open for Dobby, xHook, Xposed API, and Il2CppDumper references.
- `apply_patch` to create and update this report.
