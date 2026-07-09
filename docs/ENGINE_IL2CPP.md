# Engine: Unity IL2CPP runtime resolution and hooking

Purpose: resolve and hook Unity IL2CPP classes / methods / fields **by name at runtime**,
from an Android native module (LSPosed / Zygisk), **without** a pre-dumped per-version offset
file. This is the dynamic counterpart to the static workflow in
[`ENGINE_NATIVE_WORKFLOWS.md`](ENGINE_NATIVE_WORKFLOWS.md); hooking uses ShadowHook as set up in
[`SHADOWHOOK_NOTES.md`](SHADOWHOOK_NOTES.md).

Scope: **arm64-v8a first** (armeabi-v7a works the same way). Authorized research/testing only.
Keep any per-version fallback artifacts on an app branch, never on `main`.

---

## 0. Strategy in one paragraph (read this first)

The single most important fact: **for the large majority of Unity games, the `il2cpp_*` C API is
still exported from `libil2cpp.so`.** `il2cpp_class_get_method_from_name()` returns a
`MethodInfo*`, and the **first field of `MethodInfo` is `methodPointer`** — the actual native
code address — in *every* metadata version. So "hook a managed method by name" reduces to:

1. `dlopen("libil2cpp.so", RTLD_NOLOAD)` + `dlsym` the handful of exports we need.
2. `il2cpp_class_from_name` → `il2cpp_class_get_method_from_name` → read `*(void**)methodInfo`.
3. `shadowhook_hook_func_addr(methodPointer, proxy, &orig)`.

No global-metadata parsing, no CodeRegistration walking, **no offset file**. This is Tier A and
it is what the reusable helper below implements. Tier B (parsing metadata yourself) is only
needed when a protected game **strips or renames the exports**, and it is honestly a per-version
effort — see §2 for what actually works and what does not.

Two-tier design:

- **Tier A — exports present (default, version-tolerant, "just works by name").**
  Resolve everything through the exported API. Works Unity 2019 → 2023+ unchanged because the
  export *contract* is stable even as the metadata format churns.
- **Tier B — exports stripped/renamed (protected games).** Fall back to a one-time host-side
  dump reduced to a **minimal name→RVA table** loaded at runtime as `base + rva`, or to an
  in-process metadata parser. Be honest: fully "no-dump, no-offset" against a hardened target is
  not reliably version-tolerant.

---

## 1. Tier A — find `libil2cpp.so` and resolve the export API

### 1.1 Find the base / confirm the module is mapped

Two equivalent ways. Prefer `dl_iterate_phdr` (no file parsing, gives you `dlpi_addr` = load
bias and the full path); fall back to `/proc/self/maps` for logging/diagnostics.

```cpp
#include <link.h>     // dl_iterate_phdr, dl_phdr_info
#include <dlfcn.h>    // dlopen, dlsym, RTLD_NOLOAD
#include <string.h>
#include <stdint.h>

struct Il2CppModule { uintptr_t base = 0; char path[512] = {0}; };

static int phdr_cb(struct dl_phdr_info* info, size_t, void* data) {
    if (!info->dlpi_name) return 0;
    if (strstr(info->dlpi_name, "libil2cpp.so")) {
        auto* out = static_cast<Il2CppModule*>(data);
        out->base = static_cast<uintptr_t>(info->dlpi_addr);   // ELF load bias
        strncpy(out->path, info->dlpi_name, sizeof(out->path) - 1);
        return 1; // stop
    }
    return 0;
}

// Returns true and fills `out` if libil2cpp.so is currently mapped in this process.
static bool find_il2cpp_module(Il2CppModule* out) {
    return dl_iterate_phdr(phdr_cb, out) == 1 && out->base != 0;
}
```

Note on `dlpi_addr`: it is the **load bias**, not necessarily the address of the ELF header.
For `base + rva` math (Tier B) that is exactly what you want, because Il2CppDumper RVAs are file
offsets relative to the preferred load address (0), so runtime `addr = dlpi_addr + rva`. For
Tier A you do not need the base at all — `dlsym` gives you absolute addresses.

### 1.2 Resolve the exports with `dlopen(RTLD_NOLOAD)` + `dlsym`

`RTLD_NOLOAD` returns a handle **only if the library is already loaded** (it will be, in the
game process) without loading it again. In LSPosed/Zygisk, the plain soname sometimes fails due
to linker-namespace isolation; fall back to the **full path** from `dl_iterate_phdr`.

```cpp
static void* open_il2cpp() {
    void* h = dlopen("libil2cpp.so", RTLD_NOLOAD | RTLD_NOW);
    if (h) return h;
    Il2CppModule m;
    if (find_il2cpp_module(&m) && m.path[0])
        h = dlopen(m.path, RTLD_NOLOAD | RTLD_NOW);   // namespace-safe: full path
    return h; // may still be null if hidden by the linker namespace
}
```

