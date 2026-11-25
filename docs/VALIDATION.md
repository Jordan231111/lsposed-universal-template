# Verification procedures

Run the host suite and build both variants:

```sh
python3 -m unittest discover -s tests -v
./gradlew :app:assembleDebug :app:assembleRelease :app:lintDebug :app:lintRelease
```

Host checks exercise persistence, feature boundaries, lifecycle ownership, opaque IL2CPP metadata,
managed errors, native lifetime invalidation, ELF scanning, packaging rejection and documentation.
ASan/UBSan checks compile the production native contracts. Synthetic results establish those
contracts; target runtime behavior needs device validation.

Use the device fixtures in [tests/README.md](../tests/README.md) for library loading, observer
readiness, retirement, JNI and embedded UI transitions. Test enabled, disabled, neutral and boundary
values; recreation, rotation, pause/resume and process restart; unsupported runtime contracts and
failed writes. Remove generated packages and trial output afterward.

Check APK signing, embedded bytes, split identities and CRC. Check ZIP entry offsets and ELF
PT_LOAD alignment independently. A 4 KiB device cannot establish 16 KiB runtime compatibility.
ARM32 packaging requires a separate executable ARM32 device test. Native hot reload requires
consumer teardown and remains disabled by default.

Measure startup and idle CPU, frame-thread work, allocations, IPC, disk operations and discovery
activity with a representative target before optimizing. Account chooser UI, synthetic credential
checks and authenticated backend acceptance must be recorded as distinct results.

The template provides no application-specific gameplay or server-attestation implementation.
Missing exports, ambiguous metadata and incompatible layouts require a reviewed adapter.
