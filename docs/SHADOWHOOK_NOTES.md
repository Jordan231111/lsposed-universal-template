# ShadowHook integration

The template pins ShadowHook 2.0.1 through Maven Prefab and links `shadowhook::shadowhook`.
The supported native process ABIs are ARM32 and ARM64. API 102/Vector/LSPatch versions are separate
from the module's ShadowHook version. No demonstration hooks or byte patches are installed.

## Mode, ownership and lifetime

The Java and C++ bootstrap initialize the same SHARED mode. Shared proxies use
`SHADOWHOOK_STACK_SCOPE()` once at entry and `SHADOWHOOK_CALL_PREV` for call-through. Do not call
an `orig` trampoline directly from a shared proxy. Do not initialize a different global mode later.
See [the upstream manual](https://github.com/bytedance/android-inline-hook/blob/main/doc/manual.md).

Record every successful hook handle, check installation results, and keep effects inactive until
the entire required group is installed. If installation/rollback fails, report it and retain any
handle still requiring cleanup. Removing one hook does not undo arbitrary writes or other hooks.
Target code, arguments, proxies, captured objects and the original must all have valid lifetimes.
Native function-pointer equality can reveal aliases; several metadata methods can share one body.

A C++ proxy can be on a foreign exception path. Build cleanup frames with exception support, but
do not infer compatibility between every engine's bundled exception runtime and the module's.
Validate that boundary or provide a reviewed app-specific adapter. Managed runtime-invoke errors
are handled through the engine's exception result in the IL2CPP layer.

## Discovery and startup

`LibraryObserver` registers linker notifications before its initial loaded-image check. Its worker
sleeps without polling when the library is absent; no fixed timeout turns a delayed Activity launch
into a permanent failure. Loader callbacks only notify/invalidate; resolution runs on the worker.
The observer covers one image lifetime and must be stopped from its owner, not its callbacks.

`native_utils::resolve_export` uses ShadowHook/xDL's loaded-image dynamic-symbol API. A query handle
is released after lookup; this does not unload the target. There is no unbounded custom ELF hash
walker. Keep the target mapped while using returned addresses. Duplicate basenames require an
exact library path. Scan separate mapped ranges rather than a bounding span containing gaps.

## Scope and diagnosis

The optional IL2CPP observer installs only readiness/lifetime observers; it does not hook game
methods. Runtime acceptance, waiting and metadata readiness are distinct status states. Release
logging and operation recording are disabled by default; diagnostics remain available through
Status. No test counters, Frida agent, dump or disassembler database is embedded in the module.

The native startup setting does not unload native code. A consuming module must implement its own
feature gates and teardown. `autoHotReload=false` remains appropriate until that teardown exists.
