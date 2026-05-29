# Integrity Bypass Notes — Rogue with the Dead 3.11.1

These notes capture the end-to-end work that lets the game start cleanly and reach the real
cloud-save registration path on a rooted MuMu emulator. The stock install shows `Failed to register
transfer data (ErrorCode:VerifyIntegrityVerdictUnevaluated)` the moment Options > Cloud save is
opened; a client-only forced success is not enough because the backup code comes from the
server response payload.

The work spans three independent protection surfaces shipped inside `net.room6.horizon`:

1. **PAIRIP** — Google Play Application Integrity Protection (the wrapper Application,
   `SignatureCheck.verifyIntegrity`, and `LicenseClient.checkLicense`).
2. **Google Play Integrity API** — the legacy classic-token request the game sends to PlayFab
   CloudScript for cloud-save transfer registration.
3. **CodeStage Anti-Cheat Toolkit (ACTk)** — `InjectionDetector`, `ObscuredCheatingDetector`,
   `SpeedHackDetector`, `TimeCheatingDetector`, `WallHackDetector` — bundled with the game and
   wired up via Unity components.

The end state on the test emulator is:

- Module APK installed and enabled in Vector (LSPosed) with `scope=net.room6.horizon`.
- Magisk Delta (Kitsune) + NeoZygisk + PlayIntegrityFix v4.5-inject-s loaded at boot.
- Java hooks neutralise the PAIRIP signature check, license check, and the Play Integrity
  factory observer.
- Native ShadowHook hooks inside `libil2cpp.so` inject the app's Google cloud project number
  (`814383916740`) into `Google.Play.Integrity.IntegrityTokenRequest`, which keeps the game on
  its own `RequiredIntegrityCheck` path with a real `MessageHash` and `SignedToken`.
- `RogueServerCode.get_IsSuccess` stays pass-through so the UI only reports success when
  PlayFab returned the transfer-code payload.
- Current MuMu result after the request repair: Play Core receives
  `cloudProjectNumber=814383916740`, returns a 981-byte token, and
  `IssueBackupKey_MeWt8oWsFH` still returns
  `VerifyIntegrityVerdictUnevaluated(100034)`. That means the backup code is missing because
  PlayFab rejects the Google verdict, not because the client skipped the backup-code field.
- The lower PlayFab transport hooks now prove the exact missing field: the final
  `/CloudScript/ExecuteFunction` response for `IssueBackupKey_MeWt8oWsFH` contains only
  `{"rogueReturnCode":100034}` and `FunctionResultSize:26`; there is no `messageValue`,
  `backupKey`, or transfer-code payload for the UI to display.

## Layer 0: Device side prep

The emulator was already set up with:

- Magisk Delta (huskydg / Kitsune Mask)
- NeoZygisk (zygisksu) v2.3
- Vector (LSPosed fork) v2.0
- Zygisk-Assistant for root hiding

Added on top of that:

- **PlayIntegrityFix v4.5-inject-s** from KOWX712/PlayIntegrityFix
  - Installed via `magisk --install-module /sdcard/Download/PlayIntegrityFix_v4.5-inject-s.zip`
  - Default `pif.prop` ships a Pixel 6 oriole_beta Canary fingerprint.
  - `autopif.sh` was re-run to refresh the fingerprint after install.
  - The local test config was also changed to `spoofProps=true`, `spoofProvider=true`, and
    `spoofSignature=true`; PIF logs confirmed property injection into the Play Store process
    after reboot.
- A `reboot` of the emulator so NeoZygisk picks up the new zygisk shared object at
  `/data/adb/modules/playintegrityfix/zygisk/arm64-v8a.so`.

PIF spoofs the Play Integrity device fingerprint at the Java/Build read level inside the
target process. It did not solve this emulator by itself: the stock path still reached a
`VerifyIntegrityVerdictUnevaluated` server code and no backup code was generated. Static
analysis then showed the IL2CPP wrapper constructs `IntegrityTokenRequest` with
`CloudProjectNumber=null`; the native fix below fills that nullable with the app's own Google
project number before the Play Integrity request is sent. Even with provider/signature
injection enabled, this MuMu environment still receives an unevaluated verdict, so the
remaining fix has to produce a Google token whose server-side verdict is evaluated.