If both fail (some hardened loaders hide the handle), you can still call exports by resolving
their addresses manually from the ELF `.dynsym` you get via the base — but if names are exported
at all, `dlsym` on the full-path handle almost always works.

### 1.2b Namespace-proof fallback: parse `.dynsym` yourself (LSPatch / isolated namespaces)

Under **LSPatch (non-root) and any isolated linker namespace**, `dlopen` can return `null` **even
with the full path** — the linker deliberately hides `libil2cpp.so` from your namespace, so there
is no handle and `dlsym` has nothing to query. The module is still **mapped and readable** in the
process, though; the linker only withholds the *handle*, not the memory. So resolve exports by
walking the module's in-memory dynamic symbol table (`.dynsym`) directly, keyed by **name** (not a
per-version offset). This template ships exactly that helper —
`native_utils::resolve_export(base, "il2cpp_domain_get")` (see
[`app/src/main/cpp/native_utils.cpp`](../app/src/main/cpp/native_utils.cpp)) — which is
hash-table-independent and needs only the load base from `find_module_info()`:

```cpp
#include "native_utils.h"

// Namespace-proof bind: try dlsym first (fast path), fall back to .dynsym parsing.
static void* il2cpp_export(void* h, uintptr_t base, const char* name) {
    if (h) { if (void* p = dlsym(h, name)) return p; }
    return reinterpret_cast<void*>(native_utils::resolve_export(base, name));  // 0 -> nullptr
}

// Usage inside il2cpp_bind(): get the base once, then resolve every symbol by name.
native_utils::ModuleInfo mi = native_utils::find_module_info("libil2cpp.so");
void* h = open_il2cpp();  // may be null under LSPatch — that's fine
g_api.domain_get = reinterpret_cast<t_domain_get>(
        il2cpp_export(h, mi.base, "il2cpp_domain_get"));
// ... repeat for the rest; drop the BIND() dlsym-only macro in §1.4 for this on hidden namespaces.
```

Because `resolve_export` matches by symbol name, it stays version-tolerant just like the `dlsym`
path — it only replaces *how* you look the name up, not the Tier A design. It cannot recover
symbols that were genuinely **stripped or renamed**; that is still Tier B (§2).

### 1.3 Correct export signatures (verified)

