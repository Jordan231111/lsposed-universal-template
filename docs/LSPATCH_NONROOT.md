# Verified nonroot bundle workflow

Use the same modern API-102 module for Vector 2.2 and JingMatrix LSPatch 1.2 build 487. The patcher
is pinned by SHA-256 in `tools/download-lspatch.sh`. Embedded mode needs no module manager or root
after installation. Android 9/API 28 is the minimum; target-app requirements still apply.

## Signing

A production module and every patched split must use the deliberately selected existing identity.
Configure `TEMPLATE_KS_PATH`, `TEMPLATE_KS_PASS`, `TEMPLATE_KEY_ALIAS`, `TEMPLATE_KEY_PASS` and
`TEMPLATE_CERT_SHA256` in the environment. The certificate fingerprint is public; passwords and
private keys must not enter Git. Missing/partial configuration fails instead of changing keys.

```sh
./gradlew :app:assembleRelease
python3 tools/package-lspatch.py \
  --module app/build/outputs/apk/release/app-release.apk \
  --output target-lspatched.apks --receipt artifacts/package-receipt.json \
  /absolute/original/base.apk /absolute/original/split_config.arm64_v8a.apk
```

For local QA only, explicitly select the existing Android test key in both steps:

```sh
./gradlew :app:assembleRelease -Ptemplate.localTestSigning=true
python3 tools/package-lspatch.py --local-test-signing \
  --module app/build/outputs/apk/release/app-release.apk \
  --output target-lspatched.apks /absolute/original/base.apk
```

Do not combine local-test mode with release-signing variables. Local-test mode uses the existing
`~/.android/debug.keystore`, alias `androiddebugkey`; it does not silently generate another key.
A new machine's different key cannot update an older installation. A test-signed optimized build
is still a test identity, not a production distribution certificate.

## Input and output contracts

Use original unpatched base/splits from one package, versionCode and required device configuration.
The module's scope must include that target, and original base/splits must share one verified signer.
The tool rejects already-patched inputs, duplicate
splits, mixed versions/packages, signing mismatches and output paths that would overwrite inputs.
It normalizes filenames from manifest split identities and orders the base first.

The tool then:

1. Verifies modern metadata, the module's signer, and the configured keystore certificate.
2. Uses the integrity-pinned patcher, embedded module mode and explicit signature level 2.
3. Checks that the exact package/version/split composition is preserved.
4. Verifies the embedded module's SHA-256 against the supplied module APK.
5. Aligns uncompressed native ZIP entries for 16 KiB pages when needed, re-signs with alignment
   preserved, and verifies every final signer and alignment.
6. Checks the archive CRC and atomically replaces the requested bundle only after validation.

Passwords are passed through the environment to a short-lived Java launcher/signing tools, not
placed in the patcher's command line or generated source. Temporary staging inputs are deleted.
The optional receipt records module/bundle hashes, identities, signer, patcher and alignment.

The `.apks` is an SAI ZIP bundle containing only the patched target base/splits. It is **not** a
bundletool APK set. Do not add the standalone module APK at archive top level; it is embedded
inside the target base. Install all required splits together using SAI, with SAI re-signing off.
No separate module installation is needed for embedded mode.

## Runtime verification

Package verification does not establish application compatibility. Verify the actual candidate:
cold startup, scoped Java/native loading, overlay controls, persisted values, background/resume,
rotation, runtime retirement and relevant feature behavior. Keep separate Vector injection out of
the same target during an embedded-module test.

ZIP alignment does not repair ELF segment alignment inside an old target library. Validate the
actual native libraries and intended Android page size as well. Updating the manager or standalone
module cannot replace an already embedded runtime; repatch from originals after changes.

Signature handling cannot satisfy arbitrary server-side attestation. This workflow provides a
reproducible, verified artifact, not a universal compatibility guarantee. See
[the validation record](VALIDATION.md) and [upstream LSPatch](https://github.com/JingMatrix/LSPatch/releases/tag/v1.2).
