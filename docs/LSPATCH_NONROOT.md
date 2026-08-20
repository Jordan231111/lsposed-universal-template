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
| `manager-v1.0-455-release.apk` | `fc6b7967afef72412288eb2aa52d001d4f339d6e4232f6d0be9830af7bbc7108` |

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

## 2. One modern module APK for Vector and LSPatch

Build the template once:

```bash
./gradlew :app:assembleRelease
MODULE_APK=app/build/outputs/apk/release/app-release.apk
```

The resulting APK is valid for both rooted Vector and rootless LSPatch 1.0. There is no separate
classic `lspatch` flavor anymore. LSPatch 1.0 reads `META-INF/xposed/module.prop`; when
`targetApiVersion` is 101 or newer, it loads entry classes from
`META-INF/xposed/java_init.list`. This template advertises API 102 and lists `ModuleEntry` there.

You can verify those two load-bearing files before patching:

```bash
unzip -p "$MODULE_APK" META-INF/xposed/module.prop
unzip -p "$MODULE_APK" META-INF/xposed/java_init.list
```

Expected values include `minApiVersion=102`, `targetApiVersion=102`, and
`com.template.lsposed.ModuleEntry`. LSPatch 1.0 rejects API 100 and treats a lower target API as
legacy only when `assets/xposed_init` is present.

## 3. Choose a patch mode

LSPatch 1.0 chooses the module source at patch time:

| Mode | CLI switch | Module source | Manager required after install? |
|---|---|---|---|
| Embedded (called Integrated in parts of the UI) | `-m <module.apk>` | Module APKs baked into the patched target | No |
| Manager | `--manager` | Modules and scope supplied live by the installed manager | Yes |

Use embedded mode for a self-contained test APK. Changing its modules requires another patch. Use
manager mode when you want to change scope or module selection without rebuilding the target.
`--manager` and `-m` are mutually exclusive.

This template sets `autoHotReload=false` because it owns native hooks, a worker thread, and activity
lifecycle callbacks without a teardown path. Manager-mode changes therefore require a target
restart even though LSPatch 1.0 can hot-reload modules that explicitly opt in safely.

## 4. Patch from the command line

### Embedded mode, one APK

```bash
java -jar lspatch-v1.0-455-release.jar \
  -m "$MODULE_APK" \
  -o out \
  -l 2 \
  -f \
  base.apk
```

The stable build writes `out/base-455-lspatched.apk`.

### Embedded mode, split APK set

Pass the base and every split in the **same invocation**:

```bash
java -jar lspatch-v1.0-455-release.jar \
  -m "$MODULE_APK" \
  -o out \
  -l 2 \
  -f \
  base.apk \
  split_config.arm64_v8a.apk \
  split_config.en.apk \
  split_config.xxhdpi.apk
```

LSPatch injects the loader into the APK carrying the application component and repacks the other
splits. It signs every output with the same key, which is required for an atomic split install.

### Manager mode

Install `manager-v1.0-455-release.apk`, then patch without `-m`:

```bash
java -jar lspatch-v1.0-455-release.jar \
  --manager \
  -o out \
  -l 2 \
  -f \
  base.apk
```

Install the module APK on the device and assign the patched target in LSPatch Manager. The manager
must remain installed. If you used LSPatch 1.0's manager-cloaking feature, add
`--manager-package <actual.manager.package>` so the patched app binds to the renamed manager.

### Current CLI options worth knowing

Run `java -jar lspatch-v1.0-455-release.jar --help` for the authoritative list.

| Option | Effect |
|---|---|
| `-m`, `--embed <apk>` | Embed a module; repeat for multiple modules. Incompatible with `--manager`. |
| `--manager` | Resolve modules and scope from LSPatch Manager at runtime. |
| `--manager-package <id>` | Bind manager mode to a cloaked/custom manager package. |
| `-l`, `--sigbypasslv 0..2` | Select local signature bypass. The CLI default is **0**. |
| `-k`, `--keystore <file> <store-pass> <alias> <key-pass>` | Use a stable custom signing key. |
| `--injectdex` | Inject loader dex directly; mainly useful for browser-style targets that need it. |
| `--documents-provider` | Expose the patched app's private data through Android's document picker. |
| `--version-code <n>` | Override the patched app's version code. |
| `--name <label>` | Override its launcher label. |
| `--target-sdk <n>` | Override `targetSdkVersion`. |
| `--extract-libs` | Force `android:extractNativeLibs=true`. |
| `--cleartext` | Force `android:usesCleartextTraffic=true`. |
| `--add-permission <name>` | Add a permission; repeat as needed. |
| `-d`, `--debuggable` | Make the target debuggable. |
| `-v`, `--verbose` | Print verbose patcher output. |

`--documents-provider`, `--cleartext`, and `--debuggable` deliberately widen access or weaken target
settings. Enable them only when the test requires them, and do not distribute that build as though
it were the original app.

## 5. Signing, signature bypass, and installation

If `-k` is omitted, LSPatch uses its bundled key. Use your own stable keystore when repeated patches
must upgrade one another. Neither key matches a store-signed installation unless you own and use the
original signing key, so uninstall a differently signed copy first:

```bash
adb uninstall com.example.target
```

Install a single output with:

```bash
adb install out/base-455-lspatched.apk
```

Install a split set together:

```bash
adb install-multiple \
  out/base-455-lspatched.apk \
  out/split_config.arm64_v8a-455-lspatched.apk \
  out/split_config.en-455-lspatched.apk \
  out/split_config.xxhdpi-455-lspatched.apk
```

Uninstalling clears app data. Back up authorized test data first when necessary.

The `-l` levels affect local signature checks:

- **0:** disabled. This is the command-line default.
- **1:** spoof signature results returned through `PackageManager`.
- **2:** level 1 plus handling for direct reads of the APK through libc `openat`.

The manager UI defaults a new patch to level 2, but the CLI does not. Signature bypass cannot make a
re-signed APK pass Play Integrity, server-side attestation, or every app-specific anti-tamper scheme.
Do not treat it as a general licensing or integrity bypass.

## 6. Upgrade and troubleshooting checklist

- [ ] Downloaded the current stable release (or deliberately selected a named canary) and verified its hash.
- [ ] Re-patched targets that were built with LSPatch 0.8 or older.
- [ ] Built `app-release.apk`; did not use the removed API-93 flavor.
- [ ] Confirmed `targetApiVersion=102` and `META-INF/xposed/java_init.list` in the module APK.
- [ ] Chose embedded (`-m`) or manager (`--manager`) mode, never both.
- [ ] Passed the base and all splits in one patch command and installed all outputs together.
- [ ] Used one stable signing key and uninstalled any copy signed by another key.
- [ ] Restarted the target after changing this template's module because hot reload is disabled.
- [ ] Kept root Vector/LSPosed injection out of the same target during a clean LSPatch test.

If LSPatch does not list the module, inspect the two `META-INF/xposed` files from section 2. If the
module is listed but does not run, use the debug module and LSPatch's `-debug.jar`/manager build,
enable verbose patcher output with `-v`, force-stop the target, and capture a fresh logcat. If an app
patched before v1.0 behaves inconsistently, rebuild it rather than attempting to reuse the old
embedded loader.
