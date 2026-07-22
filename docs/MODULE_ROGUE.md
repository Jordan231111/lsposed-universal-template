# Module: Rogue with the Dead (`com.devin.lsposed.rogue`)

This module targets **Rogue with the Dead** (`net.room6.horizon`, room6.inc, v3.11.1).
It is a Java-only LSPosed module: the produced APK contains **no native code**, so it loads
unchanged on `arm64-v8a` devices and on `x86_64` Android emulators.

## Target App Analysis

| Aspect | Finding |
| --- | --- |
| Engine | **Unity IL2CPP** (`libil2cpp.so`, ~96 MB) |
| Native libs shipped | `arm64-v8a/` only - app does **not** ship a x86_64 split |
| Java entry | `net.room6.horizon.MyActivity` (subclass of `UnityPlayerActivity`) |
| Application | Wrapped by `com.pairip.application.Application` (PAIRIP shim) |
| Java obfuscation | R8 / proguard - most app classes use single-letter package and class names |
| Anti-tamper | **PAIRIP** (Google Play Integrity bytecode VM, `libpairipcore.so`) |
| Anti-cheat | **CodeStage Anti-Cheat Toolkit (ACTk)** - present in IL2CPP metadata strings (`net.codestage.actk.androidnative.ACTkAndroidRoutines`) |
| Server validation | Light - cosmetics/progression sync via REST; no per-tick authoritative simulation. Most gameplay loops run client-side. |
| Emulator behavior | Without x86_64 native split, `libil2cpp.so` is loaded via the OS's binary translation on Intel emulators (Android Studio's `Android 13/14 x86_64 + arm64 translator`). Native hooks are therefore impractical; **Java hooks remain stable**. |

### Why the Android-only split blocks ARM-only native hooking

Because the APK only ships `arm64-v8a`, ShadowHook (which only supports `arm/arm64`) is a fine
fit for **physical arm64 devices** but is **useless on Intel x86_64 emulators** even when those
emulators can translate the game's ARM code. Touching native instructions inside a translated
process is unsupported. Hence we deliberately keep this module Java-only - it works in both
worlds.

## Hooking Strategy

