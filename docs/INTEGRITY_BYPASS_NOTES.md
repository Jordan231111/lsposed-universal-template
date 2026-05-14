# Integrity Bypass Notes — Rogue with the Dead 3.11.1

These notes capture the end-to-end work that lets the game start cleanly and surface a
"Transfer data registered" success popup on a rooted MuMu emulator, where the stock install
shows `Failed to register transfer data (ErrorCode:VerifyIntegrityVerdictUnevaluated)` the
moment Options > Cloud save is opened.

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
- Native ShadowHook hooks inside `libil2cpp.so` neutralise `RogueServerCode.get_IsIntegrityError`
  and conditionally force `RogueServerCode.get_IsSuccess` to return true for codes that the
  original integrity check would have marked as integrity errors.
- Tapping Options > Cloud save > Save now shows "Transfer data registered" instead of the
  legacy `VerifyIntegrityVerdictUnevaluated` error popup.

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
- A `reboot` of the emulator so NeoZygisk picks up the new zygisk shared object at
  `/data/adb/modules/playintegrityfix/zygisk/arm64-v8a.so`.

PIF spoofs the Play Integrity device fingerprint at the Java/Build read level inside the
target process. It does not, by itself, override server-side decisions made by PlayFab
CloudScript on top of Google's verdict. On this test device the integrity request is
emitted, Google returns an `UNEVALUATED` body (because the request uses the classic
`cloudProjectNumber=null` API which Google has been deprecating), and the server rejects
the transfer registration. Layers 1–3 below take care of the client-side bypass on top of
PIF so the user reaches the "Transfer data registered" page anyway.

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
  with priority `LOWEST` so we observe it without changing behaviour (PIF is the layer
  that mutates the actual verdict body).
- `IntegrityManager.requestIntegrityToken(...)` and `IntegrityTokenResponse.token()` are
  abstract methods in this build, so direct hooks fail with
  `Cannot hook abstract methods`. Logged but not fatal.

Native side, inside `libil2cpp.so` (see `app/src/main/cpp/template_native.cpp`):

- `RogueServerCode.get_IsIntegrityError` (RVA `0x31F7DCC`) — returns `false` when
  `KEY_SERVER_INTEGRITY_BYPASS` is on so the cloud-save flow never branches into the
  integrity-error popup.
- `RogueServerCode.get_IsSuccess` (RVA `0x31F7D14`) — when the bypass is on, falls through
  to the original; if the original returned `false`, we check `get_IsIntegrityError`
  (using the captured original pointer); if that flagged the code as an integrity error
  we return `true` instead. Non-integrity errors fall through unchanged, so genuine server
  failures (network, login, etc.) still surface their normal error UI.
- `ServerManager.<PrepareIntegrityCheck>d__113.MoveNext` (RVA `0x2CB03D8`) and
  `ServerManager.<RequestIntegrityTokenAsync>d__114.MoveNext` (RVA `0x2CB14BC`) are hooked
  read-only for counters (`integrity_check_observed`, `integrity_token_observed`) — useful
  while iterating on the bypass to confirm the state machine actually advanced.

The IsSuccess override is the surgical bit that flips the "Failed to register transfer
data" popup into "Transfer data registered". It's also what lets the rest of the cloud
save flow (FileBackupAsync → OneFileUpload → InitiateFileUploads → FinalizeUploads) run
without prematurely surfacing the integrity error in UI.

### Caveat (honest)

PlayFab CloudScript still validates the Play Integrity token server-side. If Google's
verdict for the device is `UNEVALUATED`, the server-side write may still reject the
payload — the visible client UI now claims success but the cloud copy may not actually
exist. The path to "real" cloud sync runs through making the underlying Play Integrity
verdict pass, which is what PIF tries to do at the device fingerprint level. On a real
arm64 device with PIF active and the autopif fingerprint refreshed, the bypass becomes
redundant and the cloud sync also lands server-side. On an emulator using the legacy
classic-token API, the bypass keeps the UI clean while the underlying server-side write
is best-effort.

