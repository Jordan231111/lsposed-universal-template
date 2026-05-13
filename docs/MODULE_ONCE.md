# Module: OnceWorld (`com.devin.lsposed.once`)

This module targets **OnceWorld** (`work.ponix.onceworld`, Ponix, v2.2.5). Java-only LSPosed
module; the produced APK is **ABI-neutral** and works on `arm64-v8a` devices and on `x86_64`
Android emulators.

## Target App Analysis

| Aspect | Finding |
| --- | --- |
| Engine | **Unity IL2CPP** (`libil2cpp.so`, ~95 MB) |
| Native libs shipped | `arm64-v8a/` only |
| Java entry | `com.unity3d.player.UnityPlayerGameActivity` (stock Unity activity) |
| Application | Wrapped by `com.pairip.application.Application` (PAIRIP shim) |
| Java obfuscation | R8 / proguard - heavy single-letter renames |
| Anti-tamper | **PAIRIP** (Google Play Integrity bytecode VM, `libpairipcore.so`) |
| Anti-cheat | **CodeStage Anti-Cheat Toolkit (ACTk)** - `net.codestage.actk.androidnative.ACTkAndroidRoutines` present in metadata |
| `global-metadata.dat` | Unencrypted (`0xFAB11BAF` magic) - Il2CppInspectorRedux / Il2CppDumper friendly for future native hooks |
| Server validation | Light - cosmetics / leaderboard / shop are server-side; combat, idle income, and timer-based mechanics run client-side. |

### Why we chose Java-only hooks for this app

OnceWorld ships only an `arm64-v8a` native split, so a native-hook module would only help on
arm64 hardware. By staying in Java we cover **both** physical devices and Intel emulators in a
single APK. Java hooks also survive R8 reshuffles, because the framework-level methods we hook
(`System.currentTimeMillis`, `Activity.onPause`, ...) are not renamed.

## Hooking Strategy

Identical strategy as the Rogue module - kept in lockstep so the two modules share a single
hook surface that we know is stable across the same anti-cheat stack. All hooks install at
`onPackageLoaded` and are gated by FeatureRegistry; each check happens per-invocation so the
overlay can flip behavior live.

| Hook | Method(s) | Why |
| --- | --- | --- |
| **Time multiplier** | `System.currentTimeMillis`, `SystemClock.uptimeMillis`, `SystemClock.elapsedRealtime`, `SystemClock.elapsedRealtimeNanos` | OnceWorld is an incremental idle game; every gameplay loop is gated by the Java clock. Anchor-based warp keeps the clock monotonic across multiplier changes. |
| **ACTk Java bridge** | `net.codestage.actk.androidnative.ACTkAndroidRoutines.GetSystemCurrentTimeMs / GetSystemNanoTime / GetSystemNanoTimeMs` | Defeats ACTk's tamper-resistant time queries and ensures the in-engine `Time.realtimeSinceStartup` (which ACTk routes through this bridge) speeds up too. |
| **PAIRIP signature check** | `com.pairip.SignatureCheck.verifyIntegrity(Context)` | Same as Rogue. |
| **PAIRIP license callbacks** | `com.pairip.licensecheck.LicenseClient.processResponse / dontAllow / applicationError / onError` | Same as Rogue. |
| **Anti-idle: keep-screen-on** | `Activity.onResume` | OnceWorld runs Unity's player loop on a UI thread that yields when the screen turns off; we keep it on. |
| **Anti-idle: wake-lock on pause** | `Activity.onPause` | Hold a partial wake-lock for ~60s after pause to keep the scheduler running long enough for offline-tick simulations. |
| **Telemetry suppression** *(off by default)* | `FirebaseAnalytics.logEvent`, `FirebaseCrashlytics.recordException / log / setCustomKey` | Same as Rogue. |
| **Activity.onResume sample log** | `Activity.onResume` | Dev aid. |

## Feature Toggles

| Key | Default | Description |
| --- | --- | --- |
| `enabled` | true | Master enable. |
| `time_warp` | true | Apply multiplier to time queries. |
| `actk_bypass` | true | Hook ACTk Java bridge. |
| `pairip_bypass` | true | No-op PAIRIP signature & license. |
| `anti_idle` | true | Keep-screen-on + wake-lock. |
| `disable_telemetry` | false | Toggle on for Firebase/Crashlytics silence. |
| `multiplier` | 4.0 | Float, clamped 1-30. |

## Known Limitations

1. **No direct inventory edits**: gold / gems / inventory live in IL2CPP. We deliberately avoid
   reading or writing them from Java. Time acceleration is the stable shortcut.
2. **No OHK / godmode**: hit resolution is in `libil2cpp.so` and would require unstable native
   hooks. Use the time multiplier to compress combat.
3. **Server-authoritative shop / banners**: server enforces purchase validity. The module's
   time-warp does not affect the server's clock.
4. **PAIRIP native re-validation**: the Java no-op covers entry points; a future native-only
   integrity check would not be intercepted but, thanks to `PROTECTIVE` exception mode, would
   degrade gracefully without crashing the host app.
5. **Unity activity selection**: OnceWorld uses the stock `UnityPlayerGameActivity`. The
   overlay attaches at the first resumed Activity of the target package, which on emulators
   is reliably the Unity activity.

## Emulator Compatibility

| Environment | Status |
| --- | --- |
| arm64-v8a physical device, LSPosed 1.9+ | Supported. |
| `x86_64 + arm64 translator` emulator | Module loads cleanly; Java hooks fire. The game itself runs under ARM translation. |
| Pure x86_64 (no ARM translator) | Game cannot run (no x86_64 split). Module loads but has nothing to hook. |
| Magisk 28 + LSPosed/Zygisk | Confirmed: scope persists, overlay attaches. |

## Build

```
./gradlew :modules:once:assembleDebug
./gradlew :modules:once:assembleRelease
```

Output: `modules/once/build/outputs/apk/{debug,release}/once-{debug,release}.apk`.
