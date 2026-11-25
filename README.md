# LSPosed Universal Template

An Android 9+ template for authorized Java/Kotlin and native instrumentation. Vector 2.2 loads
the installed libxposed API-102 module. JingMatrix LSPatch 1.2 (build 487) embeds the same module
in the target APK. By default, only the configured app's main process is in scope. The example
multiplier changes the displayed value until your module supplies the corresponding behavior.

## Build toolchain

CI builds with JDK 21 and the checked-in Gradle wrapper. Java sources target 17. The app uses SDK 37,
NDK `30.0.16248370` and Android SDK CMake `4.1.2`. The default APK ships `armeabi-v7a` and
`arm64-v8a` libraries.

[App build settings](app/build.gradle.kts) · [Gradle wrapper](gradle/wrapper/gradle-wrapper.properties) · [Dependency versions](gradle/libs.versions.toml) · [CI SDK packages](.github/workflows/android.yml)

## Configure and build

Run from the repository root. The configure command updates the Java namespace, JNI registration,
module metadata and target scope together.

```sh
python3 tools/configure-template.py \
  --package com.yourname.yourmodule --name "Your Module" \
  --target com.example.target --author "YourName" --native-lib module_runtime
./gradlew :app:assembleDebug
```

To change only the target packages by hand, update `TemplateConfig.TARGET_PACKAGES` and
`META-INF/xposed/scope.list` together. Set process suffixes and optional IL2CPP initialization
in `TemplateConfig.java`.
For Vector, install the module APK, enable it for the configured target and restart that app.
For LSPatch, embed the signed module and install the patched target with its required splits.
Local debug and release builds use `/Users/jordan/.android/signing/jordan-modules.json`.
[Shared signing identity](SIGNING.md) · [LSPatch packaging](docs/LSPATCH_NONROOT.md#signing)

## Adding a feature

Register application-specific controls before `Bootstrap.start`, or alongside the registrations
in `FeatureRegistry.initialize`. Late registration also retains values already loaded from disk.

```java
FeatureRegistry.register(FeatureRegistry.Feature.bool("feature", "Feature", false));
FeatureRegistry.register(FeatureRegistry.Feature.integer("amount", "Amount", 1, 1, 100));
FeatureRegistry.register(FeatureRegistry.Feature.number("ratio", "Ratio", 1f, 0f, 2f, 0.05f));
```

The overlay renders registered controls. Integer bounds must fit ±16,777,216. A stepped control
supports up to 10,000 intervals. The five-argument `number` overload accepts continuous values.
Slider changes take effect live and commit on drag completion or panel close. Settings are saved
atomically in `.<module applicationId>.settings.json` in the target app's files directory.
The default scope has one writer. Multiple writing processes need coordination or separate files.

To make a hook follow the menu, read its setting and the module enable flag when it runs.
The native helpers start at most once per process. The template sets `autoHotReload=false`.

## Guides

- [Overlay layout and gestures](docs/MOD_MENU_STYLE.md)
- [Java, JNI and engine integration](docs/ENGINE_NATIVE_WORKFLOWS.md)
- [ShadowHook and native library discovery](docs/SHADOWHOOK_NOTES.md)
- [IL2CPP metadata and lifetime rules](docs/ENGINE_IL2CPP.md)
- [Frida reconnaissance](docs/FRIDA_EMULATOR_QUICKSTART.md) and [host tools](tools/README.md)
- [Host tests, Android builds, lint and device checks](tests/README.md)