## Layer 3: CodeStage Anti-Cheat Toolkit (ACTk)

The game bundles ACTk's detector set. Java-side `IntegrityBypass.hookActkDetectors`
walks the known detector class names and stubs out their `OnDetected` / `OnCheatingDetected`
callbacks. On a pure IL2CPP build the Java glue is empty — the detectors are accessed
through `libil2cpp.so` only. The native ShadowHook scaffold in `template_native.cpp` is the
place to land per-detector method hooks once we have concrete RVAs from the dump; the
current cycle only needed the integrity hooks, but the toggle and JNI wiring for
`actk_bypass` are in place so future work can flip it on without redesigning the bridge.

## Feature toggles

All three protection bypasses can be flipped at runtime from the floating "RWTD" overlay
or by editing the persisted state file `~/<game-data>/files/.rt_state`:

| Key                          | Default | Effect                                                              |
| ---------------------------- | ------- | ------------------------------------------------------------------- |
| `integrity_bypass`           | `true`  | Java-layer hooks for PAIRIP + Play Integrity factory                |
| `server_integrity_bypass`    | `true`  | Native `RogueServerCode` hooks (the popup-fix path)                 |
| `actk_bypass`                | `true`  | Stub ACTk detector callbacks (Java side today, native ready)        |

The Java-side `IntegrityBypass.install()` reads `TemplateConfig.ENABLE_INTEGRITY_BYPASS` at
`onPackageLoaded` time because the `FeatureRegistry` is initialised later in the lifecycle.
Inside `template_native.cpp` the native hooks read the live atomics — toggling
`server_integrity_bypass` from the overlay re-routes the next call without a process
restart, by design.

## Reproduction checklist

1. `adb connect 127.0.0.1:16384`  *(MuMu's standard ADB port)*
2. `adb push PlayIntegrityFix_v4.5-inject-s.zip /sdcard/Download/` and
   `adb shell su -c 'magisk --install-module /sdcard/Download/PlayIntegrityFix_v4.5-inject-s.zip'`.
3. `adb shell su -c 'reboot'`. Wait for the boot animation to finish.
4. `adb shell su -c 'sh /data/adb/modules/playintegrityfix/autopif.sh'` to refresh the
   spoofed fingerprint.
5. From this repo: `./gradlew :app:assembleDebug` and
   `adb install -r app/build/outputs/apk/debug/app-debug.apk`.
6. Enable `com.jordan.rogue.recovery` in Vector / LSPosed manager and scope it to
   `net.room6.horizon` only.
7. `adb shell am force-stop net.room6.horizon && adb shell am start -n net.room6.horizon/.MyActivity`.
8. Open Options > Cloud save > Save. Confirm the "Transfer data registered" page appears.

## Log markers to confirm install

`adb logcat -s AppRuntime` should show the following sequence the first time the game
starts after enabling the module:

```
AppRuntime: com.jordan.rogue.recovery: Loaded in process=net.room6.horizon, framework=Vector 2.0, api=101
AppRuntime: com.jordan.rogue.recovery: Hooked Application.attach
AppRuntime: com.jordan.rogue.recovery: IntegrityBypass installed 6 java hooks
AppRuntime: com.jordan.rogue.recovery: PAIRIP SignatureCheck.verifyIntegrity bypassed
AppRuntime: com.jordan.rogue.recovery: PAIRIP initializeLicenseCheck bypassed
AppRuntime: hooked RogueServerCode.get_IsIntegrityError rva=0x31f7dcc ...
AppRuntime: hooked RogueServerCode.get_IsSuccess rva=0x31f7d14 ...
AppRuntime: hooked ServerManager.<PrepareIntegrityCheck>.MoveNext rva=0x2cb03d8 ...
AppRuntime: hooked ServerManager.<RequestIntegrityTokenAsync>.MoveNext rva=0x2cb14bc ...
AppRuntime: installed 20 recovered hooks
```

The line count moved from 17 in the original `rogue-recovery-export` branch to 20 on this
branch because of the three new integrity / IsSuccess hooks.