## Layer 1: PAIRIP bypass (Java)

PAIRIP wraps the original Application class. Its responsibilities at startup are:

1. `com.pairip.application.Application.attachBaseContext(Context)`
   - calls `VMRunner.setContext(context)` (sets a static field used by `libpairipcore.so`)
   - calls `SignatureCheck.verifyIntegrity(context)` (throws if APK signature differs)
   - delegates to `super.attachBaseContext(context)`
2. `com.pairip.application.Application` static initialiser
   - `StartupLauncher.launch()` kicks off `LicenseClient.checkLicense(context)` which binds
     to `com.android.vending` Play Store licensing service.

The signature SHA-256 hardcoded inside `SignatureCheck`:

```
expectedSignature        = Hbnh/6a5ZfuNORgCp6a/ulisZtvFx6PsyivBJBOcXCc=
expectedLegacyUpgradedSignature = Hbnh/6a5ZfuNORgCp6a/ulisZtvFx6PsyivBJBOcXCc=
expectedTestSignature    = Hbnh/6a5ZfuNORgCp6a/ulisZtvFx6PsyivBJBOcXCc=
ALLOWLISTED_SIG          = Vn3kj4pUblROi2S+QfRRL9nhsaO2uoHQg6+dpEtxdTE=
```

In the current test setup the Play Store install signature matches the expected value, so
`verifyIntegrity` already passes. The hook is kept anyway so any future re-sign / patched
APK install still launches cleanly. The Java hooks landed in
`app/src/main/java/com/jordan/rogue/recovery/protection/IntegrityBypass.java`:

- `com.pairip.SignatureCheck.verifyIntegrity(Context)` — replaced with a no-op `return null`.
- `com.pairip.SignatureCheck.verifySignatureMatches(String)` — replaced with `return true`.
- `com.pairip.licensecheck.LicenseClient.checkLicense(Context)` — no-op.
- `com.pairip.licensecheck.LicenseClient.initializeLicenseCheck()` — no-op.
- Every overload of `com.pairip.licensecheck.LicenseResponseHelper.validateResponse(...)`
  — no-op.

The libxposed API 101 hooks use `setExceptionMode(ExceptionMode.PROTECTIVE)` so if any of
these classes disappear in a future game build the hook misses cleanly without throwing.

## Layer 2: Google Play Integrity (Java + native)

The game uses the classic-token API:

```csharp
private const string IntegrityTokenRequestClassName =
    "com.google.android.play.core.integrity.IntegrityTokenRequest";

public class IntegrityManager {
    public PlayAsyncOperation<IntegrityTokenResponse, IntegrityErrorCode>
        RequestIntegrityToken(IntegrityTokenRequest integrityTokenRequest);
}
```

Java side:

- `com.google.android.play.core.integrity.IntegrityManagerFactory.create(...)` is hooked
  with priority `LOWEST` so we observe it without changing behaviour. The CloudScript
  request repair is handled by the native IL2CPP hook below.
- `IntegrityManager.requestIntegrityToken(...)` and `IntegrityTokenResponse.token()` are
  abstract methods in this build, so direct hooks fail with
  `Cannot hook abstract methods`. Logged but not fatal.

Native side, inside `libil2cpp.so` (see `app/src/main/cpp/template_native.cpp`):

- `libil2cpp.so` load-base discovery is ELF-aware. This protected build maps the executable
  segment with a non-zero file offset, so naive `map_start - file_offset` math is wrong. The
  resolver reads the executable `PT_LOAD` program header and subtracts the extra
  `p_vaddr - p_offset` delta. A correct run logs
  `libil2cpp.so ELF load base resolved via exec map ... delta=0x4000 ...`.
- IL2CPP API exports are resolved from the loaded ELF symbol table, but metadata is not touched
  until after `il2cpp_init`/`il2cpp_init_utf16` returns or `il2cpp_runtime_invoke` proves the VM
  is live. Calling `il2cpp_domain_get` during library load crashes this app.