Opaque forward types (we never need their layout for Tier A except `MethodInfo`'s first field):

```cpp
struct Il2CppDomain;   struct Il2CppAssembly; struct Il2CppImage;
struct Il2CppClass;    struct Il2CppObject;   struct Il2CppString;
struct Il2CppThread;   struct Il2CppException;
struct FieldInfo;      struct MethodInfo;     // MethodInfo: field[0] == methodPointer
```

Signatures below are copied from `libil2cpp/il2cpp-api-functions.h` (the `DO_API(ret, name,
(args))` macro), which is the authoritative per-version header Unity ships. These are stable
across Unity 2019–2023 for the functions we use:

```cpp
// domain / assembly / image enumeration
typedef Il2CppDomain*          (*t_domain_get)();
typedef const Il2CppAssembly** (*t_domain_get_assemblies)(const Il2CppDomain*, size_t* size);
typedef const Il2CppImage*     (*t_assembly_get_image)(const Il2CppAssembly*);
typedef const char*            (*t_image_get_name)(const Il2CppImage*);
typedef size_t                 (*t_image_get_class_count)(const Il2CppImage*);
typedef const Il2CppClass*     (*t_image_get_class)(const Il2CppImage*, size_t index);

// name resolution
typedef Il2CppClass*   (*t_class_from_name)(const Il2CppImage*, const char* ns, const char* name);
typedef const MethodInfo* (*t_class_get_method_from_name)(Il2CppClass*, const char* name, int argc);
typedef FieldInfo*     (*t_class_get_field_from_name)(Il2CppClass*, const char* name);
typedef const char*    (*t_class_get_name)(Il2CppClass*);
typedef const char*    (*t_class_get_namespace)(Il2CppClass*);
typedef const MethodInfo* (*t_class_get_methods)(Il2CppClass*, void** iter);
typedef const char*    (*t_method_get_name)(const MethodInfo*);

// fields
typedef size_t (*t_field_get_offset)(FieldInfo*);
typedef void   (*t_field_get_value)(Il2CppObject* obj, FieldInfo*, void* out);
typedef void   (*t_field_set_value)(Il2CppObject* obj, FieldInfo*, void* val);
typedef void   (*t_field_static_get_value)(FieldInfo*, void* out);
typedef void   (*t_field_static_set_value)(FieldInfo*, void* val);

// invoke / objects / strings
typedef Il2CppObject* (*t_runtime_invoke)(const MethodInfo*, void* obj, void** params, Il2CppException** exc);
typedef Il2CppObject* (*t_object_new)(const Il2CppClass*);
typedef Il2CppString* (*t_string_new)(const char* str);

// threading / gc
typedef Il2CppThread* (*t_thread_attach)(Il2CppDomain*);
typedef void          (*t_thread_detach)(Il2CppThread*);
typedef void          (*t_gc_disable)();
typedef void          (*t_gc_enable)();
```

> Version caveats worth knowing (all documented by Il2CppInspector/Il2CppDumper):
> - Very old Unity used `TypeInfo*` where modern headers use `Il2CppClass*` — same pointer,
>   only the typedef name changed, so our `void*`-based helper is unaffected.
> - Protected games sometimes **encrypt the export names** or replace exports with **do-nothing
>   stubs**. If `dlsym` succeeds but calls behave impossibly (null classes for names you know
>   exist), assume stubbed/encrypted exports and go to Tier B.

### 1.4 A tiny loader that binds all of them

```cpp
struct Il2CppApi {
    t_domain_get                domain_get;
    t_domain_get_assemblies     domain_get_assemblies;
    t_assembly_get_image        assembly_get_image;
    t_image_get_name            image_get_name;
    t_class_from_name           class_from_name;
    t_class_get_method_from_name class_get_method_from_name;
    t_class_get_field_from_name field_from_name;
    t_field_get_offset          field_get_offset;
    t_field_static_get_value    static_get_value;
    t_field_static_set_value    static_set_value;
    t_runtime_invoke            runtime_invoke;
    t_object_new                object_new;
    t_string_new                string_new;
    t_thread_attach             thread_attach;
    t_thread_detach             thread_detach;
    t_gc_disable                gc_disable;
    t_gc_enable                 gc_enable;
    bool ok = false;
};

static Il2CppApi g_api;

#define BIND(field, sym) \
    g_api.field = reinterpret_cast<decltype(g_api.field)>(dlsym(h, sym)); \
    if (!g_api.field) { /* log(#sym " missing"); */ all = false; }

static bool il2cpp_bind() {
    if (g_api.ok) return true;
    void* h = open_il2cpp();
    if (!h) return false;
    bool all = true;
    BIND(domain_get,                "il2cpp_domain_get");
    BIND(domain_get_assemblies,     "il2cpp_domain_get_assemblies");
    BIND(assembly_get_image,        "il2cpp_assembly_get_image");
    BIND(image_get_name,            "il2cpp_image_get_name");
    BIND(class_from_name,           "il2cpp_class_from_name");
    BIND(class_get_method_from_name,"il2cpp_class_get_method_from_name");
    BIND(field_from_name,           "il2cpp_class_get_field_from_name");
    BIND(field_get_offset,          "il2cpp_field_get_offset");
    BIND(static_get_value,          "il2cpp_field_static_get_value");
    BIND(static_set_value,          "il2cpp_field_static_set_value");
    BIND(runtime_invoke,            "il2cpp_runtime_invoke");
    BIND(object_new,                "il2cpp_object_new");
    BIND(string_new,                "il2cpp_string_new");
    BIND(thread_attach,             "il2cpp_thread_attach");
    BIND(thread_detach,             "il2cpp_thread_detach");
    BIND(gc_disable,                "il2cpp_gc_disable");
    BIND(gc_enable,                 "il2cpp_gc_enable");
    // The three we can hard-require; the rest are optional niceties.
    g_api.ok = g_api.domain_get && g_api.class_from_name &&
               g_api.class_get_method_from_name;
    return g_api.ok;
}
```

### 1.5 Read an instance field by name (offset math)

`il2cpp_field_get_offset` returns the byte offset **from the start of the managed object**
(i.e. it already includes the two-pointer `Il2CppObject` header). So an instance field lives at
`(uint8_t*)instance + offset`:

```cpp
// int32 example; swap the cast for float/void*/etc.
static bool read_instance_i32(void* instance, Il2CppClass* klass,
                              const char* field, int32_t* out) {
    if (!instance || !klass) return false;
    FieldInfo* f = g_api.field_from_name(klass, field);
    if (!f) return false;
    size_t off = g_api.field_get_offset(f);
    *out = *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(instance) + off);
    return true;
}
static bool write_instance_i32(void* instance, Il2CppClass* klass,
                               const char* field, int32_t v) {
    if (!instance || !klass) return false;
    FieldInfo* f = g_api.field_from_name(klass, field);
    if (!f) return false;
    size_t off = g_api.field_get_offset(f);
    *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(instance) + off) = v;
    return true;
}
```

### 1.6 Read/write a static field, and call a method by name

Static storage is not at a fixed object address, so use the dedicated API (it also forces the
right per-class static storage). Note the class static constructor may not have run yet, so a
very early read can return defaults.

```cpp
static bool read_static_i32(Il2CppClass* klass, const char* field, int32_t* out) {
    FieldInfo* f = g_api.field_from_name(klass, field);
    if (!f) return false;
    g_api.static_get_value(f, out);   // out is &int32
    return true;
}

// Call a managed method by name via the reflection-safe path.
// `obj` = instance pointer (null for static). `params` = array of arg pointers:
//   value types -> pointer to the value; reference types -> the object pointer itself.
static Il2CppObject* invoke_by_name(Il2CppClass* klass, const char* method, int argc,
                                    void* obj, void** params) {
    const MethodInfo* m = g_api.class_get_method_from_name(klass, method, argc);
    if (!m) return nullptr;
    Il2CppException* exc = nullptr;
    Il2CppObject* ret = g_api.runtime_invoke(m, obj, params, &exc);
    // if (exc) { /* log/handle managed exception */ }
    return ret; // boxed result for value types; null for void
}
```

`il2cpp_runtime_invoke` handles the hidden trailing `MethodInfo*` argument and value-type
boxing for you. Prefer it for **calling**. For **hooking** you work with the raw
`methodPointer` instead (§3), where you must account for that trailing argument yourself.

---

## 2. Tier B — the honest fallback when exports are stripped/renamed

If `il2cpp_class_from_name` etc. are missing, stubbed, or the names are encrypted, you must
recover `NAME → methodPointer` some other way. This is exactly what Il2CppDumper /
Il2CppInspector do offline. Here is what is real:

### 2.1 What the tools actually parse

1. **`global-metadata.dat`** — starts with magic **`0xFAB11BAF`** (little-endian bytes
   `AF 1B B1 FA`) at offset 0, then an `int32` **version** at offset 4, then a long run of
   `(offset, size)` `int32` pairs describing tables (string literals, type definitions, method
   definitions, images, assemblies, …). Version→Unity (approximate; confirm per target):
   - **24** (with community sub-variants 24.0–24.5): Unity 2018.3 – 2019.x
   - **27 / 27.1 / 27.2**: Unity 2020.x
   - **28 / 29**: Unity 2021.x
   - **31**: Unity 2022.3+ / 2023.x
   The header struct **layout differs per version** (fields added/reordered), which is the core
   reason a single hardcoded parser is not version-tolerant. The runtime validates
   `header->sanity == 0xFAB11BAF` in `MetadataCache::Initialize`.

2. **`CodeRegistration` + `MetadataRegistration`** in the binary. These are the two structs
   passed to `il2cpp::vm::MetadataCache::Register`. `CodeRegistration` holds `codeGenModules`
   → per-assembly `Il2CppCodeGenModule` → a `methodPointers[]` array. A method's metadata
   **token** indexes that array to get its compiled native address. That is the whole
   `NAME → definition → token → methodPointer` chain.

   Locating them without symbols (protected `.so`): Il2CppDumper's **Auto(Plus)** matches
   candidate pointers in the data section cross-checked against metadata; **Auto(Symbol)** uses
   symbols if present; **Manual** traces `il2cpp_init → Runtime::Init → il2cpp_codegen_register`
   (arg0 = CodeRegistration, arg1 = MetadataRegistration). A commonly cited arm64 anchor near
   the register call is the byte pattern `88 12 40 F9 00 01 3F D6 88 06 40 F9` (xref from there).

### 2.2 Can you do it fully in-process with no dump and no offsets?

Honestly: **partially, and not robustly against hardened targets.** You would embed
Il2CppDumper's logic natively — read `global-metadata.dat` (from
`assets/bin/Data/Managed/Metadata/` or from mapped memory), parse the header **for each version
you choose to support**, then locate CodeRegistration/MetadataRegistration by pattern scan and
walk `codeGenModules[image].methodPointers[token]`. Limits you must accept:

- **Version fragility.** Every new metadata version can move header fields; you maintain a
  per-version parser (this is why Il2CppDumper carries version-specific structs).
- **Encryption/obfuscation.** Many protected games encrypt `global-metadata.dat` and/or the
  registration structs, or decrypt them only in memory behind custom loaders. A generic parser
  cannot defeat these; that is out of scope for a template.
- **Pattern brittleness.** Register-call byte patterns are compiler/version specific.

### 2.3 The pragmatic, honest recommendation: a minimal one-time host artifact

When exports are gone, do a **one-time host-side dump per game version** and reduce it to the
**smallest possible runtime artifact**: a flat `NAME → RVA` table. At runtime resolve
`addr = dlpi_addr + rva` (§1.1). This is per-version and therefore belongs on an **app branch**,
never `main` (consistent with [`ENGINE_NATIVE_WORKFLOWS.md`](ENGINE_NATIVE_WORKFLOWS.md)).

Minimal artifact = a generated header, e.g. from Il2CppDumper's `script.json`:

```cpp
// il2cpp_offsets.gen.h  (APP BRANCH ONLY — regenerate per game version)
struct Il2CppOffset { const char* key; uintptr_t rva; };
static const Il2CppOffset kOffsets[] = {
    {"PlayerController::Update",   0x01A2B4C0},
    {"WeaponSystem::get_ammo",     0x01A2C310},
    // ...
};
```

```cpp
// Runtime resolve: base + rva. Works even with zero il2cpp exports.
static void* resolve_rva(const char* key) {
    Il2CppModule m;
    if (!find_il2cpp_module(&m)) return nullptr;
    for (const auto& e : kOffsets)
        if (strcmp(e.key, key) == 0) return (void*)(m.base + e.rva);
    return nullptr;
}
```

