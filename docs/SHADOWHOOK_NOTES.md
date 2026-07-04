# ShadowHook notes

This template uses ByteDance `android-inline-hook` / ShadowHook through Maven Prefab:

- Gradle dependency: `com.bytedance.android:shadowhook:2.0.1`
- CMake import: `find_package(shadowhook REQUIRED CONFIG)`
- Link target: `shadowhook::shadowhook`

> **Build requirement (important):** ShadowHook **2.0.1** ships an AAR whose metadata requires
> **`compileSdk ≥ 37`**, which in turn needs **AGP 9.x** (this template pins `agp = 9.2.1`,
> `compileSdk = 37`, Gradle `9.4.1`). If you downgrade ShadowHook to 2.0.0 you can drop back to
> AGP 8.7 / compileSdk 35. Mismatching these is the usual "template won't even build" cause.

## Byte-patch first, hook second (read this before reaching for ShadowHook)

For native/C++ game targets, **most cheats are a guarded byte-patch, not a hook** — "force this
bool true", "return this constant", "skip this check". Those are done with the patch engine +
auto-resolving `CodeFeature`s in `template_native.cpp` (no trampoline, nothing to detect, trivially
revertible). Reach for ShadowHook only when you must **intercept** — read/modify arguments or the
return value, run your own logic, or trace during RE. Full workflow: `NATIVE_MODDING_PLAYBOOK.md`.
For managed engines, prefer the engine API first (`ENGINE_IL2CPP.md`, `ENGINE_GODOT_LUA_COCOS.md`)
and hook the resolved native code pointer with `shadowhook_hook_func_addr`.

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

## Native smoke test in this template

`app/src/main/cpp/template_native.cpp` installs a safe smoke-test hook:

- target: `libc.so!getpid`
- behavior: increments an in-memory counter and calls the original function unchanged
- purpose: verify ShadowHook loading, symbol resolution, and call-chain behavior

Note it hooks by **`library + symbol name`** — that only works for **exported** symbols (libc).
A stripped game's own functions have no symbols, so a real hook targets a **resolved address**
(`shadowhook_hook_func_addr` on `base + rva` from the resolvers) — you don't need Frida for that:
the `tools/` static analyzers + `memtool` ("Frida without Frida") find the address, and the
`CodeFeature` resolvers re-derive it at runtime. Replace the smoke test with your real target.

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
- If you must hook by RVA, resolve the module base from `/proc/self/maps` after the target
  library is loaded, then install `base + rva`. Keep this code app-specific.
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
