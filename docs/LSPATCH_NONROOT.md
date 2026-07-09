# Non-root delivery with LSPatch

Purpose: ship this module to a device **without root** by embedding it into a target APK with
[LSPatch](https://github.com/JingMatrix/LSPatch) (the maintained fork; the original
`LSPosed/LSPatch` works the same way). This is the counterpart to the root/LSPosed flow in the
main `README.md`. It is engine-neutral — nothing here is specific to Unity/IL2CPP; it applies to
any app you are authorized to test.

Authorized testing only. Only patch apps you own or are explicitly permitted to modify.

---

## 1. Why you must build the `lspatch` flavor (not the modern one)

LSPosed (root) speaks the **modern** `io.github.libxposed` API (level 101 here). LSPatch (non-root)
only implements the **classic** `de.robv.android.xposed` API, whose level is **93**. The two use
different entry-point discovery:

| Framework | API | Discovers entry via | Entry class |
|-----------|-----|---------------------|-------------|
| LSPosed (root) | 101 | `META-INF/xposed/java_init.list` | `ModuleEntry` (extends `XposedModule`) |
| LSPatch (non-root) | 93 | `assets/xposed_init` | `LSPatchEntry` (implements `IXposedHookLoadPackage`) |

This template ships **both** via a `framework` product-flavor dimension (see `app/build.gradle.kts`):

```bash
export ANDROID_HOME=$HOME/Library/Android/sdk
./gradlew :app:assembleLspatchRelease   # -> app-lspatch-release.apk  (embed THIS into the target)
./gradlew :app:assembleLsposedRelease   # -> app-lsposed-release.apk  (install for root/LSPosed)
```

**The silent-rejection failure mode.** LSPatch reads the manifest `xposedminversion` and the
`minApiVersion` in `META-INF/xposed/module.prop`. If those advertise **101** (the modern flavor),
LSPatch treats the module as incompatible and **loads nothing** — no crash, no toast, no logcat
error from your code, because your code never runs. The `lspatch` flavor sets `xposedminversion=93`
(via the `${xposedMinVersion}` manifest placeholder) and ships `assets/xposed_init` pointing at the
classic `LSPatchEntry`, so LSPatch accepts and loads it. If your patched app "does nothing", the
first thing to check is that you embedded the **lspatch** artifact, not the lsposed one.

---

## 2. Embedding the module into a target APK

Grab `lspatch.jar` from LSPatch releases and run the CLI:

```bash
java -jar lspatch.jar <base.apk> \
  -m app-lspatch-release.apk \   # the module APK (repeatable: -m a.apk -m b.apk)
  -o out \                       # output directory
  -l 2 \                         # sigBypassLevel (see below)
  -k <keystore> <ks-pass> <key-alias> <key-pass> \   # optional: sign with your own keystore
  -f                             # force overwrite existing output
```

If you omit `-k`, LSPatch signs the output with its **bundled debug keystore**. That is fine for a
throwaway test, but every re-patch with the bundled key still produces a signature different from
the app's original Play/store signature — see §3 on why that matters and §4 on uninstall-first.

### `sigBypassLevel` (`-l`) — what 0/1/2 mean

Re-signing changes the APK signature, so any in-app signature check (or Play integrity check) will
see the "wrong" certificate. `sigBypassLevel` controls how hard LSPatch works to hide that:

- **0 — none.** No signature spoofing. Use only for apps that never verify their own signature.
- **1 — `PackageManager` spoofing.** Hooks `PackageManager.getPackageInfo(...)` so calls that ask
  for the app's signature get the **original** signature back. Defeats most app-level self-checks.
- **2 — level 1 + on-disk APK reference.** Also serves the original signing block when the app reads
  its own APK file directly (some anti-tamper and licensing paths do this). This is the most
  compatible setting and a good default; drop to 1 or 0 only if you have a reason.

Install the patched output from `out/` (see §3 for split apps).

---

## 3. Split APKs: sign every split with the SAME certificate

Modern apps (especially anything from Google Play as an App Bundle) ship as **split APKs**: a
`base.apk` plus `config.*` splits (`config.arm64_v8a`, `config.xxhdpi`, `config.en`, …). Two rules:

