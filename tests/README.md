# Regression and device checks

Tests, doubles, QA libraries and captures are outside the Android source sets. Nothing in this
directory is linked into the release module. No real game features are installed by these tests.

## Host contracts

Run `python3 -m unittest discover -s tests -v` from the repository root. Requirements: Python 3.11+,
JDK 21, Node.js and Clang++ with AddressSanitizer/UndefinedBehaviorSanitizer. No emulator or Frida
server is needed. Set `TEMPLATE_TEST_ROOT` to a configured copy to exercise its renamed sources.

The tests compile production Java implementations against small framework doubles and the actual
C++ IL2CPP core against an opaque synthetic runtime. Scanner function bodies are extracted
verbatim for portable sanitizer checks. Other checks exercise public configure/ELF/packaging
commands, Frida agent ownership and current APIs, and compiled documentation examples/local links.
These contracts complement real Android tests; doubles do not establish device compatibility.

Build both native ABIs and run both lint variants:

```sh
./gradlew :app:assembleDebug :app:assembleRelease :app:lintDebug :app:lintRelease \
  -Ptemplate.localTestSigning=true
```

Local test signing must not be mixed with production signing environment variables.

## Owned native fixtures

With an explicitly selected device and the repository's NDK/ShadowHook dependencies installed:

```sh
python3 tests/device/audit_native.py --device DEVICE_SERIAL --delay 210 \
  --output artifacts/native-delay.json
python3 tests/device/audit_native.py --device DEVICE_SERIAL --il2cpp-observer \
  --output artifacts/observer.json
```

The first executable loads an owned library, resolves exports, performs 100 shared proxy calls,
unhooks and verifies restoration/retirement. The second exports an opaque synthetic IL2CPP API
and tests the production observer's readiness gate and two shutdown/restart lifetimes. Both use
unique device directories and remove them afterward. `--abi armeabi-v7a` requires a device that
can execute 32-bit ARM binaries; an ARM64-only emulator cannot establish that coverage.

## Live IL2CPP compatibility

Start an owned/authorized test application with a matching Frida server, then run:

```sh
python3 tests/device/audit_il2cpp.py --device DEVICE_SERIAL --package com.example.target \
  --bridge /absolute/path/frida-il2cpp-bridge/dist/index.js \
  --output artifacts/il2cpp-runtime.json
```

The external C++ library compiles the production core unchanged. It tests framework-only primitive
and reference invocations, exact Int64 handling, caught managed exceptions, readonly preservation
and delegate-based address calibration. It installs no gameplay hooks and launches/stops no app.
`--bind-only` checks exported API binding. The runner needs root to stage/remove its uniquely named
library in the target's code cache. It unloads the QA library before detaching. Keep the target's
lifetime stable during instrumentation; a debugger is not a crash isolation boundary.

## Embedded APK integration

```sh
python3 tests/device/build_embedded_fixture.py --output artifacts/owned-qa
# Install every APK inside artifacts/owned-qa/fixture.apks together with SAI, or extract and use adb install-multiple.
python3 tests/device/check_embedded_fixture.py --device DEVICE_SERIAL \
  --fixture artifacts/owned-qa/fixture.json
```

The builder creates a random package identity, an owned Java host plus a configuration split, and
an isolated configured module copy with an additional integer control. It exercises the same
verified packager used for delivery. The UI checker accepts only that generated package format;
it changes only fixture settings. It checks exact entry, independent controls, persistence,
Activity recreation, both orientations and background/resume. Root is used only to read the
fixture's settings file; the embedded module loads through LSPatch without a root injector.

Uninstall the generated host when finished and remove its ignored build directory. Never install
an unconfigured template over an unrelated module that happens to use the same package name.
See [the measured validation record](../docs/VALIDATION.md) for coverage and limitations.
