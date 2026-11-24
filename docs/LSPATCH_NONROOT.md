# Release signing and LSPatch bundles

Vector loads the installed module APK. JingMatrix LSPatch embeds the same signed API-102
module in the target for rootless installation. The packaging tool retrieves the
[checksum-pinned patcher](../tools/download-lspatch.sh).

The examples assume the repository root. The Python packager uses `hashlib.file_digest`
(Python 3.11+), and the pinned patcher uses Java 21 class files. For packaging, put `java`,
`keytool`, `aapt2`, `apksigner` and `zipalign` on PATH. CI installs Android SDK Build Tools
37.0.0. The downloader uses `shasum` to verify the JAR and `curl` on a cache miss.
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

The `.apks` archive contains the patched target APKs with the module embedded in the base.
Install it using SAI with re-signing off, or extract the APKs and install the complete set with
`adb install-multiple`. Rebuild from the originals when the module or patcher changes. To test
the embedded module in isolation, disable any separately installed Vector copy for that target.