This trades "version-tolerant by name" for "one cheap, automatable regeneration step per
version." For protected games that is the realistic state of the art; do not promise more.

---

## 3. Hooking a managed method with ShadowHook

Once you have the native `methodPointer` (Tier A: `*(void**)MethodInfo`; Tier B: `base + rva`),
hook it exactly like any native function. ShadowHook is already wired into this template
(`com.bytedance.android:shadowhook:2.0.0`, `shadowhook::shadowhook`; see
[`SHADOWHOOK_NOTES.md`](SHADOWHOOK_NOTES.md)).

### 3.1 The `MethodInfo` first-field trick (version-tolerant)

`methodPointer` is the **first** member of `MethodInfo` in every metadata version (in v27+ a
`virtualMethodPointer` was inserted *after* it, so field[0] is unchanged). Therefore:

```cpp
// Robust across Unity versions: no struct definition required.
static inline void* method_code(const MethodInfo* mi) {
    return mi ? *reinterpret_cast<void* const*>(mi) : nullptr;
}
```

### 3.2 Proxy signature (critical correctness detail)

The **compiled** IL2CPP function is not the C# signature. Its native shape is:

- instance method `T Foo(A a)` → `T foo(Il2CppObject* self, A a, const MethodInfo* method)`
- static  method `T Bar(A a)` → `T bar(A a, const MethodInfo* method)`

i.e. `this` is prepended for instance methods, and a hidden trailing `const MethodInfo*` is
**always** appended. Your proxy must match this or the stack/registers will be wrong. Value
types may pass in registers, on the stack, or via a hidden return buffer — verify the AArch64
ABI at the site (same warning as the static workflow doc).

### 3.3 Install the hook

```cpp
#include "shadowhook.h"

// Example: hook  int Player::get_health()  (instance getter, 0 args)
static void*  g_orig_get_health = nullptr;   // receives original for chaining
static void*  g_stub_get_health = nullptr;   // unhook handle

// proxy: instance getter -> (self, MethodInfo*)
static int32_t proxy_get_health(void* self, const void* method) {
    // UNIQUE mode: call original directly through the returned orig pointer.
    typedef int32_t (*fn)(void*, const void*);
    int32_t real = reinterpret_cast<fn>(g_orig_get_health)(self, method);
    return real < 100 ? 100 : real;   // example: floor health at 100
}

static bool install_get_health_hook(const MethodInfo* mi) {
    void* code = method_code(mi);
    if (!code) return false;
    g_stub_get_health = shadowhook_hook_func_addr(code, (void*)proxy_get_health,
                                                  &g_orig_get_health);
    // if (!g_stub_get_health) log(shadowhook_to_errmsg(shadowhook_get_errno()));
    return g_stub_get_health != nullptr;
}
```

`shadowhook_hook_func_addr(void* func_addr, void* new_addr, void** orig_addr)` is the right call
here — the target has no usable symbol, only an address. Initialize ShadowHook once:
`shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false)` (UNIQUE = single owner of each hook point,
simplest `orig` call). If you use **SHARED** mode instead, the proxy must call the original via
`SHADOWHOOK_CALL_PREV(proxy_get_health, self, method)` and end with `SHADOWHOOK_STACK_SCOPE()` —
see [`SHADOWHOOK_NOTES.md`](SHADOWHOOK_NOTES.md). Unhook with `shadowhook_unhook(stub)`.

### 3.4 Thread attach + GC considerations

- **Proxy threads are already attached.** When your proxy fires, you are on a thread IL2CPP
  itself called, so it is attached and it is safe to touch managed state.
- **Your own threads are not.** If you spawn a worker (menu thread, poller) and want to call
  `il2cpp_runtime_invoke`, allocate objects, or read managed memory, you **must**
  `il2cpp_thread_attach(il2cpp_domain_get())` once on that thread and
  `il2cpp_thread_detach` before it exits. Attach once per thread, not per call.
- **GC.** Default IL2CPP uses Boehm GC (conservative, **non-moving**), so raw object addresses
  are stable — but an object can still be **collected** if nothing references it. Keep a live
  managed reference (or a GC handle) for anything you cache across frames; never stash a bare
  `Il2CppObject*` from one call and dereference it many frames later. Wrap short, sensitive
  sequences (multi-step reads that must be consistent) in `il2cpp_gc_disable()` /
  `il2cpp_gc_enable()`, and keep that window tiny — disabling GC for long leaks memory and can
  trip anti-cheat heuristics.

### 3.5 Runtime readiness — do NOT call the il2cpp API before `il2cpp_init` finishes

