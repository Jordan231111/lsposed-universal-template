# ShadowHook notes

This template uses ByteDance `android-inline-hook` / ShadowHook through Maven Prefab:

- Gradle dependency: `com.bytedance.android:shadowhook:2.0.0`
- CMake import: `find_package(shadowhook REQUIRED CONFIG)`
- Link target: `shadowhook::shadowhook`

## Why ShadowHook here

ShadowHook is actively maintained and currently has better Android-version coverage than older inline hook libraries that have gone stale. Useful features for LSPosed modules:

- hooks by address or `library name + symbol name`
- pending hook tasks when a library is not loaded yet
- callback after a pending hook resolves
- per-hook mode selection
- operation recording for debugging
- linker `.init`/`.fini` callbacks
- symbol lookup helpers that can bypass common linker namespace limitations

## ABI limitations

ShadowHook supports:

- `arm64-v8a`
- `armeabi-v7a`

It does **not** support:

- `x86`
- `x86_64`
- Houdini-translated ARM code on x86 Android

For an Android Studio emulator on an Intel Mac, native hooks with ShadowHook are usually not available because those AVDs are typically x86_64. Use Java hooks only there, or use an arm64 device. On Apple Silicon, choose an arm64 emulator image if available.

## Native scaffold in this recovery clone

`app/src/main/cpp/template_native.cpp` is app-specific for Rogue with the Dead `3.11.1` arm64.
It currently:

- initializes ShadowHook
- stores recovered feature state from Java before the IL2CPP wait begins
- waits for `libil2cpp.so`
- resolves the ELF load base from `/proc/self/maps` plus the executable `PT_LOAD` header
- hooks `il2cpp_init`, `il2cpp_init_utf16`, and `il2cpp_runtime_invoke` as a safe bootstrap
- after IL2CPP runtime init, resolves recovered gameplay, cloud-save integrity repair,
  backup-result forge, ACTk scaffolding, and game-speed hooks by IL2CPP metadata names
- resolves feature-critical managed field offsets from IL2CPP field metadata instead of keeping
  app-version-specific field constants in the hook logic
- applies saved game speed only from IL2CPP-safe callbacks, never directly from the early Java
  attach path

When porting this branch to another app or APK version, update the assembly/class/method/field
names only after Frida or static IL2CPP analysis confirms the same methods and ABI. Avoid
direct target RVAs for app feature hooks; they are expected to change between APK updates.

## Hook modes

Recommended defaults:

- `SHADOWHOOK_HOOK_WITH_SHARED_MODE` for general hooks and compatibility.
- `SHADOWHOOK_HOOK_WITH_MULTI_MODE` for hot paths after you have tested recursion behavior.
- `SHADOWHOOK_HOOK_WITH_UNIQUE_MODE` only when you intentionally require exclusive ownership of a hook point.

Shared mode proxy functions must use `SHADOWHOOK_STACK_SCOPE()` or `SHADOWHOOK_POP_STACK()` before returning.

## Practical stability checklist

- Keep native hooks minimal.
- Confirm ABI before enabling native hooks.
- Log hook return values and `shadowhook_get_errno()` while developing.
- Export operation records from the overlay/Java bridge when debugging.
- Prefer `library + symbol` for stable exported symbols.
- Prefer Frida reconnaissance before committing a permanent native hook.
- If you must hook by RVA for a temporary investigation, keep it out of the shipped feature path.
  Permanent Rogue feature hooks should resolve through symbols or IL2CPP metadata names.
- Do not install native hooks from `Application.attach` synchronously when the target library
  loads later in process startup. Use a short worker thread, a linker callback, or ShadowHook
  pending hooks and log the final install result.
- Gate every non-smoke-test hook behind a runtime feature flag. A module-level disable belongs
  in LSPosed/Vector; in-app switches should enable or disable individual behaviors.
- Avoid hooking extremely hot libc/pthread functions in production unless required and thoroughly profiled.
- Disable verbose ShadowHook logs in release builds.

## Detection hygiene for authorized testing

This template avoids obsolete hook libraries and keeps module scope explicit. For legitimate research, also consider:

- keep package/module names project-specific instead of obvious keywords
- reduce debug logs in release builds
- avoid broad global hooks
- keep hooks fail-closed and reversible
- avoid shipping unused native libraries
- document what you changed and why

Do not use this template to hide malicious behavior, bypass third-party protections outside an authorized lab, or violate app/platform terms.
