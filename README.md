# LSPosed Universal Template

An Android 9+ module foundation for authorized Java/Kotlin and ARM/ARM64 native instrumentation.
The same modern libxposed API-102 module can run under Vector or inside an LSPatch target.
The template installs no game features, protection bypasses, automatic byte patches, or sample
Activity hooks. The example menu value has no effect until a consuming module implements one.

## Supported foundation

- Application/context bootstrap, exact package/process scope, and lifecycle-bound overlay.
- Persistent bool and numeric controls, explicit numeric steps, direct input, and live updates.
- ShadowHook shared-mode infrastructure and event-driven native-library discovery.
- Optional compiled IL2CPP helpers: opaque metadata, full method signatures, typed field APIs,
  owned thread attachments, GC roots, managed exception results, and calibrated native addresses.
- A single read-only Frida reconnaissance agent; optional loader observation is explicit.
- Verified release signing and an atomic, validated LSPatch/SAI bundle command.

**Compatibility is evidence-based.** Engine detection is diagnostic, not permission to call an
engine API. A protected runtime with replaced exports, a changed native ABI, or a stripped member
can require an explicit adapter. No template can infer every application's semantics.
See [IL2CPP contracts](docs/ENGINE_IL2CPP.md), [other engines](docs/ENGINE_NATIVE_WORKFLOWS.md),
and [validation](docs/VALIDATION.md) for the implemented boundaries and measured coverage.

## Toolchain, checked 2026-11-07

| Component | Pin |
| --- | --- |
| Android Gradle Plugin | 9.5.1 |
| Gradle | 9.9.0, distribution SHA-256 verified |
| JDK | 21 for builds and LSPatch; Java sources target 17 |
| compile/target SDK | 37; device minimum 28 |
| NDK | 30.0.16248370 (r30 LTS) |
| Android SDK CMake | 4.1.2 |
| AndroidX Annotation | 1.11.0, compileOnly |
| libxposed API | 102.0.0, compileOnly |
| ShadowHook | 2.0.1, Maven Prefab |
| Frameworks | Vector 2.2; LSPatch 1.2 build 487 |

Framework versions remain independent from the module's native dependency. Host Frida, server,
and bridges must be a tested matching set; see [the Frida workflow](docs/FRIDA_EMULATOR_QUICKSTART.md).
SDK CMake is intentionally pinned separately from a desktop CMake installation.

## Configure and build

```sh
python3 tools/configure-template.py \
  --package com.yourname.yourmodule --name "Your Module" \
  --target com.example.target --author "YourName" --native-lib module_runtime
./gradlew :app:assembleDebug
```

Edit `TemplateConfig.java` for target processes and optional IL2CPP initialization. The configure
command keeps Java packages, JNI registration, module metadata and native library names aligned.
Do not include a second XposedBridge/API-93 module flavor.

A production release requires the existing signing identity explicitly:

```sh
# Set these through your local secret manager or CI environment:
# TEMPLATE_KS_PATH       absolute existing keystore path
# TEMPLATE_KS_PASS       store password
# TEMPLATE_KEY_ALIAS     alias with a private key
# TEMPLATE_KEY_PASS      key password
# TEMPLATE_CERT_SHA256   expected public certificate SHA-256
./gradlew :app:assembleRelease
```

Missing or partial configuration fails; it never silently switches certificates. For local QA
only, explicitly select the existing Android test key:

```sh
./gradlew :app:assembleRelease -Ptemplate.localTestSigning=true
```

Do not mix that option with release-signing variables. Keep the same identity for updates.
No private keystore belongs in Git. For rootless delivery, use the
[verified bundle workflow](docs/LSPATCH_NONROOT.md).

## Adding a feature

Register application-specific controls before `Bootstrap.start`, or alongside the registrations
in `FeatureRegistry.initialize`. Late registration also retains values already loaded from disk.

```java
FeatureRegistry.register(FeatureRegistry.Feature.bool("feature", "Feature", false));
FeatureRegistry.register(FeatureRegistry.Feature.integer("amount", "Amount", 1, 1, 100));
FeatureRegistry.register(FeatureRegistry.Feature.number("ratio", "Ratio", 1f, 0f, 2f, 0.05f));
```

Bool controls and every numeric feature render automatically. Integer controls are limited to
the exact float integer range (±16,777,216). Numeric input is finite, bounded,
and normalized to its declared step. Five-argument `number` preserves continuous float values.
Settings use `.<module applicationId>.settings.json` inside the target's files directory, avoiding
collisions between modules. A fork upgrading from `.rt_state` should explicitly retain/migrate its
old filename if it needs existing preferences; unrelated modules' files are never imported.

Slider changes are live; persistence commits on drag completion or panel close. File replacement
is atomic and failed writes preserve the previous file and remain retryable. The in-app values
are target-process settings, not a companion-application preferences bridge.
Persistence assumes one writing process, as in the default main-process scope. A fork enabling
multiple writers needs a coordinator or process-specific filenames.

Every real hook must read its own live setting and the module enable flag. Registering a menu
control does not implement native behavior or an undo operation. The native startup control
controls initial setup; it does not unload a library or undo downstream hooks.

## Checks

```sh
python3 -m unittest discover -s tests -v
./gradlew :app:assembleDebug :app:assembleRelease :app:lintDebug :app:lintRelease \
  -Ptemplate.localTestSigning=true
```

[Tests](tests/README.md) compile production implementations and exercise negative contracts.
Tests, device fixtures, captures and instrumentation do not enter the module source sets.
Runtime compatibility still needs representative target/ABI tests. `autoHotReload=false` is
intentional: a consuming module must implement teardown before supporting code reload.

The default APK contains ARM32 and ARM64 libraries. A Java-only x86 distribution needs its native
build/dependency and ABI filters removed; disabling a runtime setting does not change APK ABIs.
