# Java and native engine integration

The template provides scope/bootstrap, UI/settings, native library notifications, exported-symbol
lookup and optional IL2CPP metadata helpers. Engine detection is descriptive evidence. It never
selects a game hook, modifies a VM, or authorizes calling an API.

## Shared contracts

1. Identify the target package, process, ABI, original base/splits and current binary identity.
2. Wait for the owning runtime, not merely the Android Application. `LibraryObserver` handles
   already-loaded and delayed libraries without a periodic startup poll. Its load callback runs on
   a worker. Its retirement callback must only invalidate/signal; never call the linker or engine.
3. Use the real function prototype, thread affinity, object lifetime and error convention.
4. When a binding expects one target, check for a unique match.
5. Publish a feature only when its complete required binding group succeeds. Keep originals,
   handle failed installation/teardown, and test toggles and restarts in the actual lifecycle.

## Java/Kotlin and JNI

Bind application classes through the actual target classloader after the package/application is
ready. Match overload parameter and return types. `chain.proceed()` propagates original exceptions;
protective hook mode does not make native faults safe. Main/remote/isolated process selection must
be explicit. A missing process identity is not treated as the main process.

For obfuscated names that move between APK versions, use DexKit string/signature/call matchers
and validate a unique result on the actual APK. DexKit is an optional consuming-module dependency,
not a default scan or hidden startup cost in this template.

Never share `JNIEnv*` across threads. Retain a `JavaVM*`, attach only threads you own, manage
local/global references and pending exceptions, and release them at the correct lifecycle boundary.
See [Android JNI guidance](https://developer.android.com/ndk/guides/jni-tips).

## Native C/C++, including Cocos2d-x

Use `ModuleInfo`'s ELF load bias and separate readable/executable `PT_LOAD` ranges. An APK mapping
start is not a load bias, and the span between two segments can contain unmapped gaps. Symbol
lookup uses ShadowHook's maintained namespace-aware dynamic-symbol API. It queries already-loaded
images; it does not load a missing target. A duplicate basename requires an exact identity.

Validate C++ calling conventions, hidden struct returns, float registers, thunks, vtables and
`libc++` layouts against the current binary. Exact unwind-derived extents or validated metadata
are stronger than a prologue heuristic. A Cocos library can be named `libapp.so` and contain Lua
statically; neither its name nor a string constant establishes a usable engine interface.
Run scene work on the owning engine thread. Invalidate nodes/callbacks when scenes are replaced.
Do not keep a raw object solely because its address remains readable.

For runtime edits to executable instructions, account for concurrent execution, rollback and
instruction-cache coherence.

## Unity

`libunity.so` does not distinguish Mono from IL2CPP. The optional IL2CPP adapter requires its
actual configured library and validated exports. Use [the compiled IL2CPP helper](ENGINE_IL2CPP.md).
For Mono, use the matching embedding API and domain/GC lifecycle; IL2CPP handles and layouts are
not interchangeable with Mono. See [Mono embedding](https://www.mono-project.com/docs/advanced/embedding/).

## Lua / LuaJIT

Use headers/configuration matching the exact runtime. Symbols alone do not prove an ABI: Lua 5.2,
5.3, 5.4 and LuaJIT differ in continuations, numeric types, registry indices and return signatures.
For example, `lua_version` returns a pointer in Lua 5.2/5.3, but a number by value in Lua 5.4.
Calling the wrong shape is not a version detector. Numeric widths can also be configured.

A Lua state belongs to its owner thread. Preserve the stack top, use the correct protected-call
API, retain objects through registry references and invalidate the state on close/recreation.
Use the reference for the target's version. [Lua 5.4 C API](https://www.lua.org/manual/5.4/manual.html#4)

## Godot

Prefer the target version's supported extension API. Godot 3 GDNative and Godot 4 GDExtension are
not interchangeable. Obtain the matching interface header, initialization level, method signatures
and hashes from the target's API description. `Variant`/`StringName` construction and destruction,
precision options and object lifetime must match. A C++ mangled name does not establish these.

See [GDExtension](https://docs.godotengine.org/en/stable/engine_details/engine_api/gdextension/what_is_gdextension.html)
and the [versioned C example](https://docs.godotengine.org/en/4.6/tutorials/scripting/gdextension/gdextension_c_example.html).

## Unreal, Flutter and JavaScript runtimes

- Unreal object arrays, FName storage and reflection layouts are build-specific. Use matching SDK
  contracts, correct UObject rooting/weak handles and game-thread ownership. A raw pointer is not
  a GC root. See [Epic's pointer guidance](https://dev.epicgames.com/documentation/en-us/unreal-engine/object-pointers-in-unreal-engine).
- Flutter's Android embedding and platform views do not expose a universal stable Dart AOT object
  layout. Validate the Dart/engine version, isolate and thread before native interaction. An Android
  view hook cannot automatically see arbitrary Flutter widget state.
- Hermes/JSC libraries alone do not prove React Native. Their C/C++/JSI interfaces and garbage
  collection contracts are version-specific. Use the owning runtime's supported integration.

The overlay and settings work independently of engine detection. For engine-specific calls, use
the API and lifecycle of the runtime you integrate with.

For offline ELF inspection and address conversion, see the [host tools](../tools/README.md#static-analysis-host).
