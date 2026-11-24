# Release signing and LSPatch bundles

Vector loads the installed module APK. JingMatrix LSPatch embeds the same signed API-102
module in the target for rootless installation. The packaging tool retrieves the
[checksum-pinned patcher](../tools/download-lspatch.sh).

Run the commands from the repository root with Python 3.11+, JDK 21 (`java` and `keytool`),
Android SDK Build Tools 37.0.0 (`aapt2`, `apksigner` and `zipalign`), `curl` and `shasum`
on PATH. Configure the module for the target package before building.
[Module setup](../README.md#configure-and-build)

## Signing

For a production release, set the following environment variables. Gradle signs the module.
The packager verifies its certificate and signs the patched target APKs with the same identity.

| Variable | Value |
| --- | --- |
| `TEMPLATE_KS_PATH` | Absolute path to an existing keystore |
| `TEMPLATE_KS_PASS` | Keystore password |
| `TEMPLATE_KEY_ALIAS` | Alias of the private key |
| `TEMPLATE_KEY_PASS` | Key password |
| `TEMPLATE_CERT_SHA256` | Expected SHA-256 fingerprint of the signing certificate |

```sh
./gradlew :app:assembleRelease
```

For local QA, leave all five variables unset and select test signing explicitly.

```sh
./gradlew :app:assembleRelease -Ptemplate.localTestSigning=true
```

Gradle can create the default debug keystore during the build. The packager's test mode
requires `~/.android/debug.keystore` to exist with the standard Android debug alias and passwords.
Preserve the keystore for updates signed with the same identity.

## Package and install

Supply the original base APK and every split required by the device. They must share a package,
version code and original signer. The module's scope must include that package.

```sh
python3 tools/package-lspatch.py \
  --module app/build/outputs/apk/release/app-release.apk \
  --output target-lspatched.apks \
  /absolute/original/base.apk /absolute/original/split_config.arm64_v8a.apk
```

For the QA build, add `--local-test-signing` to that packaging command. To save the verified
hashes, signer and split identities, add `--receipt PATH`.

The tool verifies the embedded module bytes, package and split identities, signer, ZIP alignment
and archive CRC before replacing the output. ZIP alignment covers uncompressed native entries.
ELF segment alignment inside each library also needs to support the device's page size.

Install the `.apks` archive with SAI and turn SAI re-signing off. It contains the patched target
APKs with the module embedded in the base. Rebuild from the originals when the module or patcher
changes. Test startup and overlay behavior on the intended device with only the embedded module
active for that target.