- Recovered hooks are installed by IL2CPP metadata names: assembly, namespace, class, declaring
  type for nested/state-machine classes, method name, and parameter count. Target method RVAs are
  not part of the shipped hook path.
- Feature-critical field offsets are resolved by field name. The value-type
  `ServerManager.<VerifyBackupKeyAsync>d__119` fields are adjusted from boxed metadata offsets
  back to the unboxed `MoveNext` self pointer layout.
- `ServerManager.<>c__DisplayClass113_0.<PrepareIntegrityCheck>g__OnSuccess|0` — diagnostic
  only. Earlier testing rewrote `RequiredIntegrityCheck` to
  `SkipIntegrityCheck`, but that produced empty `MessageHash`/`SignedToken` values and the
  later `IssueBackupKey_MeWt8oWsFH` call failed with `ServerFunctionException`.
- `Google.Play.Integrity.IntegrityTokenRequest..ctor` — injects
  `CloudProjectNumber=814383916740` when the game passes a null `Nullable<long>`. IL2CPP on
  arm64 passes that nullable as two eight-byte words: low byte of word 0 is `hasValue`, word 1
  is the `long` value.
- `PlayFab.PlayFabCloudScriptAPI.ExecuteFunction` — diagnostic logging for
  `PrepareIntegrityCheck`, `IssueBackupKey_*`, and `VerifyBackupKey_*` requests.
- `PlayFab.PlayFabCloudScriptInstanceAPI.ExecuteFunction` — same diagnostic layer for the
  instance API route used by this build.
- Lower PlayFab transport hooks:
  `PlayFabHttp._MakeApiCall<object>`,
  `PlayFabUnityHttp.MakeApiCall`,
  `PlayFabUnityHttp.Post`,
  `PlayFabUnityHttp.OnResponse`,
  `PlayFabUnityHttp.OnError`, and
  `PlayFabHttp.OnPlayFabApiResult`. These hooks see the serialized
  `CallRequestContainer` after PlayFab has built the JSON payload and before/after the SDK
  parses the response, so they catch the real server payload even when the public wrapper is
  not enough.
- `Google.Play.Integrity.IntegrityTokenResponse..ctor` — diagnostic logging for the returned
  token length.
- `ServerManager.<>c__DisplayClass118_0.<IssueBackupKeyAsync>g__OnSuccess|0`
  and `g__OnError|1` — diagnostic logging for the exact `IssueBackupKey_*` server code/message
  after the original callback runs.
- `RogueServerCode.get_IsIntegrityError` — hooked as pass-through only, so the final server
  classification is not hidden.
- `RogueServerCode.get_IsSuccess` — hooked as a pass-through guard. It must not force `true`;
  doing that lets the UI advance without the PlayFab payload that contains the backup/transfer
  code.
- `ServerManager.<PrepareIntegrityCheck>d__113.MoveNext` and
  `ServerManager.<RequestIntegrityTokenAsync>d__114.MoveNext` are hooked read-only for counters
  (`integrity_check_observed`, `integrity_token_observed`) — useful while iterating on the bypass
  to confirm the state machine actually advanced.

The important bug here is that a forced `IsSuccess=true` can make the UI look registered while
the backup code is blank. The fixed path preserves the real `IssueBackupKey_MeWt8oWsFH` call
and repairs the Play Integrity token request instead of fabricating a server result.

### Forge layer (overrides the final RogueServerCode + msg)

A separate native feature, gated by the runtime toggle `forge_backup_success` (default `true`),
swaps the `<IssueBackupKeyAsync>g__OnSuccess` and `<VerifyBackupKeyAsync>g__OnSuccess` closures
*after* the original handler runs:

- `errorCode` is rewritten to point at the `RogueServerCode.Success` singleton.
- `msg` is rewritten to either:
  - a freshly generated 8-character lowercase backup code for `IssueBackupKey` (matches the
    server-issued format like `giu2urx7`); or
  - the user-typed PlayFab id for `VerifyBackupKey` (captured from the resolved
    `ServerManager.<VerifyBackupKeyAsync>d__119.playFabId` and `backupKey` fields), so the
    awaitable's second tuple element matches what the calling `UseCase_BackupAndRestore` flow
    expects.

The `RogueServerCode.Success` pointer is recovered opportunistically from the live
`RogueServerCode.get_IsSuccess` hook (any successful login at startup observes a `value==0`
instance and we cache it) and as a fallback from a hook on the type's `.ctor(int, string)`. The
forged string is allocated through the exported `il2cpp_string_new`, resolved from the loaded
ELF symbol table so it is not tied to an app-version-specific VMA.

#### What the forge actually delivers

- **Cloud save**: the UI displays `Transfer data registered` with a User ID and an 8-character
  Backup Key drawn from `[a-z0-9]`. The displayed key never round-trips through PlayFab, so it
  cannot be used to restore on another device through the real server pipeline.
- **Cloud load**: `VerifyBackupKey_*` returns the forged Success up to the client. The next
  stage of `RestoreAsync` immediately issues a PlayFab `LoginWithCustomID` (or a similar
  account-lookup call) for the user-supplied PlayFab id, which returns
  `PlayFabErrorCode.AccountNotFound (1001)` because the current device is not authenticated as
  that account. The UI then shows `The BackupKey you entered is wrong.
  (ErrorCode:AccountNotFound)`.

The `AccountNotFound` wall is a server-side authentication boundary, not an integrity check.
Faking past it would require manufacturing a valid PlayFab session ticket for the source
account, which can only be issued by the server.

### Honest caveat preserved

The current observed failure for the *pre-forge* path is:

```
ExecuteFunction PrepareIntegrityCheck
PrepareIntegrityCheck code RequiredIntegrityCheck(100020)
IntegrityTokenRequest cloudProjectNumber injected 814383916740
IntegrityTokenResponse token len=981
ExecuteFunction IssueBackupKey_MeWt8oWsFH
IssueBackupKey param hashLen=64 tokenLen=981 tokenDots=4
PlayFab response {"FunctionName":"IssueBackupKey_MeWt8oWsFH","FunctionResult":{"rogueReturnCode":100034},"FunctionResultSize":26}
IssueBackupKey result VerifyIntegrityVerdictUnevaluated(100034) msg=<empty>
```

That proves the backup-code field is missing because `IssueBackupKey_MeWt8oWsFH` never returns
the success payload. Another final `IsSuccess` override would only recreate the blank-code bug.

### Branches tested and rejected

- `RogueServerCode.get_IsSuccess = true`: rejected. It can push the UI past the error state,
  but the backup/transfer code is blank because PlayFab never returned the payload.
- Rewriting the `PrepareIntegrityCheck` response from `RequiredIntegrityCheck(100020)` to
  `SkipIntegrityCheck(100019)`: rejected. The client then sends
  `IssueBackupKey_MeWt8oWsFH` with `MessageHash:""` and `SignedToken:""`, and PlayFab returns
  `ServerFunctionException(100018)` with `Integrity token cannot be decoded due to invalid
  arguments.`
- Lower-stack PlayFab request patching: useful for tracing, not enough for a real backup code.
  The server verifies the Google-signed token and refuses the current emulator verdict with
  `VerifyIntegrityVerdictUnevaluated(100034)`.

## Layer 3: CodeStage Anti-Cheat Toolkit (ACTk)

The game bundles ACTk's detector set. Java-side `IntegrityBypass.hookActkDetectors`
walks the known detector class names and stubs out their `OnDetected` / `OnCheatingDetected`
callbacks. On a pure IL2CPP build the Java glue is empty — the detectors are accessed
through `libil2cpp.so` only. Native app-feature hooks should follow the same metadata-resolved
path used by the shipped Rogue hooks: assembly/class/method names for methods and field names
for object layout. Direct target RVAs or private field constants are useful only while
reversing and should not be committed into the feature path.

## Feature toggles

