# Regression and device checks

The tests and device fixtures live outside the Android module source sets. Run the commands
below from the repository root.

## Host contracts

The Python host suite calls `javac --release 17`, `java`, `node` and a C++20 `clang++` with
AddressSanitizer and UndefinedBehaviorSanitizer. It uses this checkout by default. Set
`TEMPLATE_TEST_ROOT` to test a different configured copy. For the Android build and lint,
CI uses JDK 21 and the [SDK packages listed in the workflow](../.github/workflows/android.yml).
The Gradle command below selects local test signing, so leave production signing variables unset for it.

```sh
python3 -m unittest discover -s tests -v
./gradlew :app:assembleDebug :app:assembleRelease :app:lintDebug :app:lintRelease
```

## Owned native fixtures

With an explicitly selected device and the repository's NDK/ShadowHook dependencies installed:

```sh
python3 tests/device/audit_native.py --device DEVICE_SERIAL --delay 210 \
  --output artifacts/native-delay.json
python3 tests/device/audit_native.py --device DEVICE_SERIAL --il2cpp-observer \
  --output artifacts/observer.json
```

The first fixture checks delayed loading and restoration after unhooking. The second checks
observer readiness and retirement across restarts. The default ABI is `arm64-v8a`.
Use `--abi armeabi-v7a` on a device that runs 32-bit ARM binaries.

## Live IL2CPP compatibility

Start an owned/authorized test application with a matching Frida server, then run:

```sh
python3 tests/device/audit_il2cpp.py --device DEVICE_SERIAL --package com.example.target \
  --bridge /absolute/path/frida-il2cpp-bridge/dist/index.js \
  --output artifacts/il2cpp-runtime.json
```

The runner uses root to stage and remove its QA library in the target's code cache. It exercises
framework calls, fields, exceptions and address calibration. Use `--bind-only` to limit the check
to exported API binding. Keep the app running through detachment.

## Embedded APK integration

The fixture builder reads the [shared signing profile](../SIGNING.md) and creates a new output directory.

```sh
python3 tests/device/build_embedded_fixture.py --output artifacts/owned-qa
# Install every APK inside artifacts/owned-qa/fixture.apks together with SAI, or extract and use adb install-multiple.
python3 tests/device/check_embedded_fixture.py --device DEVICE_SERIAL \
  --fixture artifacts/owned-qa/fixture.json
```

The builder packages an owned host, a split and a configured module under a fresh identity.
The UI checker exercises controls, persistence, rotation, recreation and resume. Reading the
fixture settings file requires root. Uninstall the generated host and remove the output directory
when finished.
