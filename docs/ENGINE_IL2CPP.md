# IL2CPP runtime contracts

The maintained implementation is `app/src/main/cpp/il2cpp_runtime.{h,cpp}`. The former copy/paste
bridge and RVA fallback examples have been removed. The module is engine-neutral by default.

## Initialization and lifetime

Opt in with `TemplateConfig.ENABLE_IL2CPP_RUNTIME=true`. Configure the actual library name through
`IL2CPP_LIBRARY_NAME`. The observer uses ShadowHook loader callbacks plus an initial loaded-image
check; a background-created process can wait without periodic polling or a fixed startup timeout.
A duplicate library basename is rejected. An exact runtime path or reviewed adapter is required
when several namespaces load different images with the same name.

Mapping a library and finding exports are not VM readiness. The observer waits until a real
`il2cpp_runtime_invoke` returns, validates core metadata on an attached worker, and removes that
transient hook. It deliberately does not guess `il2cpp_init`'s return ABI, which differs between
older and newer Unity APIs. The remaining shutdown/lifetime observers invalidate old bindings.
The status distinguishes waiting for the library, waiting for managed initialization, ready,
unavailable and retired.

`observed_runtime()` returns null until verified readiness. A retired runtime requires explicit
stop/start or a process restart. A consumer must quiesce its operations and release retained objects
before stopping/rebinding a live runtime. Mapping checks are snapshots, not protection against
arbitrary concurrent unload. Old class, field, method and calibration handles carry a lifetime
identity and cannot be reused after invalidation. Delayed readiness cannot resurrect a retired VM.

A custom integration can bind a `Runtime` to its own reviewed `Environment` (export lookup,
executable-address validation and bounded pointer reads). `bind` performs no metadata calls.
`confirm_initialized` requires actual initialization evidence; it is not a probe to try repeatedly.
No private API table, protected-app decoder, version whitelist or guessed symbol fallback is shipped.

## Metadata and fields

- Assemblies are explicit, with optional `.dll` suffixes. Searching all assemblies rejects ambiguity.
- Nested types support `/` or `+`. Missing classes are never cached permanently.
- Methods match the complete parameter list, return type and static/instance status. Inheritance
  cannot let an unrelated overload hide a base contract. Open generic methods need a constructed
  method; generic sharing is not treated as a normal native hook target.
- Instance field access uses exported field APIs, not copied object offsets. Primitive reads/writes
  check the actual ABI kind, preserving exact 64-bit values. Readonly/literal fields cannot be written.
- Managed references use GC roots and the reference setter/write barrier. Boxed values can be
  obtained through `read_object`; fields of boxed value types and static reference writes require
  a managed accessor rather than unverified pointer arithmetic.
- Static initialization uses a managed `RuntimeHelpers.RunClassConstructor` invocation so a type
  initializer exception is reported. If the required reflection surface is absent, the operation
  fails rather than calling an arbitrary initializer through an unchecked native ABI.

This read-only example is compiled by the documentation tests:

```cpp
#include "il2cpp_runtime.h"
#include <cstdint>

bool read_score(il2cpp::Runtime& runtime, const il2cpp::Root& entity, std::int64_t& score) {
    auto owner = runtime.class_of(entity);
    if (!owner) return false;
    auto field = runtime.find_field(owner.value, "Score", "System.Int64", false);
    if (!field) return false;
    auto value = runtime.read<std::int64_t>(field.value, entity.get());
    if (!value) return false;
    score = value.value;
    return true;
}
```

`Score` is an application contract to replace, not a universal field. Inspect each returned error.
A retained reference preserves memory; it does not preserve a disposed scene/controller's logical
validity. Resolve current scene objects through their owning application lifecycle.

## Managed calls and native hooks

`invoke` calls the exact selected concrete method through the runtime-invoke API and returns
rooted result/exception objects. It does not automatically dispatch a base virtual method to an
override; resolve the intended concrete implementation first. Abstract metadata is rejected. An
ordinary managed exception is not confused with successful null/void. Primitive values and byrefs
point to value storage; reference arguments contain the object pointer directly, **not `&object`**.
Use `unbox<T>` only for an exact primitive ABI. `string` and `string_value` preserve managed UTF-16.

A thread attachment does not move work to Unity's main thread. Metadata and suitable framework
operations can run on an attached worker. Unity/scene APIs require an application-validated
main-thread callback/dispatcher. No guessed universal frame hook or dispatcher is provided.
`ThreadScope` detaches only attachments it owns. Release handles before the runtime is retired.

Native addresses are a separate capability. `calibrate_method_pointer` uses two independent
framework delegates and their exported field APIs to discover agreeing pointer slots. The 128-byte
search budget is a bound, not a private structure definition. `code_pointer` checks readable
metadata, executable targets, slot agreement and the calibration lifetime. `unique_hook_targets`
rejects duplicate selected code addresses. Missing reflection delegates disable this capability.

No copied `MethodInfo`, `Il2CppClass` or object layout is required. Native value-type receivers,
struct argument/return ABI and generic shared code require explicit adapters; managed invocation
is a separate path. A resolved address does not validate the consuming proxy's C++ prototype,
virtual-dispatch choice, hidden arguments, exception unwinding or application semantics.
Keep the metadata argument when the reviewed native ABI requires it.

Use ShadowHook shared-mode proxies consistently: one `SHADOWHOOK_STACK_SCOPE()` at entry and
`SHADOWHOOK_CALL_PREV` to call through. Do not mix an already-initialized shared engine with a
unique-mode example. Foreign C++ exception runtimes can need an app-specific unwind adapter;
a successful primitive hook does not establish that exception path.

## Portability and verification

The code supports the public exported C API and ARM calling conventions. Older unsigned 32-bit
GC handles are zero-extended in the pointer-sized ARM slot; newer pointer-sized handles are retained
without truncation. This is not a claim about arbitrary desktop ABIs or customized API tables.

A renamed member, removed export, missing AOT implementation, changed struct representation or
protected/custom runtime can require a new adapter. Metadata identity is evidence, not a promise
that an arbitrary future application behaves the same way. Runtime-invoke expects a callable
implementation; choose concrete methods and validate their presence before invoking app code.

Host sanitizer fixtures cover late registration, changed layouts/signatures, ambiguous resolution,
primitive/reference fields, exceptions, thread/root ownership and stale lifetimes. External device
checks compile this same implementation and invoke framework-only operations. See
[validation](VALIDATION.md) and [test commands](../tests/README.md) for actual coverage.
