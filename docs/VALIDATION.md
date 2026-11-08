# Validation record — 2026-11-07

This record covers template 0.2.0. A passing fixture establishes its tested contracts, not universal
compatibility with every engine, private ABI, protected runtime or future Android release.

## Build and host checks

- AGP 9.5.1, Gradle 9.9.0 with distribution checksum, JDK 21, SDK 37, NDK r30 and SDK CMake 4.1.2.
- 64 host checks passed. Debug and optimized release builds passed for `arm64-v8a` and
  `armeabi-v7a`; both lint variants reported no issues. Configuration-cache reuse passed.
- Production C++ core under ASan/UBSan: late assemblies, nested/ambiguous identity, exact signatures,
  inheritance, abstract rejection, primitive/reference fields, readonly protection, initialization,
  managed exceptions, owned roots/threads, changed calibration layouts and expired lifetimes.
- Java contracts: strict/atomic persistence, exact float round trips, integer steps/range,
  independent feature registration, preview/commit, context readiness, process scope and diagnostic
  Java/native/Unity/JavaScript/React Native classification.
- Host ELF scanner/CLI, configuration rename, Frida API/listener ownership, bundle rejection and
  documentation examples/link tests.
- The JNI boundary preserves normal return values and contains C++ exceptions. All 16 core
  contracts also passed as an actual Android ARM64 executable.
- Signing rejects absent/partial settings, wrong certificate, wrong private-key password and
  mixed test/production mode; accepts an explicitly pinned matching identity.
- ZIP entries validate at 16 KiB alignment; module ARM64 ELF LOAD segments validate at >=16 KiB.
  ARM32 libraries validate at >=4 KiB. QA sources/probes are absent from APK source sets.

The toolchain still emits an upstream Gradle `Configuration.setVisible` deprecation. It is not a
module compile/lint failure; reassess the plugin before moving to Gradle 10.

## Android evidence

MuMu instance 0: Android API 32, ARM64-only userspace, 4 KiB pages. Frida client/Python and locally
patched undetected server: 17.25.0; bridge 0.14.0. The installed stack was preserved.

| Check | Observed result |
| --- | --- |
| Native library already/delayed loading | Immediate and 210-second delayed-load fixtures passed |
| Shared hook ownership | 100 proxy calls per native run, unhook restored original behavior |
| Idle delayed observer | About 0.19 ms process CPU during the 210-second wait; notification driven |
| Native library retirement | Observed and invalidated the fixture lifetime |
| IL2CPP observer | No ready runtime before an invocation; two explicit shutdown/restart lifetimes passed |
| ReLevel standard IL2CPP | Bound 49 public APIs; primitive/reference invoke, managed exception, readonly preservation, exact Int64 maximum and two calibrated slots passed |
| OnceWorld standard IL2CPP | The same framework-only checks passed |
| Embedded API-102 module | Owned Java host base + configuration split installed and cold-launched |
| Embedded native JNI | ShadowHook/helper status available; engine-neutral default installed no game hook |
| Menu/settings | Two numeric controls, integer normalization, bool state, restart, recreation, rotation, background/resume and reset passed |
| Bundle | Exact embedded module bytes, scope, split identities, signer, CRC and alignment verified |

The live C++ probe initially encountered instrumentation-process crashes before its native entry
point. Isolated checks narrowed this to the earlier custom-library loading/export-lookup path;
the final runner collects API addresses first and uses platform `dlopen`/`dlsym`. Both applications
passed the complete probe afterward. This does not establish a general fix for every Frida fault.

## Reference calibration and limits

Read-only reference review used Honkai (custom/protected runtime boundary), Candy Crush (native
resolution evidence and separate ranges), OnceWorld (full metadata signatures), ReLevel
(independent delegate calibration), and Another Eden's `ae-pcd-stamp-tracer` (native/Cocos/Lua
startup delayed beyond three minutes). No app-specific feature, RVA, protection decoder or private
layout was copied into the production template. Reference repositories were not edited.

ARM32 is build/packaging coverage here, not a device runtime pass: this MuMu image rejects 32-bit
executables. A 16 KiB Android runtime was not boot-tested; ELF/ZIP alignment was checked statically.
The embedded LSPatch path was exercised; separate Vector manager activation/hot reload was not.
Hot reload remains disabled until a consumer implements teardown.

Godot, Mono, Lua, Unreal, Flutter and JavaScript guidance specifies integration contracts; it does
not claim tested native adapters. Unsupported layouts/missing exports require a reviewed adapter.
No test establishes server-side feature behavior, arbitrary game semantics or protection bypasses.

Reproduction commands and fixture boundaries are in [tests/README.md](../tests/README.md).
Generated APKs, captures and raw logs stay in ignored `artifacts/`, outside the distributable source.