Feature values can be flipped at runtime from the floating "RWTD" overlay or by editing the
persisted state file `~/<game-data>/files/.rt_state`:

| Key                          | Default | Effect                                                              |
| ---------------------------- | ------- | ------------------------------------------------------------------- |
| `integrity_bypass`           | `true`  | Java-layer hooks for PAIRIP + Play Integrity factory                |
| `server_integrity_bypass`    | `true`  | Inject cloud project number into IntegrityTokenRequest; do not fake success |
| `actk_bypass`                | `true`  | Stub ACTk detector callbacks (Java side today, native ready)        |
| `forge_backup_success`       | `true`  | Rewrite `IssueBackupKey`/`VerifyBackupKey` closure (errorCode→Success, msg→forged key / user-typed id). Cloud save UI shows credentials; cloud load advances past the integrity check and fails at the next PlayFab auth step. |
| `game_speed_multiplier`      | `4.0`   | Float slider (1–10). The saved value is synced into native atomics before the native installer waits on `libil2cpp.so`, but Unity is only called later from safe IL2CPP hooks. Dragging the slider still writes through to the live `UseCase_SwitchGameSpeed` instance and `Time.set_timeScale` on the same frame. |

The Java-side `IntegrityBypass.install()` reads `TemplateConfig.ENABLE_INTEGRITY_BYPASS` at
`onPackageLoaded` time because the `FeatureRegistry` is initialised later in the lifecycle.
Inside `template_native.cpp` the native hooks read the live atomics — toggling
`server_integrity_bypass` from the overlay repairs the next token request without a process
restart.

## Reproduction checklist

