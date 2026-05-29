# Undetected Frida + Android emulator quickstart

Use this workflow only on apps/devices you own or are authorized to test.

The goal is Frida-first reconnaissance: gather classes, methods, native libraries, symbols, call timing, and ABI facts before you hard-code a permanent LSPosed or ShadowHook hook.

## 0. Host tools on macOS

```bash
brew install android-platform-tools python xz wget
python3 -m pip install --upgrade pip frida-tools
adb version
frida --version
```

## 1. Pick the right emulator image

For Java-only LSPosed hooks, x86_64 emulator images are fine.

For ShadowHook native inline hooks, use arm/arm64. ShadowHook does not support x86/x86_64. On Apple Silicon, prefer an arm64 system image. On Intel Macs, use a real arm64 device for native-hook validation.

Check what your emulator/device is:

```bash
adb devices
adb shell getprop ro.product.cpu.abi
adb shell getprop ro.product.cpu.abilist
adb shell uname -m
adb shell getprop ro.build.version.release
adb shell getprop ro.build.version.sdk
```

## 2. Root / writable location check

Most Frida server workflows require root in the emulator/device process namespace.

```bash
adb root
adb remount
adb shell id
adb shell su -c id
adb shell 'echo $PATH'
adb shell 'command -v su || which su'
adb shell ls -ld /data/local/tmp
```

If `adb root` fails, try `adb shell su -c id`. If both fail, use a rooted emulator image, Magisk-rooted AVD, Genymotion, or a rooted test device.

Some Magisk-rooted emulators resolve `su` from the Android runtime APEX path. For example, one confirmed setup resolves `su` as `/apex/com.android.runtime/bin/su`, with `/apex/com.android.runtime/bin` already present in `PATH`. If your image behaves this way, keep that path available when running root commands and Frida server:

```bash
adb shell 'export PATH=/apex/com.android.runtime/bin:$PATH; su -c id'
```

## 3. Download matching zer0def/undetected-frida release

Open the releases page and choose the latest release, then make your host `frida --version` match the device `frida-server` version:

- https://github.com/zer0def/undetected-frida/releases

Use this undetected Frida server build for the workflow below. Do not use older `17.8.x` builds just because they still run; only pin an older release when your host tools are deliberately pinned to the same version. Do not substitute stock `frida-server` unless you are intentionally comparing behavior or debugging a version/build mismatch.

Architecture mapping:

- `arm64-v8a` / `aarch64` -> `android-arm64`
- `armeabi-v7a` / `armv7l` -> `android-arm`
- `x86_64` -> `android-x86_64`
- `x86` / `i686` -> `android-x86`

Example for arm64 using the latest release when this doc was updated; replace the version if the releases page shows a newer one:

```bash
UND_FRIDA_VERSION=17.9.6
xz -d undetected-frida-server-${UND_FRIDA_VERSION}-android-arm64.xz
mv undetected-frida-server-${UND_FRIDA_VERSION}-android-arm64 frida-server
chmod +x frida-server
```

## 4. Push and run server with adb

```bash
adb push frida-server /data/local/tmp/frida-server
adb shell chmod 755 /data/local/tmp/frida-server
adb shell 'export PATH=/apex/com.android.runtime/bin:$PATH; su -c "pidof frida-server >/dev/null && kill $(pidof frida-server) || true"'
adb shell 'export PATH=/apex/com.android.runtime/bin:$PATH; su -c "/data/local/tmp/frida-server -D"'
adb shell 'export PATH=/apex/com.android.runtime/bin:$PATH; su -c "ps -A | grep frida"'
frida-ls-devices
frida-ps -Uai
```

If `su -c` does not work on your image, enter a root shell and run it manually:

```bash
adb shell
su
/data/local/tmp/frida-server -D
ps -A | grep frida
exit
exit
```

## 5. Baseline app reconnaissance commands

Replace `com.example.target` with your app package.

```bash
adb shell pm list packages | grep example
adb shell dumpsys package com.example.target | grep -E 'versionName|versionCode|primaryCpuAbi|nativeLibraryDir|dataDir'
adb shell am force-stop com.example.target
adb logcat -c
adb shell monkey -p com.example.target 1
adb shell pidof com.example.target
frida-ps -Uai | grep com.example.target
```

Capture logs while the app starts:

```bash
adb logcat -v time | grep -E 'ActivityTaskManager|AndroidRuntime|AppRuntime|shadowhook|libc|dlopen|com.example.target'
```

## 6. Run the provided Frida reconnaissance script

```bash
frida -U -f com.example.target -l scripts/frida/android_recon.js --no-pause
```

Attach to an already running app:

```bash
frida -U -n com.example.target -l scripts/frida/android_recon.js
```

What to look for:

- actual process name
- ABI
- loaded native modules
- Java classes and method names relevant to your feature
- timing of native library loads
- stable exported symbols

## 7. Export native symbols before writing ShadowHook code

List loaded modules with Frida:

```bash
frida -U -n com.example.target -l scripts/frida/list_modules.js
```

Inspect a likely library:

```bash
frida -U -n com.example.target -l scripts/frida/list_exports.js
```

Or use adb to pull the APK/native libraries and inspect offline with NDK tools:

```bash
adb shell pm path com.example.target
adb pull /data/app/REPLACE_WITH_BASE_APK_PATH/base.apk ./target-base.apk
unzip -l target-base.apk | grep '\.so'
unzip target-base.apk 'lib/arm64-v8a/*.so' -d ./target-libs
llvm-readelf -sW ./target-libs/lib/arm64-v8a/libtarget.so | grep -v ' UND '
```

## 8. Frida-first to LSPosed/ShadowHook conversion checklist

Before moving anything into this template:

1. Confirm package and process name.
2. Confirm ABI.
3. Confirm classloader timing for Java hooks.
4. Confirm method signature and argument/return types.
5. Confirm native library load timing.
6. Confirm exported symbol or reliable address resolution.
7. Test the behavior with Frida on a clean app launch.
8. Only then write the permanent hook.

## 9. Troubleshooting

Frida cannot connect:

```bash
adb kill-server
adb start-server
adb devices
adb shell 'echo $PATH'
adb shell 'command -v su || which su'
adb shell 'export PATH=/apex/com.android.runtime/bin:$PATH; su -c "pidof frida-server >/dev/null && kill $(pidof frida-server) || true"'
adb shell 'export PATH=/apex/com.android.runtime/bin:$PATH; su -c "/data/local/tmp/frida-server -D"'
frida-ps -U
```

Architecture mismatch:

```bash
adb shell uname -m
file frida-server
```

App crashes at launch:

```bash
adb logcat -d -t 400 | grep -E 'AndroidRuntime|FATAL|DEBUG|crash|tombstone|Frida|AppRuntime'
```

ShadowHook native scaffold skipped:

```bash
adb shell getprop ro.product.cpu.abi
adb logcat -d -s AppRuntime AppRuntime shadowhook_tag
```

If ABI is x86/x86_64, use Java hooks on emulator or switch to arm64 hardware/emulator for native inline hooks.
