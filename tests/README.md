# Regression and device checks

The tests and device fixtures live outside the Android module source sets. Run the commands
below from the repository root.

## Host contracts

Use Python 3.11+, JDK 21, Node.js and Clang++ with AddressSanitizer and UndefinedBehaviorSanitizer.
The unit suite runs entirely on the host. Set `TEMPLATE_TEST_ROOT` to a configured copy to test
renamed sources. For the Android build and lint checks, install the [SDK packages used in CI](../.github/workflows/android.yml)
and leave the five production signing variables unset.

```sh
python3 -m unittest discover -s tests -v
./gradlew :app:assembleDebug :app:assembleRelease :app:lintDebug :app:lintRelease \
  -Ptemplate.localTestSigning=true
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

Run the local QA build first so the standard Android debug keystore exists. Choose a new output
directory for the fixture.

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