This is the single most common early-init crash. An LSPosed/LSPatch module gets control at
`Application.attach`, and a worker thread that resolves offsets typically starts there — **long
before** Unity has called `il2cpp_init()`. Calling **any** `il2cpp_*` function (even
`il2cpp_domain_get`) before init completes segfaults deep inside `libil2cpp.so` (fault addr is a
small value like `0x135`), because the runtime's globals/TLS aren't set up yet. Waiting in a loop
that *calls* `il2cpp_domain_get()` doesn't help — the call itself is what crashes.

The reliable gate: `il2cpp_init` is an **exported** symbol, so hook it (you can `resolve_export`
it before the runtime is up) and only touch the API after it returns.

```cpp
static std::atomic<bool> g_rt_ready{false};
static void* g_orig_init = nullptr;
static void* il2cpp_init_proxy(const char* name) {
    SHADOWHOOK_STACK_SCOPE();
    void* r = SHADOWHOOK_CALL_PREV(il2cpp_init_proxy, name);
    g_rt_ready.store(true, std::memory_order_release);   // runtime is now up
    return r;
}
// the instant libil2cpp is mapped (before Unity calls init):
if (void* f = (void*)native_utils::resolve_export(base, "il2cpp_init"))
    shadowhook_hook_func_addr(f, (void*)il2cpp_init_proxy, &g_orig_init);
for (int i = 0; i < 1200 && !g_rt_ready.load(std::memory_order_acquire); ++i) usleep(25000);
// only now: attach this worker thread, poll find_class(...) until a known class resolves
// (assemblies register slightly after init), then resolve everything by name.
```

Order that works: **map libil2cpp → hook `il2cpp_init` → wait flag → `il2cpp_thread_attach` this
worker → poll `find_class("<known class>")` until non-null → resolve all names → install hooks.**
Because the gate lands right after `il2cpp_init` (still before any scene `MonoBehaviour.Start`),
launch-gate/anti-cheat hooks resolved here are in time.

### 3.6 Overloaded methods: `il2cpp_class_get_method_from_name` matches name + argc only

It returns the *first* method with that name and parameter **count**, so overloads that share both
are ambiguous (`SceneManager.LoadScene(string)` vs `(int)`; a conversion operator's two
`op_Implicit(x)` directions). Disambiguate by the first parameter's type: iterate
`il2cpp_class_get_methods(klass, &iter)`, match `il2cpp_method_get_name` + `param_count`, then check
`il2cpp_type_get_name(il2cpp_method_get_param(m, 0))` contains e.g. `"String"` / `"Int32"`. Free the
returned type-name string with `il2cpp_free`.

---

## 4. Reusable C++ API surface (drop-in helper)

A compact, engine-agnostic surface a template exposes. `asmName`/`ns` may be `nullptr` to search
all assemblies / the global namespace. All lookups are cached.

```cpp
// il2cpp_bridge.h
#pragma once
#include <stdint.h>

namespace il2 {
    bool  init();                       // bind exports (Tier A). false => try Tier B.
    bool  ready();

    // Resolution (return opaque il2cpp pointers as void*)
    void* find_class (const char* asmName, const char* ns, const char* klass);
    void* find_method(const char* asmName, const char* ns, const char* klass,
                      const char* method, int argc);           // -> native methodPointer
    const void* find_method_info(const char* asmName, const char* ns, const char* klass,
                                 const char* method, int argc); // -> MethodInfo*
    int32_t field_offset(void* klass, const char* field);      // -1 on failure

    // Typed field access by name
    bool read_field_int (void* instance, const char* asmName, const char* ns,
                         const char* klass, const char* field, int32_t* out);
    bool write_field_int(void* instance, const char* asmName, const char* ns,
                         const char* klass, const char* field, int32_t v);
    bool read_static_int(const char* asmName, const char* ns, const char* klass,
                         const char* field, int32_t* out);

    // Hook a managed method by name. Returns ShadowHook stub (or nullptr).
    void* hook_method(const char* asmName, const char* ns, const char* klass,
                      const char* method, int argc, void* proxy, void** orig);

    // Threading (call on your own threads only)
    void attach_current_thread();
    void detach_current_thread();
}
```

