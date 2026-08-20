# Non-root delivery with LSPatch 1.0

Use [JingMatrix/LSPatch](https://github.com/JingMatrix/LSPatch) to run this module in an app without
root or Zygisk. LSPatch rewrites the target APK so that the app starts Vector's framework runtime
and loads the selected Xposed modules inside its own processes.

Only patch apps you own or are explicitly authorized to test.

## 1. Current release and compatibility

This guide was checked on **2026-08-19** against the official
[LSPatch releases](https://github.com/JingMatrix/LSPatch/releases):

| Channel | Release | Patcher | Status |
|---|---|---|---|
| Stable | [`v1.0`](https://github.com/JingMatrix/LSPatch/releases/tag/v1.0), build 455 | `lspatch-v1.0-455-release.jar` | Recommended |
| Canary | [`canary-460`](https://github.com/JingMatrix/LSPatch/releases/tag/canary-460) | `lspatch-v1.0-460-release.jar` | CI-tested prerelease |

The stable release assets used by this guide are:

| Asset | SHA-256 |
|---|---|
| `lspatch-v1.0-455-release.jar` | `405e466336d89dcdeca0a5faebc7a5bcc17470a03f9de1ac047647a80febf351` |

Download the stable command-line patcher and verify it before use:

```bash
curl -fL \
  -o lspatch-v1.0-455-release.jar \
  https://github.com/JingMatrix/LSPatch/releases/download/v1.0/lspatch-v1.0-455-release.jar
shasum -a 256 lspatch-v1.0-455-release.jar
```

Check [`releases/latest`](https://github.com/JingMatrix/LSPatch/releases/latest) before copying a
pinned command from this document. Canary releases are deliberately short-lived; the project keeps
only the five newest canaries.

LSPatch 1.0 is a ground-up rebuild on Vector. Its patched-app runtime supports:

- modern libxposed modules at API 101 or newer (API 102 in this template);
- legacy `de.robv.android.xposed` modules for compatibility;
- Android 9 (API 28) or newer;
- a real `IXposedService` for modern modules.

This is materially different from LSPatch 0.8 and older. A target patched with an older LSPatch does
not acquire the 1.0 runtime merely because the manager was updated: **re-patch every target app**.

## 2. Build the module with the existing Android debug key

This workflow deliberately uses the standard local Android debug key for repeatable test installs:

```text
${HOME}/.android/debug.keystore
store password: android
alias:          androiddebugkey
key password:   android
```

Verify the key exists, then build without any release-signing environment override:

```zsh
ANDROID_DEBUG_KEYSTORE="${HOME}/.android/debug.keystore"
test -f "$ANDROID_DEBUG_KEYSTORE"
keytool -list \
  -keystore "$ANDROID_DEBUG_KEYSTORE" \
  -storepass android \
  -alias androiddebugkey

env -u TEMPLATE_KS_PATH -u TEMPLATE_KS_PASS \
  -u TEMPLATE_KEY_ALIAS -u TEMPLATE_KEY_PASS \
  ./gradlew :app:assembleRelease

MODULE_APK="$PWD/app/build/outputs/apk/release/app-release.apk"
```

With those four environment overrides absent, the Gradle release build falls back to Android's
debug signing configuration. The LSPatch command in section 4 passes the same keystore explicitly,
so the module APK and every patched target APK use one consistent certificate.

The module is valid for both rooted Vector and rootless LSPatch 1.0. LSPatch reads
`META-INF/xposed/module.prop`; when `targetApiVersion` is 101 or newer, it loads entries from
`META-INF/xposed/java_init.list`. Verify the module before patching:

```zsh
unzip -p "$MODULE_APK" META-INF/xposed/module.prop
unzip -p "$MODULE_APK" META-INF/xposed/java_init.list
```

Expected values include `minApiVersion=102`, `targetApiVersion=102`, and
`com.template.lsposed.ModuleEntry`.

## 3. Required delivery: embedded module inside an SAI bundle

This template's non-root workflow is intentionally fixed:

- always pass `-m "$MODULE_APK"` so the module is embedded;
- never use `--manager` mode;
- pass the target base APK and every target split to one LSPatch invocation;
- sign every output with `${HOME}/.android/debug.keystore`; and
- package the patched target APK set as one `target-lspatched.apks` file.

The module APK becomes `assets/lspatch/modules/<module-package>.apk` **inside the patched base APK**.
Do not also place `app-release.apk` at the top level of `target-lspatched.apks`: SAI treats top-level
APK files as the package set it must install, and a separate module package would make that set
invalid. The top level contains only the patched target base and its patched target splits.

[SAI's format description](https://github.com/Aefyr/SAI/blob/master/META-FORMAT.md) defines `.apks`
as a renamed ZIP archive. `icon.png` and `meta.sai_v1.json`/`meta.sai_v2.json` are metadata added by
SAI exports, not requirements for installing a ZIP that contains a valid base-and-split APK set.

## 4. Copy-paste embedded patch and `.apks` bundle workflow

The example below works in macOS `zsh`. Keep only `base.apk` in `TARGET_APKS` for a monolithic app;
for a split app, list the base and every required split.

```zsh
LSPATCH_JAR="$PWD/lspatch-v1.0-455-release.jar"
MODULE_APK="$PWD/app/build/outputs/apk/release/app-release.apk"
ANDROID_DEBUG_KEYSTORE="${HOME}/.android/debug.keystore"
PATCH_OUTPUT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/lspatch-output.XXXXXX")"
SAI_TEMP_BUNDLE="${PATCH_OUTPUT_DIR}.apks"
SAI_BUNDLE="$PWD/target-lspatched.apks"

TARGET_APKS=(
  "$PWD/base.apk"
  "$PWD/split_config.arm64_v8a.apk"
  "$PWD/split_config.en.apk"
  "$PWD/split_config.xxhdpi.apk"
)

test -f "$LSPATCH_JAR"
test -f "$MODULE_APK"
test -f "$ANDROID_DEBUG_KEYSTORE"

java -jar "$LSPATCH_JAR" \
  -m "$MODULE_APK" \
  -k "$ANDROID_DEBUG_KEYSTORE" android androiddebugkey android \
  -o "$PATCH_OUTPUT_DIR" \
  -l 2 \
  -f \
  "${TARGET_APKS[@]}"

(
  cd "$PATCH_OUTPUT_DIR"
  /usr/bin/zip -q -9 "$SAI_TEMP_BUNDLE" ./*.apk
)

mv -f "$SAI_TEMP_BUNDLE" "$SAI_BUNDLE"
/usr/bin/zip -T "$SAI_BUNDLE"
unzip -Z1 "$SAI_BUNDLE"
shasum -a 256 "$SAI_BUNDLE"
```

The result is always `target-lspatched.apks`, including when the target has only one APK. The
temporary patch directory is unique, so a stale split from an earlier run cannot leak into the new
bundle. Re-running the final `mv -f` deliberately replaces the prior bundle with the newly verified
one.

### Verify the embedded module and consistent signer

Before transferring the bundle, confirm that LSPatch embedded the module and that every APK has the
same signer:

```zsh
unzip -l "$PATCH_OUTPUT_DIR"/base-*-lspatched.apk \
  | grep 'assets/lspatch/modules/.*\.apk'

for apk in "$MODULE_APK" "$PATCH_OUTPUT_DIR"/*.apk; do
  printf '%s\n' "$apk"
  apksigner verify --verbose --print-certs "$apk" \
    | grep 'certificate SHA-256 digest'
done
```

Every printed certificate digest must be identical. If `apksigner` is not on `PATH`, use
`${HOME}/Library/Android/sdk/build-tools/37.0.0/apksigner`.

## 5. Install the bundle with SAI

1. Copy `target-lspatched.apks` to the Android device.
2. Open SAI (Split APKs Installer) and choose **Install APKs**.
3. Select `target-lspatched.apks`.
4. Leave any SAI re-signing/signing option disabled; LSPatch already signed every member with the
   consistent Android debug key.
5. Approve the Android package-installer prompt.

SAI installs all APK members in one package session. A differently signed store build or an older
patch made with another key cannot be upgraded in place. Uninstall that copy first, noting that an
uninstall clears its app data.

## 6. Relevant LSPatch options and signing limits

Run `java -jar lspatch-v1.0-455-release.jar --help` for the authoritative option list.

| Option | Use in this workflow |
|---|---|
| `-m`, `--embed <apk>` | Required. Embeds this template's module in the target. |
| `-k`, `--keystore <file> <store-pass> <alias> <key-pass>` | Required. Uses the existing Android debug key consistently. |
| `-l`, `--sigbypasslv 2` | Explicitly enables `PackageManager` plus libc `openat` signature handling. |
| `--injectdex` | Optional; mainly useful for browser-style targets that require direct loader-dex injection. |
| `--documents-provider` | Optional; exposes the patched app's private data through Android's document picker. |
| `--version-code <n>` | Optional version-code override. |
| `--name <label>` | Optional launcher-label override. |
| `--target-sdk <n>` | Optional `targetSdkVersion` override. |
| `--extract-libs` | Optional `android:extractNativeLibs=true` override. |
| `--cleartext` | Optional `android:usesCleartextTraffic=true` override. |
| `--add-permission <name>` | Optional permission addition; repeat as needed. |
| `-d`, `--debuggable` | Optional debuggable target build. |
| `-v`, `--verbose` | Optional verbose patcher log. |

The standard Android debug keystore credentials are public and intended only for development. They
provide consistent local signatures, not production identity or security. Signature bypass also
cannot make a re-signed APK pass Play Integrity, server-side attestation, or every app-specific
anti-tamper scheme.

## 7. Upgrade and troubleshooting checklist

- [ ] Downloaded the current stable LSPatch release and verified its hash.
- [ ] Re-patched targets built with LSPatch 0.8 or older.
- [ ] Built `app-release.apk` with the existing Android debug key and no signing-env override.
- [ ] Confirmed `targetApiVersion=102` and `META-INF/xposed/java_init.list` in the module APK.
- [ ] Passed `-m "$MODULE_APK"`; did not use manager mode.
- [ ] Passed the target base and every required target split in one patch command.
- [ ] Passed the explicit `-k ... debug.keystore android androiddebugkey android` arguments.
- [ ] Confirmed the module is nested inside the patched base APK.
- [ ] Confirmed every module/target certificate digest is identical.
- [ ] Confirmed the `.apks` top level contains only patched APKs from the target package.
- [ ] Installed `target-lspatched.apks` through SAI with SAI re-signing disabled.
- [ ] Re-patched and reinstalled after changing the module; this template disables hot reload.
- [ ] Kept root Vector/LSPosed injection out of the same target during a clean LSPatch test.

If the module does not run, inspect the modern metadata from section 2 and the embedded module path
from section 4. Re-run with LSPatch's debug JAR plus `-v`, force-stop the target, and capture a fresh
logcat. If a target patched before v1.0 behaves inconsistently, rebuild it rather than reusing the
old embedded loader.
