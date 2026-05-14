# Rogue Recovery

[![License: CC BY-NC-ND 4.0](https://img.shields.io/badge/License-CC%20BY--NC--ND%204.0-lightgrey.svg)](LICENSE)

A quick-start Android/LSPosed module template for authorized testing and rapid prototyping.

What it includes:

- Modern `libxposed` API 101 entry point (`io.github.libxposed:api:101.0.1`, `compileOnly`).
- Both `onPackageLoaded` and `onPackageReady` callbacks are overridden for broad compatibility.
- Process-level filter: `TemplateConfig.TARGET_PROCESS_SUFFIXES` + `SKIP_PROCESS_SUFFIXES`
  default to hooking only the main process and skipping common anti-cheat / push /
  crash-handler satellite processes.
- Modern LSPosed metadata under `META-INF/xposed/`:
  - `java_init.list`
  - `scope.list`
  - `module.prop` with the correct `exceptionMode=protective` key
- A safe Java hook smoke test using the API 101 interceptor-chain style.
- `FeatureRegistry` — runtime feature flags (bool/float) with best-effort persistence and
  live-updating overlay toggles bound to each feature key.
- `EngineDetector` — identifies Unity / Unreal / Cocos2d-x / Godot / Flutter /
  React-Native / Xamarin targets at startup so you can branch hook strategies.
- `NativeUtils` — JNI helpers for `/proc/self/maps` module lookup, IDA-style pattern
  scan, `dlsym` resolution, and safe `mprotect`-guarded read/write-memory primitives.
- A movable dark/lavender Nyx-styled floating menu:
  - movable oval bubble
  - movable rectangular panel when opened (drag the header)
  - one toggle row per `FeatureRegistry` bool feature
- Optional native scaffold using ByteDance ShadowHook (`com.bytedance.android:shadowhook:2.0.0`)
  registered via `JNI_OnLoad` / `RegisterNatives` so there are no package-derived JNI export
  names in the `.so` symbol table.
- Debug/release split with `VERBOSE_LOGS` as a `BuildConfig` field — release builds strip
  verbose logs, rename no class/method in the `ModuleEntry` path (required for LSPosed
  discovery) but obfuscate everything else via R8.
- Neutral log tag (`AppRuntime`) and worker thread name; release builds disable verbose Java and
  native logging by default.
- Release signing falls back to the debug keystore when no env keystore is configured
  (`TEMPLATE_KS_PATH`, `TEMPLATE_KS_PASS`, `TEMPLATE_KEY_ALIAS`, `TEMPLATE_KEY_PASS`).
- Configure script supports `--native-lib` to rename the packaged `.so` away from the
  obvious `librogue_recovery.so`.
- Frida-first Android emulator workflow documentation for reconnaissance before building
  permanent hooks.
- Engine-specific native workflow notes for Unity IL2CPP and other native-heavy targets,
  kept as optional documentation so the template remains engine-neutral.

Use this only on apps/systems you own or are authorized to test.

## Current researched versions

- `libxposed` API latest on Maven Central: `101.0.1`.
- LSPosed/Vector modern API level: `101`.
- ByteDance Android inline hook library (`shadowhook`) latest Maven release: `2.0.0`.
- ShadowHook supports `armeabi-v7a` and `arm64-v8a`; it does **not** support `x86`/`x86_64` or Houdini-translated environments.

## Quick start

From this directory:

```bash
./tools/configure-template.py \
  --package com.yourname.yourmodule \
  --name "Your Module" \
  --target com.example.target \
  --author "Jordan" \
  --native-lib audio_util      # optional; renames librogue_recovery.so
```

Then build:

```bash
./gradlew :app:assembleRelease
```

Install:

```bash
adb install -r app/build/outputs/apk/release/app-release.apk
```

Enable the module in LSPosed/Vector, select the target scope, then force-stop and reopen the target app:

```bash
adb shell am force-stop com.example.target
adb shell monkey -p com.example.target 1
adb logcat -c
adb logcat -s AppRuntime shadowhook_tag
```

Release builds silence these logs by default (`BuildConfig.VERBOSE_LOGS=false`).
Use `./gradlew :app:installDebug` while developing to get the chatty tag.

## Files you normally edit first

- `app/src/main/java/com/jordan/rogue/recovery/TemplateConfig.java`
- `app/src/main/resources/META-INF/xposed/scope.list`
- `app/src/main/resources/META-INF/xposed/module.prop`
- `app/src/main/res/values/arrays.xml`
- `app/src/main/res/values/strings.xml`

## Modern libxposed API shape

The entry class extends `io.github.libxposed.api.XposedModule` and is listed in:

```text
app/src/main/resources/META-INF/xposed/java_init.list
```

Hooking is interceptor-chain based:

```java
hook(method)
    .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
    .intercept(chain -> {
        Object result = chain.proceed();
        // post-call logic
        return result;
    });
```

Important points:

- Keep `libxposed` as `compileOnly`; never package the framework API into the APK.
- Keep scope tight. Do not hook every app unless you are building a framework/system module and know why.
- Use `PROTECTIVE` exception mode for release builds so hook failures do not crash the target app.
- Use `deoptimize(executable)` only when you actually need it; overusing deopt can hurt performance.

## ShadowHook notes

ShadowHook is the recommended native inline-hook scaffold in this template because it is actively maintained and supports:

- hook by function address or `library + symbol`
- pending hooks for ELFs loaded later
- hook modes per proxy function (`shared`, `multi`, `unique`)
- instruction-level intercepts
- linker init/fini callbacks
- operation records for debugging

The template’s native code installs a harmless `libc.so!getpid` smoke-test hook and returns the original value unchanged. Replace that with your app-specific native hook only after finding stable symbols/addresses with Frida.

See `docs/SHADOWHOOK_NOTES.md`.

## Engine-specific native workflows

The template is deliberately not IL2CPP-specific. `EngineDetector` is only a routing helper:
use it to decide which research notes or hook installers are relevant, but keep target-specific
offsets, metadata dumps, and generated analysis files out of `main`.

For Unity IL2CPP targets, see `docs/ENGINE_NATIVE_WORKFLOWS.md`. That document covers static
metadata recovery, RVA-to-runtime-address mapping, value-type ABI checks, delayed library loading,
and settings-bridge issues that also apply to other native-heavy engines. Treat it as a playbook
for a branch that targets one app, not as default template behavior.

## Frida-first workflow

Use Frida first to answer questions like:

- What process and ABI am I actually in?
- Which classes/methods are loaded?
- Which native libraries load and when?
- Which exported symbols exist?
- Which Java methods or native symbols are stable enough to become permanent LSPosed/ShadowHook hooks?

Use the latest undetected Frida server linked in `docs/FRIDA_EMULATOR_QUICKSTART.md` for this workflow. Keep the host `frida-tools` version and device `frida-server` version aligned, and do not mix these steps with stock `frida-server` unless you are intentionally debugging a version/build mismatch.

See `docs/FRIDA_EMULATOR_QUICKSTART.md`.

## Moving the menu panel

Tap the `Nyx` bubble to open the rectangular display. Drag the title/subtitle header area to move the open panel. The bubble itself is also draggable.

## Architecture warning

If you use an x86/x86_64 emulator, Java LSPosed hooks can still work, but the native ShadowHook scaffold will not. For native inline-hook testing, use a real arm64 device or an arm64 emulator image on Apple Silicon.

## Repository files

- `LICENSE` — CC BY-NC-ND 4.0 license notice and official license links.
- `SECURITY.md` — safe issue-reporting expectations.
- `CONTRIBUTING.md` — contribution and validation expectations.
- `docs/ENGINE_NATIVE_WORKFLOWS.md` — optional notes for Unity IL2CPP and native-heavy targets.
- `.github/workflows/android.yml` — GitHub Actions build for debug and release APKs.

## License

This project is licensed under Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International (`CC-BY-NC-ND-4.0`). You may share the unmodified template with attribution for non-commercial use. Do not distribute modified versions without separate permission.