```cpp
// il2cpp_bridge.cpp  — depends on §1 (g_api, il2cpp_bind, method_code) + shadowhook.
#include "il2cpp_bridge.h"
#include "shadowhook.h"
#include <string.h>
#include <unordered_map>
#include <string>

// ---- assembly/image name match (tolerate optional ".dll") --------------------
static bool image_name_matches(const char* imgName, const char* want) {
    if (!want) return true;                     // null => any assembly
    if (!imgName) return false;
    size_t n = strlen(imgName);
    if (n >= 4 && strcasecmp(imgName + n - 4, ".dll") == 0) n -= 4; // strip .dll
    return strncasecmp(imgName, want, n) == 0 && want[n] == '\0';
}

namespace il2 {

bool init()  { return il2cpp_bind(); }
bool ready() { return g_api.ok; }

void* find_class(const char* asmName, const char* ns, const char* klass) {
    if (!ready()) return nullptr;
    static std::unordered_map<std::string, void*> cache;
    std::string key = std::string(asmName ? asmName : "*") + "|" +
                      (ns ? ns : "") + "|" + klass;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    Il2CppDomain* dom = g_api.domain_get();
    size_t n = 0;
    const Il2CppAssembly** as = g_api.domain_get_assemblies(dom, &n);
    void* found = nullptr;
    for (size_t i = 0; i < n && !found; ++i) {
        const Il2CppImage* img = g_api.assembly_get_image(as[i]);
        if (asmName && g_api.image_get_name &&
            !image_name_matches(g_api.image_get_name(img), asmName)) continue;
        found = g_api.class_from_name(img, ns ? ns : "", klass);
    }
    cache[key] = found;
    return found;
}

const void* find_method_info(const char* asmName, const char* ns, const char* klass,
                             const char* method, int argc) {
    Il2CppClass* c = static_cast<Il2CppClass*>(find_class(asmName, ns, klass));
    if (!c) return nullptr;
    return g_api.class_get_method_from_name(c, method, argc);   // argc = -1 not valid; give real count
}

void* find_method(const char* asmName, const char* ns, const char* klass,
                  const char* method, int argc) {
    return method_code(static_cast<const MethodInfo*>(
        find_method_info(asmName, ns, klass, method, argc)));
}

int32_t field_offset(void* klass, const char* field) {
    if (!ready() || !klass) return -1;
    FieldInfo* f = g_api.field_from_name(static_cast<Il2CppClass*>(klass), field);
    return f ? static_cast<int32_t>(g_api.field_get_offset(f)) : -1;
}

bool read_field_int(void* instance, const char* asmName, const char* ns,
                    const char* klass, const char* field, int32_t* out) {
    if (!instance || !out) return false;
    int32_t off = field_offset(find_class(asmName, ns, klass), field);
    if (off < 0) return false;
    *out = *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(instance) + off);
    return true;
}

bool write_field_int(void* instance, const char* asmName, const char* ns,
                     const char* klass, const char* field, int32_t v) {
    if (!instance) return false;
    int32_t off = field_offset(find_class(asmName, ns, klass), field);
    if (off < 0) return false;
    *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(instance) + off) = v;
    return true;
}

bool read_static_int(const char* asmName, const char* ns, const char* klass,
                     const char* field, int32_t* out) {
    if (!ready() || !out) return false;
    Il2CppClass* c = static_cast<Il2CppClass*>(find_class(asmName, ns, klass));
    if (!c) return false;
    FieldInfo* f = g_api.field_from_name(c, field);
    if (!f) return false;
    g_api.static_get_value(f, out);
    return true;
}

void* hook_method(const char* asmName, const char* ns, const char* klass,
                  const char* method, int argc, void* proxy, void** orig) {
    void* code = find_method(asmName, ns, klass, method, argc);
    if (!code || !proxy) return nullptr;
    return shadowhook_hook_func_addr(code, proxy, orig);
}

static thread_local Il2CppThread* t_attached = nullptr;
void attach_current_thread() {
    if (ready() && !t_attached) t_attached = g_api.thread_attach(g_api.domain_get());
}
void detach_current_thread() {
    if (t_attached) { g_api.thread_detach(t_attached); t_attached = nullptr; }
}

} // namespace il2
```

Known limitations of this surface (intentional, keep them documented):

- **Generics and nested types**: `il2cpp_class_from_name` does not resolve constructed generics
  (`List<int>`) and needs `il2cpp_class_get_nested_types` for `Outer/Inner`. Add helpers per app
  if needed; not part of the version-tolerant core.
- **Overloads**: `argc` disambiguates by arity only. Two same-arity overloads need iterating
  `il2cpp_class_get_methods` and checking parameter types.
- `read_field_int` uses class-level offset lookup; for boxed/valuetype fields inside structs the
  offset is still object-relative, which is what you want for a heap instance.

---

## 5. How to use it from the module

Native side (`app/src/main/cpp/template_native.cpp` is where the smoke hook already lives):

```cpp
// Called after you have confirmed libil2cpp.so is mapped. IL2CPP libs often load AFTER
// Application.attach — poll on a bounded worker or use a ShadowHook pending/linker callback
// (see ENGINE_NATIVE_WORKFLOWS.md step 2). Do NOT block startup.
static void on_il2cpp_ready() {
    shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);   // once, process-wide
    if (!il2::init()) {
        // Tier A unavailable (stripped/encrypted exports) -> use Tier B RVA table on app branch.
        return;
    }

    // Read a field by name:
    int32_t hp = 0;
    // il2::read_field_int(playerInstance, "Assembly-CSharp", "", "Player", "health", &hp);

    // Hook a method by name (gate behind a runtime feature flag):
    static void* orig = nullptr;
    il2::hook_method("Assembly-CSharp", "", "Player", "get_health",
                     /*argc=*/0, (void*)proxy_get_health, &orig);
}
```

Wait-for-library sketch (bounded, non-blocking) — reuse the template's existing worker pattern:

