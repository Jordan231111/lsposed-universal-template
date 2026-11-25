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

Local builds load the shared identity from `/Users/jordan/.android/signing/jordan-modules.json`.
See [SIGNING.md](../SIGNING.md) for the keystore, alias, certificate and CI configuration.
The module, embedded module and patched splits use the same signer. Missing or partial
configuration fails before an output is replaced.

```sh
./gradlew :app:assembleRelease
```

## Package and install

Supply the original base APK and every split required by the device. They must share a package,
version code and original signer. The module's scope must include that package.

```sh
python3 tools/package-lspatch.py \
  --module app/build/outputs/apk/release/app-release.apk \
  --output target-lspatched.apks \
  /absolute/original/base.apk /absolute/original/split_config.arm64_v8a.apk
```

To save the verified hashes, signer and split identities, add `--receipt PATH`.

The tool verifies the embedded module bytes, package and split identities, signer, ZIP alignment
and archive CRC before replacing the output. ZIP alignment covers uncompressed native entries.
ELF segment alignment inside each library also needs to support the device's page size.

The `.apks` archive contains the patched target APKs with the module embedded in the base.
Install it using SAI with re-signing off, or extract the APKs and install the complete set with
`adb install-multiple`. Rebuild from the originals when the module or patcher changes. To test
the embedded module in isolation, disable any separately installed Vector copy for that target.