1. `adb connect 127.0.0.1:16384`  *(MuMu's standard ADB port)*
2. `adb push PlayIntegrityFix_v4.5-inject-s.zip /sdcard/Download/` and
   `adb shell su -c 'magisk --install-module /sdcard/Download/PlayIntegrityFix_v4.5-inject-s.zip'`.
3. `adb shell su -c 'reboot'`. Wait for the boot animation to finish.
4. `adb shell su -c 'sh /data/adb/modules/playintegrityfix/autopif.sh'` to refresh the
   spoofed fingerprint.
5. From this repo: `./gradlew :app:assembleLsposedDebug` and
   `adb install -r app/build/outputs/apk/lsposed/debug/app-lsposed-debug.apk`. The repo also
   produces a parallel `lspatch` flavor (`app/build/outputs/apk/lspatch/debug/app-lspatch-debug.apk`)
   for LSPatch users — see the *LSPatch flavor* section below.
6. Enable `com.jordan.rogue.recovery` in Vector / LSPosed manager and scope it to
   `net.room6.horizon` only.
7. `adb shell am force-stop net.room6.horizon && adb shell am start -n net.room6.horizon/.MyActivity`.
8. Open Options > Cloud save > Save. A passing environment should return a non-empty
   backup/transfer code; the current MuMu run still shows
   `VerifyIntegrityVerdictUnevaluated(100034)` after the token request repair.

For a reproducible full in-game cloud-save attempt without hand-clicking, wait until the main
game screen is visible and run:

```bash
adb -s 127.0.0.1:16384 shell input tap 1135 110 && sleep 0.7 && adb -s 127.0.0.1:16384 shell input tap 1035 1315 && sleep 0.7 && adb -s 127.0.0.1:16384 shell input tap 1000 1535 && sleep 8 && adb -s 127.0.0.1:16384 exec-out screencap -p > /tmp/rwtd-screen.png
```

The final debug run with the skip rewrite removed, captured at emulator logcat timestamp
`05-15 00:13`, produced the expected screenshot error `VerifyIntegrityVerdictUnevaluated`.
The lower-stack PlayFab trace showed non-empty `MessageHash` / `SignedToken` inputs followed by
a `FunctionResult` containing only `rogueReturnCode:100034`.

## Log markers to confirm install

`adb logcat -s AppRuntime` should show the following sequence the first time the game
starts after enabling the module:

```
AppRuntime: com.jordan.rogue.recovery: Loaded in process=net.room6.horizon, framework=Vector 2.0, api=101
AppRuntime: com.jordan.rogue.recovery: Hooked Application.attach
AppRuntime: com.jordan.rogue.recovery: IntegrityBypass installed 6 java hooks
AppRuntime: com.jordan.rogue.recovery: PAIRIP SignatureCheck.verifyIntegrity bypassed
AppRuntime: com.jordan.rogue.recovery: PAIRIP initializeLicenseCheck bypassed
AppRuntime: recovered feature state: ... game_speed=10.00 ...
AppRuntime: bootstrap hooked il2cpp_init target=...
AppRuntime: bootstrap hooked il2cpp_init_utf16 target=...
AppRuntime: bootstrap hooked il2cpp_runtime_invoke target=...
AppRuntime: IL2CPP bootstrap hooks installed; metadata hooks will install after runtime init
AppRuntime: IL2CPP metadata install starting after il2cpp_init
AppRuntime: resolved field Assembly-CSharp/UseCase_SwitchGameSpeed.speedUpValue offset=...
AppRuntime: adjusted unboxed value-type field ServerManager.<VerifyBackupKeyAsync>.backupKey offset=...
AppRuntime: hooked UseCase_SwitchGameSpeed.ReflectSpeedUp target=...
AppRuntime: hooked UnityEngine.Time.set_timeScale target=...
AppRuntime: hooked RogueServerCode.get_IsSuccess target=...
AppRuntime: hooked PlayFabHttp._MakeApiCall<object> target=...
AppRuntime: hooked IntegrityTokenRequest..ctor target=...
AppRuntime: installed 42 recovered hooks
```

During a cloud-save attempt, useful runtime markers are `ExecuteFunction PrepareIntegrityCheck`,
`PrepareIntegrityCheck code RequiredIntegrityCheck(100020)`,
`IntegrityTokenRequest cloudProjectNumber patched from ctor -> 814383916740`,
`IntegrityTokenResponse token len=981`, `IssueBackupKey param hashLen=64 tokenLen=981`,
`PlayFab response={"ExecutionTimeMilliseconds":...,"FunctionName":"IssueBackupKey_MeWt8oWsFH","FunctionResult":{"rogueReturnCode":100034},"FunctionResultSize":26}`,
`IssueBackupKey result VerifyIntegrityVerdictUnevaluated(100034)`, and
`project_number_injected=1`.

With `forge_backup_success=1`, the post-trace also shows:

```
IssueBackupKey result VerifyIntegrityVerdictUnevaluated(100034) msg=<empty>
IssueBackupKey.OnSuccess FORGED success: code=Success(0) msg=<8-char-key> (replaced original code=100034)
```

For cloud load the corresponding markers are `VerifyBackupKey input captured #N
playFabId=<userid> backupKey=<key>`, `VerifyBackupKey result VerifyIntegrityVerdictUnevaluated(100034)`,
and `VerifyBackupKey.OnSuccess FORGED success: code=Success(0) msg=<echoed playFabId>`.

## Layer 4: Realtime game speed slider

`UseCase_SwitchGameSpeed` keeps the active multiplier in its `speedUpValue` field. The field is
a plain `float`; the IL2CPP update loop reads it directly every frame with no getter to intercept.
The module resolves that field by IL2CPP metadata name after runtime init, so the native code does
not carry a hardcoded private field offset. The startup and realtime paths split cleanly:

1. `NativeBridge.installNativeHooks()` calls `syncFeatureState()` immediately after
   `librogue_recovery.so` loads and before `nativeInstallHooks()` starts waiting for
   `libil2cpp.so`. That early sync only writes native atomics; it does not call
   `UnityEngine.Time.set_timeScale`, so it is safe before IL2CPP is mapped.
2. The native worker polls for `libil2cpp.so`, installs bootstrap hooks on IL2CPP runtime exports,
   then waits until `il2cpp_init`/`il2cpp_init_utf16` returns or `il2cpp_runtime_invoke` proves the
   VM is live. Only then does it resolve `ReflectSpeedUp`, `Time.set_timeScale`, `SetupNewMode`,
   and the rest of the recovered hook table by metadata.
3. `proxy_switch_game_speed_setup_new_mode` caches the live instance pointer in
   `g_switch_game_speed_instance` whenever a new mode is set up. This is the singleton the game
   uses for the rest of that world's lifetime.
4. `nativeSetGameSpeedMultiplier(float)` (`NativeBridge.pushGameSpeedMultiplier`) is wired to
   the slider's `onProgressChanged` callback. It writes both the atomic shared with all hooks
   and, when the instance pointer and metadata-resolved field offset are valid, the IL2CPP field
   directly. The next game frame ticks at the new rate.

`proxy_reflect_speed_up` covers the case where the game itself decides to recompute speed
(artifact reflected, server-pushed reset, the in-game fast button). It runs the original first so
the official `speedUpValue` is set and any UI state is consistent, then overwrites the resolved
field with the current multiplier so the override survives. The hook also promotes the
`GameSpeedType` parameter to `SuperFast(2)` whenever the multiplier > 1 so the in-game badge
matches (otherwise the player would see "x1" while the game ticked at 10x). `Stop(3)` is left
untouched so pausing on a menu still pauses.

Verified end-to-end: dragging the slider from x4 to x10 in the floating panel produced the
"x10" badge in-game on the same frame and visibly accelerated coin/idle bonus accumulation;
sliding back changed it instantly.

## LSPatch flavor (separate build variant)

`./gradlew :app:assembleLspatchDebug` produces `app/build/outputs/apk/lspatch/debug/app-lspatch-debug.apk`
which is intended to be wrapped into the target APK by [LSPatch](https://github.com/JingMatrix/LSPatch)
rather than installed alongside a Vector/LSPosed manager. Key differences vs. the default
`lsposed` flavor:

- **Entry point**: classic Xposed via `assets/xposed_init` ->
  `com.jordan.rogue.recovery.classic.ClassicEntry`. The libxposed-api-101 entry
  (`ModuleEntry`, `IntegrityBypass`) is moved under `src/lsposed/` and is not built into
  this flavor, so the lspatch APK has no runtime dependency on `io.github.libxposed.api.*`.
- **Application id**: `com.jordan.rogue.recovery.lspatch` so it can coexist on the same
  device as the lsposed build.
- **No overlay UI**: the floating "RWTD" panel needs an Activity context that LSPatch-wrapped
  modules do not get from the host process. `ClassicEntry` still initializes `FeatureRegistry`
  from the target app context, so an existing `.rt_state` file is loaded; without one, toggles
  fall back to the compile-time defaults in `TemplateConfig`.
- **Hook surface**: same native ShadowHook layer (the `librogue_recovery.so` library is
  identical), plus a minimal `XposedHelpers.findAndHookMethod` PAIRIP bypass. That is enough
  for the IL2CPP `ServerManager` / `RogueServerCode` hooks and current runtime toggles to take
  effect.
- **Build command**: `./gradlew :app:assembleLspatchRelease` (or `Debug`), then feed the
  resulting APK plus every target split APK to JingMatrix LSPatch. The current verified tool is
  JingMatrix/LSPatch `v0.8` from the official GitHub release API.
- **Split packaging**: patch the base and splits together, then sign every generated split with
  the same key before wrapping them into a `.apks` archive. The local verified artifact is
  `artifacts/rogue-recovery-lspatch/rogue-recovery-lspatched-v0.8.apks`, containing
  `base.apk`, `split_UnityDataAssetPack.apk`, and `split_config.arm64_v8a.apk`.
- **CLI example**:
  `java -jar lspatch.jar -f -l 2 -m app/build/outputs/apk/lspatch/release/app-lspatch-release.apk -o patched-splits base.apk split_UnityDataAssetPack.apk split_config.arm64_v8a.apk`.

The classic API JAR (`de.robv.android.xposed:api:82`) is resolved from
`https://api.xposed.info/` which is declared in `settings.gradle.kts`. It is `compileOnly`,
so it is not bundled into the APK.