1. **Patch/sign all of them, and with the same certificate.** Android requires every installed split
   of a package to share one signing certificate. If the base is signed with key A and a config
   split with key B (or is left store-signed), installation fails with:

   ```
   INSTALL_FAILED_UPDATE_INCOMPATIBLE      (or INSTALL_FAILED_INVALID_APK / signatures do not match)
   ```

2. **Install them together, atomically:**

   ```bash
   adb install-multiple out/base.apk out/split_config.arm64_v8a.apk out/split_config.xxhdpi.apk ...
   ```

   `install-multiple` commits all splits in one session so the signature-consistency check passes.
   Installing the base alone and adding splits later tends to fail the same way.

If a build of this app is **already installed** (e.g. the Play version, or a previous patch signed
with a different key), its signature will not match your newly signed splits. **Uninstall first**,
then install the patched set:

```bash
adb uninstall <package>
adb install-multiple out/*.apk
```

Uninstalling clears app data too, so re-do any login/first-run setup afterward.

---

## 4. The metaloader first-launch flake

A **freshly installed** LSPatch build sometimes crashes **once** on first launch inside LSPatch's
bootstrap component:

```
java.lang.NoClassDefFoundError: ... org.lsposed.lspatch.metaloader.LSPAppComponentFactoryStub
```

This is a known first-launch timing issue in the metaloader stub (the AppComponentFactory shim
LSPatch injects). **Just relaunch the app** — the second start loads the stub and boots normally. If
it crashes *every* launch, that is a different problem (usually a bad/incomplete split install from
§3 or the wrong flavor from §1).

**Aggravated by a root LSPosed framework.** If the same device also runs a root LSPosed/Zygisk
framework that is scoped to (or not excluding) this package, both LSPosed and the embedded LSPatch
loader can inject the same process and race, which makes the metaloader flake far more likely (and
muddies your logs). For a clean non-root test, **exclude the target app** from Zygisk / the LSPosed
scope / the denylist so only LSPatch is injecting it.

---

## 5. Optional: PairIP / Play licensing (app-category-specific, NOT in the template by default)

Many Play-distributed apps embed Google's **PairIP** anti-tamper and/or Play licensing
(`com.pairip.licensecheck.*`). These verify that the running APK is the untouched, Play-signed
build. A re-signed LSPatch build fails that check, and the app typically bounces the user to its
Play Store page instead of starting.

This only affects the **non-root, re-signed** path (a root install runs the original Play-signed
APK, so the check passes natively). Because it is specific to that app category — not something the
generic template should carry — the template code does **not** include a bypass. If you need it for
an authorized target, add it to your `LSPatchEntry` at `Application.attach`, wrapped so it is a
no-op when the class is absent:

```java
// In LSPatchEntry, inside the afterHookedMethod once you have the Context/ClassLoader.
// App-category-specific — only for a non-root re-signed build of a PairIP-protected app.
private static void bypassPairipLicense(ClassLoader cl) {
    if (cl == null) return;
    try {
        XposedHelpers.findAndHookMethod(
                "com.pairip.licensecheck.ILicenseV2ResultListener$Stub", cl,
                "onTransact", int.class, android.os.Parcel.class,
                android.os.Parcel.class, int.class,
                XC_MethodReplacement.returnConstant(true));
    } catch (Throwable ignored) {
        // Class not present (non-PairIP app) or already handled — leave the app untouched.
    }
}
```

Forcing the binder result callback `onTransact` to report success means the deny verdict is never
delivered to the license verifier, so there is no Play-Store redirect. Keep `sigBypassLevel` at 2
(§2) alongside this, since PairIP also inspects the on-disk signing block.

---

## 6. Quick checklist

- [ ] Built and embedded the **`lspatch`** flavor (`assembleLspatchRelease`), not `lsposed`.
- [ ] `xposedminversion` / `minApiVersion` resolve to **93** in the embedded module.
- [ ] Chose a `sigBypassLevel` (default **2**) appropriate to the target's self-checks.
- [ ] Patched **every** split and signed them all with the **same** key.
- [ ] Used `adb install-multiple`; uninstalled any prior differently-signed install first.
- [ ] If it crashed once on `LSPAppComponentFactoryStub`, relaunched.
- [ ] Excluded the app from any root LSPosed/Zygisk injection for a clean non-root test.
- [ ] (Only if the app uses PairIP/licensing) added the bypass to your `LSPatchEntry`.