All hooks are installed at `onPackageLoaded` (i.e. before `Application.attach`, so they are
ready before the target's own static initializers run) and gated by the in-process
`FeatureRegistry`. The user can flip any feature live from the overlay; no restart required.

| Hook | Method(s) | Why |
| --- | --- | --- |
| **Time multiplier** | `System.currentTimeMillis`, `SystemClock.uptimeMillis`, `SystemClock.elapsedRealtime`, `SystemClock.elapsedRealtimeNanos` | Idle/incremental gameplay polls the system clock from Java for timed rewards, breath gates, and offline catch-up. Multiplying the reported elapsed time accelerates cooldowns, regen, energy refills, and any "X / hour" mechanic. We use an **anchor-based warp** (`anchor + (now - anchor) * multiplier`) so the reported clock never jumps backwards when the multiplier changes - if we just multiplied raw `now` values, switching from x4 to x1 would briefly look like the clock went **back in time**, which both ACTk and naive game code interpret as a tamper signal. |
| **ACTk Java bridge** | `net.codestage.actk.androidnative.ACTkAndroidRoutines.GetSystemCurrentTimeMs / GetSystemNanoTime / GetSystemNanoTimeMs` | ACTk routes time queries through these Java methods rather than directly calling libc to defeat naive `clock_gettime` hooks. Hooking them defeats ACTk's time-tamper detection **and** speeds the game up via the same constant. |
| **PAIRIP signature check** | `com.pairip.SignatureCheck.verifyIntegrity(Context)` | PAIRIP compares the SHA-256 of the running APK's signing cert against a hardcoded constant. Even though LSPosed does not repack the target, PAIRIP can additionally invoke `libpairipcore.so` to compute its own integrity assertions; no-opping the check is a defensive baseline. |
| **PAIRIP license callbacks** | `com.pairip.licensecheck.LicenseClient.processResponse / dontAllow / applicationError / onError` | These methods gate startup on a Google Play licensing response; on environments without Play (some emulators, sideloaded installs) they can short-circuit the app. We no-op them all (returns null). |
| **Anti-idle: keep-screen-on** | `Activity.onResume` | Adds `WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON` so the device doesn't dim/sleep mid-session. |
| **Anti-idle: wake-lock on pause** | `Activity.onPause` | Acquires a 60-second partial wake-lock as the Activity backgrounds, so the Unity render thread keeps scheduling and idle accumulators tick offline. We **do not** skip `super.onPause()` because doing so would break lifecycle invariants. |
| **Telemetry suppression** *(off by default)* | `FirebaseAnalytics.logEvent`, `FirebaseCrashlytics.recordException / log / setCustomKey` | Optional: silences analytics that may also feed anti-cheat heuristics. Off by default because suppressing **all** analytics can be more noisy than the events themselves. |
| **Activity.onResume sample log** | `Activity.onResume` | Useful while developing more hooks. Toggleable from the overlay. |

### Why these hooks survive minor app updates

- We resolve every target class via `Class.forName(name, false, classLoader)` and skip-on-missing.
  If the target swaps the SignatureCheck implementation or removes ACTk, the relevant hook
  silently no-ops instead of crashing.
- We **never** rely on a hardcoded smali line, an obfuscated symbol, or an IL2CPP offset.
- All hooks are `PROTECTIVE` (`XposedInterface.ExceptionMode.PROTECTIVE`): if the interceptor
  body itself throws, LSPosed lets the original call run, so a single bad cast can never wedge
  the target app.

### Why the time-multiplier choice (default x4)

A default of `x4` is aggressive enough to be visible on minute-scale idle gameplay but small
enough that the **per-frame** delta is still plausible: ACTk's tamper alarm fires on a
**discontinuity** in the wall clock relative to monotonic time, not on a steady offset. By
warping both wall and monotonic time from the same anchor, the ratio stays constant and the
detector sees no jump.

## Feature Toggles (overlay)

| Key | Default | Description |
| --- | --- | --- |
| `enabled` | true | Master enable (off = passthrough). |
| `time_warp` | true | Apply the multiplier to time queries. |
| `actk_bypass` | true | Hook ACTk Java bridge methods. |
| `pairip_bypass` | true | No-op PAIRIP signature & license callbacks. |
| `anti_idle` | true | Keep-screen-on + wake-lock. |
| `disable_telemetry` | **false** | Off; toggle on if you want analytics silenced. |
| `multiplier` | 4.0 | Float, clamped 1-30. |

## Known Limitations

1. **Inventory / currency edits live in IL2CPP** - we do not write to Unity ints from Java.
   The IL2CPP methods that mutate gold/diamonds/energy live behind `libil2cpp.so` with
   obfuscated thunks; without dumping `global-metadata.dat` and resolving a method-pointer table
   on every game update, any direct write would break the next time the binary is recompiled.
   Time-acceleration is the **stable** alternative: the game itself reads the cooldowns and
   refills as fast as we ask.
2. **One-hit-kill / godmode** are not exposed: the relevant hit-resolution functions are in
   IL2CPP. Implementing them safely would require either Frida-style native hooks (broken on
   x86_64 emulators that run the ARM `libil2cpp.so` under translation) or runtime IL2CPP
   method-pointer scanning - neither survives a minor app update reliably. Use the time
   multiplier to compress encounter cooldowns instead.
3. **Server-authoritative drops** (limited-time gacha banners, leaderboard rewards) are still
   server-validated. Time-warp only affects the **client clock**; the server clock is not
   touched.
4. **PAIRIP native VM** can periodically re-validate via `libpairipcore.so` during gameplay.
   Our Java no-ops cover the Java entry points; if a future version embeds a native check that
   panics regardless, that would manifest as a startup-time crash. PROTECTIVE mode means the
   module itself never crashes the app - any failure degrades to "feature disabled".
5. **R8 obfuscation churn**: room6 ships builds where the *Application* class is consistently
   `com.pairip.application.Application`, but other classes are renamed every release. We
   intentionally do **not** hook any minified class to stay update-resilient.

## Emulator Compatibility

| Environment | Status |
| --- | --- |
| arm64-v8a physical device, LSPosed 1.9+ | Supported - all hooks installable. |
| Android Studio `x86_64 + arm64 translator` (API 30+) | Module loads cleanly; Java hooks fire. The game itself still needs the translator (room6 doesn't ship x86_64), but the **module** is ABI-neutral. |
| Pure x86_64 emulator (no ARM translation) | Module loads but Rogue can't run because room6's `libil2cpp.so` is arm64-only. Not a module limitation. |
| LSPosed/Zygisk on Magisk 28+ | Confirmed via blueprint - module is detected, scope is enforced, overlay attaches to the target Activity. |

## Build

```
./gradlew :modules:rogue:assembleDebug      # debug APK with verbose logging
./gradlew :modules:rogue:assembleRelease    # R8-minified release APK
```

Output: `modules/rogue/build/outputs/apk/{debug,release}/rogue-{debug,release}.apk`.