```cpp
// pseudo: poll up to ~10s, then give up and log.
for (int i = 0; i < 100 && !find_il2cpp_module(&m); ++i) usleep(100 * 1000);
if (m.base) on_il2cpp_ready(); else /* log("libil2cpp.so never mapped") */;
```

Rules that keep this template-clean and safe:

- Gate every non-smoke hook behind a runtime feature flag; keep patches reversible (wrapper /
  argument-rewrite preferred over constant-return).
- Keep any Tier B `NAME→RVA` header on an **app branch** under an ignored `artifacts/`-style
  path; never commit target offsets to `main`.
- Authorized research/testing only; do not use this to bypass third-party protections outside a
  lab you own. Same detection-hygiene rules as [`SHADOWHOOK_NOTES.md`](SHADOWHOOK_NOTES.md).

---

## 6. Version / robustness notes (arm64 focus)

- **Export API stability (Tier A) is the win.** The `il2cpp_*` C exports we use have kept the
  same signatures Unity **2019 → 2020 → 2021 → 2022/2023**. The metadata *format* churns
  (v24 → v27 → v29 → v31) but that is hidden behind the exports, so name-based resolution keeps
  working across versions without code changes. This is why Tier A is the template default.
- **`MethodInfo.methodPointer` is field[0] in all versions** — the one struct fact we rely on,
  and it has never moved (v27+ only *appended* `virtualMethodPointer` after it). Reading
  `*(void**)methodInfo` is safe everywhere.
- **What breaks, and how to degrade:**
  - Exports stripped/renamed → Tier A `dlsym` returns null → detect in `il2cpp_bind()` and fall
    back to Tier B RVA table (per-version, app branch). Do not pretend Tier A can recover.
  - Exports present but **stubbed** (return null/garbage) → detect by sanity-checking a known
    class lookup at init; if it fails, treat as Tier B.
  - `global-metadata.dat` **encrypted** → in-process parsing won't help; a host dump against a
    memory-dumped `libil2cpp.so`/decrypted metadata is the only route, reduced to an RVA table.
  - Metadata **version bump** you don't yet parse (Tier B only) → keep parsers version-gated on
    the `int32` at file offset 4; refuse gracefully on unknown versions rather than misparse.
  - ABI: **arm64-v8a** is primary; ShadowHook also covers armeabi-v7a. x86/x86_64 emulators are
    Java-hook-only here (see [`SHADOWHOOK_NOTES.md`](SHADOWHOOK_NOTES.md)).
- **Uncertain / verify-per-target:** exact Unity↔metadata-version mapping (24/27/29/31 boundaries
  shift with point releases); AArch64 value-type passing at any specific hooked site; whether a
  given protected title decrypts metadata in place. Confirm each on the actual target before
  shipping a hook.

---

## Sources

- IL2CPP export signatures (`il2cpp-api-functions.h`, `DO_API` macro):
  [dreamanlan/il2cpp_ref](https://github.com/dreamanlan/il2cpp_ref/blob/master/libil2cpp/il2cpp-api-functions.h),
  [4ch12dy/il2cpp (Unity 2019)](https://github.com/4ch12dy/il2cpp/blob/master/unity_2019_x/libil2cpp/il2cpp-api-functions.h)
- Exports stripping / encryption / stub caveats and metadata dependency:
  [Il2CppInspector tutorial (katyscode)](https://katyscode.wordpress.com/2021/01/14/il2cppinspector-tutorial-working-with-code-in-il2cpp-dll-injection-projects/)
- `global-metadata.dat` format, magic `0xFAB11BAF`, version history & obfuscation:
  [IL2CPP Part 2 — Structural Overview & Finding the Metadata (katyscode)](https://katyscode.wordpress.com/2020/12/27/il2cpp-part-2/),
  [Finding obfuscated global-metadata (katyscode)](https://katyscode.wordpress.com/2021/02/23/il2cpp-finding-obfuscated-global-metadata/)
- CodeRegistration / MetadataRegistration location, `methodPointers`, register-call trace &
  arm64 pattern:
  [Manually Finding CodeRegistration and MetadataRegistration (tomorrowisnew)](https://tomorrowisnew.com/posts/Finding-CodeRegistration-and-MetadataRegistration/),
  [Perfare/Il2CppDumper](https://github.com/Perfare/Il2CppDumper)
- `MethodInfo` layout (methodPointer as field[0]; v27+ virtualMethodPointer) & version-specific
  structs:
  [Il2CppDumper HeaderConstants.cs](https://github.com/Perfare/Il2CppDumper/blob/master/Il2CppDumper/Outputs/HeaderConstants.cs)
- ShadowHook API (`shadowhook_init`, `shadowhook_hook_func_addr`, modes, `SHADOWHOOK_CALL_PREV`,
  `SHADOWHOOK_STACK_SCOPE`):
  [bytedance/android-inline-hook manual](https://github.com/bytedance/android-inline-hook/blob/main/doc/manual.md)
