# Runtime resolution & hooking: Godot, Lua, Cocos2d-x

Reusable, engine-agnostic strategy for **auto-resolving and hooking three script/native game
engines by name, version-tolerantly**, from an Android native module (LSPosed/Zygisk) at
runtime.

This document is deliberately **honest about where a clean runtime approach does not exist**.
Godot in particular has no supported way to call its C++ core from an injected module; the
sections below say so and give the realistic fallbacks instead of pretending otherwise.

It plugs into the existing template pieces:

- `EngineDetector` (Java) already classifies the target as `UNITY / UNREAL / COCOS2DX / GODOT /
  FLUTTER / REACT_NATIVE / XAMARIN / NATIVE`. See
  `app/src/main/java/com/template/lsposed/engine/EngineDetector.java`.
- `native_utils` (C++/JNI) already exposes the four primitives every strategy below needs:
  `nativeFindModule` (module base+size from `/proc/self/maps`), `nativePatternScan` (AOB scan),
  `nativeResolveSymbol` (`dlopen(RTLD_NOLOAD)` + `dlsym`), `nativeReadMemory`,
  `nativeWriteMemory`. See `app/src/main/cpp/native_utils.cpp`.
- ShadowHook 2.0.0 is the hook engine (`shadowhook_hook_sym_name*`, `shadowhook_hook_func_addr*`).
  See `docs/SHADOWHOOK_NOTES.md`.

> Scope / ethics: same rules as the rest of the template — authorized targets only, keep
> target-specific offsets and AOB patterns on app-specific branches, gate every non-smoke hook
> behind a feature flag. Do not commit game offsets to `main`.

---

## 0. Shared model: resolution order and the two anchors

Every engine below uses the **same resolution ladder**. Prefer the earliest rung that works:

1. **Exported symbol** — `dlopen(lib, RTLD_NOW|RTLD_NOLOAD)` then `dlsym`. Only works for symbols
   actually in the module's `.dynsym`. Cheap, stable, version-tolerant when it works.
   (`native_utils::native_resolve_symbol` already does exactly this.)
2. **Exported *anchor* + xref** — even fully stripped engines keep their **JNI entry points**
   exported (the linker needs them by name). Those functions call straight into the engine, so
   they are a reliable place to (a) hook a per-frame tick with zero pattern scanning, or (b)
   disassemble the prologue to recover a private callee's address.
3. **RTTI / vtable recovery** — for C++ engines with RTTI enabled (Cocos), find the type-info
   name string in `.rodata`, walk to the `type_info`, then to the vtable, then to virtual method
   pointers.
4. **AOB pattern scan**, keyed by engine version — last resort for stripped, non-virtual,
   non-exported functions. Store the pattern in a version→pattern table so upgrades are a data
   change, not a code change. (`native_utils::native_pattern_scan`.)

Version tolerance = **detect the version first, then pick the resolver**. For script engines
(Lua) the ABI is discoverable from *which symbols exist*. For native engines (Godot/Cocos) read
the version string embedded in the `.so`, then select the matching symbol names or AOB pattern.

Minimal native resolver used by the snippets below (drop next to `native_utils`):

```cpp
// resolve.h — try exported symbol first, fall back to an AOB pattern inside one module.
#include <dlfcn.h>
#include <cstdint>
#include <cstddef>

// native_utils already implements module-range + pattern scanning; reuse via these decls,
// or lift its read_maps()/parse_pattern()/pattern helpers into a shared TU.
namespace nu {
    bool   module_range(const char* soname, uintptr_t* base, size_t* size); // from /proc/self/maps
    void*  pattern_scan(uintptr_t base, size_t size, const char* ida_pattern); // "E9 ?? ?? ?? ??"
}

// Exported symbol, or nullptr.
inline void* sym(const char* soname, const char* name) {
    void* h = dlopen(soname, RTLD_NOW | RTLD_NOLOAD);
    if (!h) return nullptr;
    void* p = dlsym(h, name);
    dlclose(h);          // RTLD_NOLOAD keeps the refcount; safe to balance.
    return p;
}

// dlsym -> pattern fallback. `pattern` may be nullptr if you only have exports.
inline void* resolve(const char* soname, const char* symname, const char* pattern) {
    if (void* p = sym(soname, symname)) return p;
    if (!pattern) return nullptr;
    uintptr_t base = 0; size_t size = 0;
    if (!nu::module_range(soname, &base, &size)) return nullptr;
    return nu::pattern_scan(base, size, pattern);
}
```

All hooks below assume ShadowHook is already `shadowhook_init(SHADOWHOOK_MODE_SHARED, …)`-ed by
the template, and use the shared-mode proxy convention (`SHADOWHOOK_STACK_SCOPE()` +
`SHADOWHOOK_CALL_PREV`).

---

## (A) GODOT — `libgodot_android.so` (Godot 3.x and 4.x)

### A.1 What is actually exported — the honest baseline

Godot's Android build links the **entire engine statically** into one `libgodot_android.so`. The
only things reliably in its `.dynsym` are:

- the JNI bridge — `Java_org_godotengine_godot_GodotLib_*` (defined in
  `platform/android/java_godot_lib_jni.cpp`), plus `JNI_OnLoad`;
- libc/STL imports and a few glue symbols.

`Object::set` / `Object::get` / `Object::call`, `ClassDB`, `Engine::get_singleton()`,
`SceneTree`, `GDScriptFunction::call` are **ordinary mangled C++ members with internal linkage in
a release build; they are not in `.dynsym`**, and release templates are stripped on top of that.
So **`dlsym` will not find the engine core.** ([Godot PR #105605 — native debug symbols][gd-syms],
[HackTricks — reversing native libs][ht])

**GDScript vs GDExtension/GDNative — and why it matters for an injected module:**

- **GDScript** is bytecode run by an in-process VM (`GDScriptFunction::call`); nothing about it is
  exposed as a C ABI.
- **Godot 4 = GDExtension.** The engine hands a **function-pointer table** to each *registered*
  extension through its entry point:
  ```c
  GDExtensionBool my_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                          GDExtensionClassLibraryPtr        p_library,
                          GDExtensionInitialization        *r_initialization);
  ```
  You then resolve everything with `p_get_proc_address("classdb_get_method_bind")`,
  `p_get_proc_address("object_method_bind_call")`, `p_get_proc_address("variant_call")`, etc.
  ([gdextension_interface.h @4.4][gd-iface], [hello-gdextension C example][gd-hello])
- **Godot 3 = GDNative.** Same idea, older shape: your library receives a
  `godot_gdnative_core_api_struct*` full of function pointers such as
  `godot_method_bind_get_method("Object","set")` and
  `godot_method_bind_ptrcall(mb, instance, args, ret)`.
  ([Godot 3.0 GDNative C example][gd-gdnative-c])

> **The clean path is closed for injection.** `p_get_proc_address` (Godot 4) and the GDNative api
> struct (Godot 3) are delivered **only to a library the game itself loads as an extension**. An
> LSPosed/Zygisk module is *not* a registered extension, so it never receives them. There is no
> supported, `dlsym`-able "get the interface" function. Anyone claiming you can just call
> `ClassDB`/`Object::set` from an injected Godot module is skipping this fact.

Two ways to get the clean ABI anyway, in order of realism:

1. **The target already loads an extension.** Many shipped Godot games bundle a `.gdextension`
   (ads, IAP, analytics). Hook *its* entry symbol (exported, named in the `.gdextension`/`.pck`)
   and **steal the first argument** — that pointer *is* `p_get_proc_address` (or the GDNative api
   struct). From then on you have the full, version-safe ClassDB/Variant ABI.
2. **Ship your own extension** by editing `project.godot`/the `.pck` to register it. Requires
   repacking the app; generally not feasible for a closed third-party target.

If neither holds, you cannot call the engine "properly" — use the fallbacks in A.3/A.4.

`classdb_get_method_bind` (and `variant_get_ptr_utility_function`) additionally require a
**per-method hash** taken from the version's `extension_api.json`; the hash is a forward-compat
guard, so the correct hash is itself version-specific. ([godot-proposals #8092][gd-hash])

### A.2 Detect version and Godot 3 vs 4 at runtime

No API needed — read it out of the mapped `.so`:

```cpp
// Godot embeds its version string in .rodata, e.g. "4.2.1.stable.official" / "3.5.3.stable".
// Scan the module for the "<major>.<minor>.<patch>.stable" shape, or just the leading digit.
uintptr_t base; size_t size;
nu::module_range("libgodot_android.so", &base, &size);
// AOB for the ASCII of ".stable" then look back for the version, or search "godotengine".
// Coarse 3-vs-4 discriminator that does not need the version string:
bool is_v4 = sym("libgodot_android.so", "JNI_OnLoad") &&
             nu::pattern_scan(base, size, /* ascii "GDExtension" */
                              "47 44 45 78 74 65 6E 73 69 6F 6E");   // "GDExtension"
// Godot 3 instead contains "GDNative"/"nativescript" marker strings.
```

Both majors export the same JNI class (`org.godotengine.godot.GodotLib`), so the JNI anchor in
A.3 works for **both 3.x and 4.x**.

### A.3 Best hook that needs *no* engine symbols — the per-frame tick

`Java_org_godotengine_godot_GodotLib_step` is exported and is called **once per frame on the GL
thread**; it calls `os_android->main_loop_iterate()` → `Main::iteration()`.
([java_godot_lib_jni.cpp][gd-jni]) Hook it for a stable, version-tolerant per-frame callback into
your own logic — polling live pattern-resolved addresses, applying writes, driving an overlay —
without resolving a single C++ core symbol.

```cpp
static void* g_step_orig = nullptr;

// JNIEXPORT jboolean Java_..._GodotLib_step(JNIEnv*, jclass) in Godot 3/4.
static jboolean godot_step_proxy(JNIEnv* env, jclass klass) {
    SHADOWHOOK_STACK_SCOPE();
    on_godot_frame();                        // your per-frame work (guard re-entrancy / cost)
    return SHADOWHOOK_CALL_PREV(godot_step_proxy, env, klass);
}

void install_godot_frame_hook() {
    g_step_orig = shadowhook_hook_sym_name(
        "libgodot_android.so",
        "Java_org_godotengine_godot_GodotLib_step",
        reinterpret_cast<void*>(godot_step_proxy),
        &g_step_orig);
}
```

### A.4 Reading/writing game state without the interface (fallback)

If you cannot obtain `p_get_proc_address`, you cannot construct a fresh `Variant`/`StringName` (the
constructors are also unexported), so **do not try to call `Object::set` from scratch**. Instead
**intercept the engine's own calls and edit in place**. Two viable hook points, both
pattern-resolved and keyed to the detected version:

- **`Object::set` / `Object::get`** — every native property write/read passes through here.
  Illustrative Itanium mangling (confirm against a matching debug build with `nm -C` — the exact
  parameter list drifts across versions):
  `_ZN6Object3setERK10StringNameRK7VariantPb` (`Object::set(StringName const&, Variant const&,
  bool*)`), `_ZNK6Object3getERK10StringNamePb` (`Object::get(...) const`).
- **`GDScriptFunction::call`** — the whole GDScript VM funnels through one function:
  `Variant GDScriptFunction::call(GDScriptInstance *p_instance, const Variant **p_args, int
  p_argcount, Callable::CallError &r_err, CallState *p_state)`, in
  `modules/gdscript/gdscript_vm.cpp`. Hooking it lets you observe/patch every script call; the
  Variant stack is `alloca`'d locally so per-opcode patching is impractical, but the outer call
  boundary is stable. ([Godot GDScript VM][gd-vm], [gdscript_function.h][gd-vm-h])

Because none of these are exported, the resolver **must** carry a per-version AOB pattern:

```cpp
struct GodotProfile { const char* version_prefix;         // "4.2", "3.5", ...
                      const char* aob_object_set;
                      const char* aob_object_get;
                      const char* aob_gdscript_call; };
// Build this table from Frida recon on each target version; ship it on the app branch.
static void* resolve_object_set(const GodotProfile& p) {
    return resolve("libgodot_android.so", "_ZN6Object3setERK10StringNameRK7VariantPb",
                   p.aob_object_set);   // dlsym works only on unstripped/debug builds
}
```

### A.5 Reusable helper concept — "set property P on all objects of class C"

**Clean variant (only if you captured the interface, per A.1):**

```text
1. mb_set = classdb_get_method_bind("Object", "set", HASH_FOR_VERSION)     // Godot 4
2. build args: StringName(P) via string_name_new_with_*; Variant(value) via variant_new_*
3. for each live instance o of class C:
       object_method_bind_call(mb_set, o, args, 2, &ret, &err)
   (Godot 3: godot_method_bind_get_method("Object","set") + godot_method_bind_ptrcall)
```
Enumerating "all objects of class C" needs live instances: hook C's constructor (pattern-resolved)
and record `this` into a set, or walk the scene tree from the `SceneTree`/`Engine` singleton if
you also resolved it.

**Injection-only variant (no interface) — intercept instead of push:**

```text
1. hook Object::set(name, value, valid)   [pattern-resolved for the version]
2. in the proxy:
     if class_of(this) == C && stringname_text(name) == P:   // class via Object::get_class (also
                                                              // pattern) or vtable identity of C
         overwrite *value (in place) with your Variant before calling the original
3. optionally hook Object::get(name, valid) and override the returned Variant for (C, P)
   so reads observe the forced value too.
```
This forces P on writes/reads flowing through the engine, which is what an injected module can
actually guarantee. `stringname_text`/`class_of` are themselves pattern-resolved helpers; keep
them in the per-version profile.

**Godot 3 vs 4 summary**

| Concern | Godot 3.x | Godot 4.x |
|---|---|---|
| Extension ABI | GDNative api struct (`godot_gdnative_core_api_struct*`) | GDExtension `p_get_proc_address` table |
| Method call | `godot_method_bind_get_method` + `godot_method_bind_ptrcall` | `classdb_get_method_bind`(+hash) + `object_method_bind_call` |
| Interface delivered to injected module? | No | No |
| Per-frame anchor (exported) | `Java_..._GodotLib_step` | `Java_..._GodotLib_step` |
| Core symbols in `.dynsym`? | No (stripped release) | No (stripped release) |
| Script VM entry | `GDScriptFunction::call` | `GDScriptFunction::call` (arg list changed) |

---

## (B) LUA games — cocos2d-lua, embedded Lua 5.1/5.3, LuaJIT

Lua is the friendly case: the C API is a **stable, `extern "C"`, ABI**. On Android the Lua
symbols are almost always exported (either in `liblua.so`/`libluajit.so`, or re-exported from the
host `.so` such as `libcocos2dlua.so`). So rung 1 (`dlsym`) usually just works — the real work is
**picking the right signatures for the flavor**.

### B.1 The signatures that differ across flavors

The three flavors you will meet are **Lua 5.1**, **Lua 5.3**, and **LuaJIT** (which implements the
**Lua 5.1 API + ABI**, plus 5.2-style extensions). ([LuaJIT extensions][lj-ext],
[LuaJIT C API][lj-api]) Lua 5.2 sits between 5.1 and 5.3 and mostly matters for the
`getglobal`/`pcall` split.

| Operation | Lua 5.1 / LuaJIT | Lua 5.3 |
|---|---|---|
| protected call | **`lua_pcall`** is a real function: `int lua_pcall(lua_State*, int nargs, int nresults, int errfunc)` | `lua_pcall` is a **macro** → real symbol is **`lua_pcallk`**: `int lua_pcallk(lua_State*, int, int, int, lua_KContext, lua_KFunction)` |
| load bytecode/text | **`luaL_loadbuffer`**: `int luaL_loadbuffer(lua_State*, const char* buff, size_t sz, const char* name)` | `luaL_loadbuffer` is a **macro** → **`luaL_loadbufferx`**: `int luaL_loadbufferx(lua_State*, const char*, size_t, const char*, const char* mode)` |
| to number | `lua_Number lua_tonumber(lua_State*, int)` | `lua_tonumber` is a **macro** → **`lua_tonumberx`**: `lua_Number lua_tonumberx(lua_State*, int, int* isnum)` |
| get global | **macro** `lua_getglobal(L,s) → lua_getfield(L, LUA_GLOBALSINDEX, s)` | real function `int lua_getglobal(lua_State*, const char*)` (returns pushed type; **5.2 returns `void`**) |
| set global | **macro** `lua_setglobal(L,s) → lua_setfield(L, LUA_GLOBALSINDEX, s)` | real function `void lua_setglobal(lua_State*, const char*)` |
| `lua_getfield` | `void lua_getfield(lua_State*, int, const char*)` | `int  lua_getfield(lua_State*, int, const char*)` (returns type) |
| `lua_settop` | `void lua_settop(lua_State*, int)` — **same across all** | same |
| globals table index | pseudo-index **`LUA_GLOBALSINDEX` (`-10002`)** | **removed**; use `lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS)` with `LUA_RIDX_GLOBALS == 2` |

([Lua 5.1 manual][lua51], [Lua 5.3 manual][lua53], [Lua 5.2 "what changed"][lua52wiki],
[lua-compat-5.3][lua-compat])

Extra hazards worth pinning down:

- **`lua_pcall`/`luaL_loadbuffer`/`lua_tonumber` are macros in 5.2+**, so those *names* are **not
  exported** — the exported symbols are the `*k`/`*x` variants. Resolve by the real symbol.
- **`lua_Integer` width changed**: `ptrdiff_t` in 5.1, `long long` (64-bit) by default in 5.3.
  `lua_Number` is `double` in every stock build. If a game uses a custom `luaconf.h` (some do),
  confirm before trusting numeric reads.
- **`luaL_dostring` is always a macro** — never a symbol: `(luaL_loadstring(L,s) ||
  lua_pcall(L,0,LUA_MULTRET,0))` with `LUA_MULTRET == -1`. To run arbitrary Lua you must call
  `luaL_loadstring` + `lua_pcall`/`lua_pcallk` yourself (B.4).

### B.2 Detect the flavor at runtime (by which symbols exist)

```cpp
enum class LuaFlavor { LUA51, LUA52, LUA53, LUAJIT, UNKNOWN };

LuaFlavor detect_lua(const char* so) {   // e.g. "libluajit.so", "liblua.so", "libcocos2dlua.so"
    if (sym(so, "luaJIT_setmode") || sym(so, "luaJIT_version")) return LuaFlavor::LUAJIT; // 5.1 API
    if (sym(so, "lua_isinteger"))  return LuaFlavor::LUA53;   // integer subtype => 5.3/5.4
    if (sym(so, "lua_pcallk"))     return LuaFlavor::LUA52;   // pcallk but no isinteger => 5.2
    if (sym(so, "lua_pcall"))      return LuaFlavor::LUA51;   // real pcall symbol => 5.1
    return LuaFlavor::UNKNOWN;
}
```

Runtime cross-check (5.2+ only): `lua_version` returns `const lua_Number*`; deref gives `502`/`503`.
LuaJIT reports `LUAJIT_VERSION`/`luaJIT_version` instead. If the game statically links Lua into a
big `.so` (common with Cocos), pass that `.so` name as `so`.

### B.3 Bind the API once, flavor-aware

```cpp
struct LuaApi {
    LuaFlavor flavor = LuaFlavor::UNKNOWN;
    const char* so   = nullptr;

    // Names that exist across flavors are resolved to the right symbol here:
    int   (*pcall)(lua_State*, int, int, int)                      = nullptr; // 5.1/LuaJIT direct
    int   (*pcallk)(lua_State*, int, int, int, intptr_t, void*)    = nullptr; // 5.2/5.3
    int   (*loadbuffer)(lua_State*, const char*, size_t, const char*)              = nullptr;
    int   (*loadbufferx)(lua_State*, const char*, size_t, const char*, const char*)= nullptr;
    int   (*loadstring)(lua_State*, const char*)                   = nullptr;
    void  (*settop)(lua_State*, int)                               = nullptr;
    void  (*getfield)(lua_State*, int, const char*)                = nullptr; // 5.1 shape
    // 5.2+ globals:
    void  (*getglobal)(lua_State*, const char*)                    = nullptr; // ret void/int; call as void
    void  (*setglobal)(lua_State*, const char*)                    = nullptr;
    double(*tonumber)(lua_State*, int)                             = nullptr; // 5.1/LuaJIT
    double(*tonumberx)(lua_State*, int, int*)                      = nullptr; // 5.2/5.3
};

bool bind_lua(LuaApi& a, const char* so) {
    a.so = so; a.flavor = detect_lua(so);
    a.loadstring = (decltype(a.loadstring)) sym(so, "luaL_loadstring");
    a.settop     = (decltype(a.settop))     sym(so, "lua_settop");
    if (a.flavor == LuaFlavor::LUA51 || a.flavor == LuaFlavor::LUAJIT) {
        a.pcall      = (decltype(a.pcall))      sym(so, "lua_pcall");
        a.loadbuffer = (decltype(a.loadbuffer)) sym(so, "luaL_loadbuffer");
        a.getfield   = (decltype(a.getfield))   sym(so, "lua_getfield");
        a.tonumber   = (decltype(a.tonumber))   sym(so, "lua_tonumber");
    } else { // 5.2 / 5.3
        a.pcallk      = (decltype(a.pcallk))      sym(so, "lua_pcallk");
        a.loadbufferx = (decltype(a.loadbufferx)) sym(so, "luaL_loadbufferx");
        a.getglobal   = (decltype(a.getglobal))   sym(so, "lua_getglobal");
        a.setglobal   = (decltype(a.setglobal))   sym(so, "lua_setglobal");
        a.tonumberx   = (decltype(a.tonumberx))   sym(so, "lua_tonumberx");
    }
    return a.settop && (a.pcall || a.pcallk);
}
```

`LUA_GLOBALSINDEX` = `-10002` (5.1/LuaJIT). On 5.2+ read a global with
`lua_getglobal`; the flavor branch keeps you off the removed pseudo-index.

### B.4 Capture the `lua_State*` at runtime, then intercept scripts and run Lua

Games rarely hand you the state, so **hook a hot C-API entry and grab arg 1** (`lua_State*` is
always the first parameter). The best choice is the loader, because you *also* get the script
buffer to inspect or patch as it loads:

```cpp
static LuaApi g_lua;
static lua_State* g_L = nullptr;          // captured state
static void* g_loadbuf_orig = nullptr;

// 5.1 / LuaJIT: int luaL_loadbuffer(lua_State*, const char* buff, size_t sz, const char* name)
static int loadbuffer_proxy(lua_State* L, const char* buff, size_t sz, const char* name) {
    SHADOWHOOK_STACK_SCOPE();
    g_L = L;                               // <-- state captured
    // Inspect/patch the chunk here (buff/sz) before it compiles: e.g. swap a constant,
    // or copy into a mutable buffer and pass that instead. Honor original signature.
    return SHADOWHOOK_CALL_PREV(loadbuffer_proxy, L, buff, sz, name);
}

void hook_lua_loader() {
    if (g_lua.flavor == LuaFlavor::LUA51 || g_lua.flavor == LuaFlavor::LUAJIT)
        g_loadbuf_orig = shadowhook_hook_sym_name(g_lua.so, "luaL_loadbuffer",
                            (void*)loadbuffer_proxy, &g_loadbuf_orig);
    else  // 5.2/5.3: hook luaL_loadbufferx (extra const char* mode arg)
        g_loadbuf_orig = shadowhook_hook_sym_name(g_lua.so, "luaL_loadbufferx",
                            (void*)loadbuffer_proxy, &g_loadbuf_orig);
}
```

Alternative capture points if the loader is not hit early enough: hook `lua_pcall`/`lua_pcallk`
(every protected call), or — Cocos-specific and cleanest — `LuaEngine::getInstance()->
getLuaStack()->getLuaState()` (see C.5). `lua_settop` also works (universal signature) but is
extremely hot; prefer the loader.

**Run arbitrary Lua once you hold the state** (must run on the Lua/GL thread — e.g. from inside
one of these hooks or your Godot/Cocos per-frame tick, never from a random worker thread):

```cpp
// Equivalent to luaL_dostring, done by hand because dostring is a macro (no symbol):
bool lua_run(const LuaApi& a, lua_State* L, const char* code) {
    if (!L || a.loadstring(L, code) != 0) return false;      // pushed error on failure
    int rc;
    if (a.pcall)  rc = a.pcall(L, 0, /*LUA_MULTRET*/ -1, 0); // 5.1 / LuaJIT
    else          rc = a.pcallk(L, 0, -1, 0, 0, nullptr);    // 5.2 / 5.3 (ctx=0, k=NULL)
    return rc == 0;
}

// Read/patch a global:
double lua_get_global_number(const LuaApi& a, lua_State* L, const char* name) {
    if (a.getfield)  a.getfield(L, /*LUA_GLOBALSINDEX*/ -10002, name); // 5.1 / LuaJIT
    else             a.getglobal(L, name);                             // 5.2 / 5.3
    double v = a.tonumber ? a.tonumber(L, -1) : a.tonumberx(L, -1, nullptr);
    a.settop(L, -2);   // pop the value (settop is stable across flavors)
    return v;
}
```

To *write* a global generically it is usually simplest to `lua_run(a, L, "GLOBAL = 123")` rather
than pushing values by hand across flavor differences.

### B.5 Robustness / version notes (Lua)

- Resolve by the **exact real symbol** (`lua_pcallk` not `lua_pcall` on 5.2+, etc.) — the macro
  names are not in the symbol table.
- **LuaJIT** additionally exports 5.2-style extensions (`lua_tonumberx`, `lua_loadx`, `lua_copy`,
  `lua_version`) *and* keeps 5.1 (`LUA_GLOBALSINDEX`, `lua_pcall`). Detect it via `luaJIT_setmode`
  **first** so you don't misclassify it as 5.2 just because `lua_tonumberx` exists.
- Some builds statically link Lua into the game `.so` with hidden visibility → symbols vanish.
  Then fall back to the host bridge (Cocos `LuaEngine`, C.5) or an AOB pattern for `lua_pcall`.
- For code that must compile against both 5.1 and 5.3, `lua-compat-5.3` backports the 5.3-shaped
  APIs onto 5.1/5.2. ([lua-compat-5.3][lua-compat])

---

## (C) COCOS2D-X C++ — `libcocos2dcpp.so`

### C.1 Resolution order (exports → JNI anchor → RTTI/vtable → pattern)

Cocos2d-x compiles its C++ into `libcocos2dcpp.so` (or `libcocos2djs.so` / `libcocos2dlua.so` for
the script variants). Historically Cocos does **not** build with aggressive
`-fvisibility=hidden`, and it keeps **RTTI enabled** (it uses `dynamic_cast`), so you usually have
more to work with than Godot:

1. **Exported mangled symbols** (`dlsym`), Itanium ABI. Frequently present:
   - `cocos2d::Director::getInstance()` → `_ZN7cocos2d8Director11getInstanceEv`
   - `cocos2d::Director::getRunningScene()` → `_ZN7cocos2d8Director15getRunningSceneEv`
   - `cocos2d::Director::mainLoop()` → `_ZN7cocos2d8Director8mainLoopEv` *(virtual)*
   - `cocos2d::Director::drawScene()` → `_ZN7cocos2d8Director9drawSceneEv` *(non-virtual)*
   - `cocos2d::Scheduler::update(float)` → `_ZN7cocos2d9Scheduler6updateEf`
   Verify each with `nm -CD libcocos2dcpp.so` on the actual target — presence varies by build.
2. **JNI anchor (always exported).** `Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender` is the
   per-frame entry from the Java `GLSurfaceView`; it does exactly
   `cocos2d::Director::getInstance()->mainLoop();`. ([Cocos2dxRenderer JNI][cc-jni]) Even when
   `cocos2d::*` is stripped, this is exported — hook it for a per-frame tick (C.3), or disassemble
   its prologue to recover the `Director::getInstance` call target and the `mainLoop` vtable slot.
3. **RTTI / vtable recovery** (for virtual methods when exports are gone): find the type-info name
   string `"N7cocos2d8DirectorE"` in `.rodata`, walk to its `type_info`, then to the vtable that
   references it; virtual slots (`mainLoop`, `setAnimationInterval`, …) follow. `getInstance`
   (static) and `drawScene` (non-virtual) are **not** in the vtable — get those from exports or
   the JNI xref.
4. **AOB pattern**, version-keyed — last resort for stripped non-virtual functions.

### C.2 Where game state lives

`Director` is the singleton root of everything. ([Cocos Director reference][cc-dir])

```cpp
using GetInstanceFn = void* (*)();          // cocos2d::Director* (opaque here)
auto Director_getInstance =
    (GetInstanceFn) resolve("libcocos2dcpp.so", "_ZN7cocos2d8Director11getInstanceEv", nullptr);
void* director = Director_getInstance ? Director_getInstance() : nullptr;
// From the Director you reach: getRunningScene() -> Scene -> Node tree (children, positions,
// user objects), plus getScheduler(), getActionManager(), getTextureCache().
```

Node field offsets (position, scale, tag, script bindings) are **version- and build-specific** —
recover them from the running instance, not from assumptions. `getRunningScene()` gives the live
scene graph to walk.

### C.3 Hook a per-frame update

Simplest and most robust — hook the exported JNI render entry (works even fully stripped):

```cpp
static void* g_render_orig = nullptr;
static void cocos_render_proxy(JNIEnv* env, jclass klass) {
    SHADOWHOOK_STACK_SCOPE();
    on_cocos_frame();                        // read/patch Director state here
    SHADOWHOOK_CALL_PREV(cocos_render_proxy, env, klass);
}
void hook_cocos_frame() {
    g_render_orig = shadowhook_hook_sym_name(
        "libcocos2dcpp.so",
        "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender",
        (void*)cocos_render_proxy, &g_render_orig);
}
```

Finer-grained alternatives when the mangled symbols resolve: hook `Director::mainLoop()`
(virtual), or `Scheduler::update(float dt)` for the frame **delta** (`_ZN7cocos2d9Scheduler6updateEf`).
Per-frame chain for reference: `onDrawFrame` → `nativeRender` → `Director::mainLoop` →
`drawScene` → `Scheduler::update(dt)` → `glClear` → scene `visit`/`render`. ([Cocos JNI +
render loop][cc-jni])

### C.4 RTTI/vtable fallback sketch (stripped builds)

```text
1. pattern_scan .rodata for the C-string  "N7cocos2d8DirectorE"     (the type_info name)
2. find the std::type_info whose name-ptr points at that string
3. find the vtable: a pointer slot referencing that type_info (vtable[-1] == &type_info,
   vtable[-2] == offset-to-top). virtual functions start at vtable[0].
4. map the slot index of mainLoop() from a known-good build; call/hook through it.
```
Use exports/JNI-xref for `getInstance`/`drawScene` since they are not virtual.

### C.5 cocos2d-lua: reach the `lua_State*` through the engine

When the target is `libcocos2dlua.so` (Cocos + Lua bindings), you can grab the state through the
Cocos bridge instead of guessing the Lua `.so`:

```cpp
// cocos2d::LuaEngine::getInstance()  -> _ZN7cocos2d9LuaEngine11getInstanceEv
// LuaEngine::getLuaStack()           -> _ZN7cocos2d9LuaEngine11getLuaStackEv
// LuaStack::getLuaState()            -> _ZN7cocos2d8LuaStack10getLuaStateEv
auto LuaEngine_getInstance = (void*(*)())      resolve("libcocos2dlua.so","_ZN7cocos2d9LuaEngine11getInstanceEv", nullptr);
auto LuaEngine_getLuaStack = (void*(*)(void*)) resolve("libcocos2dlua.so","_ZN7cocos2d9LuaEngine11getLuaStackEv", nullptr);
auto LuaStack_getLuaState  = (lua_State*(*)(void*)) resolve("libcocos2dlua.so","_ZN7cocos2d8LuaStack10getLuaStateEv", nullptr);
lua_State* L = nullptr;
if (LuaEngine_getInstance && LuaEngine_getLuaStack && LuaStack_getLuaState) {
    void* eng = LuaEngine_getInstance();
    L = LuaStack_getLuaState(LuaEngine_getLuaStack(eng));
}
```

([Cocos LuaEngine / LuaStack::getLuaState][cc-lua]) Then hand `L` to the **section (B)** helpers.
Classic Cocos2d-lua ships **Lua 5.1**-family (tolua); the *Quick-Cocos2dx-Community* fork often
ships **LuaJIT** — so still run `detect_lua()` rather than assuming. If the bridge symbols are
stripped, fall back to hooking `luaL_loadbuffer`/`lua_pcall` (B.4) to capture `L`.

### C.6 Robustness / version notes (Cocos)

- Try `dlsym` on the mangled names first; **many** Cocos builds keep them. The JNI `nativeRender`
  anchor is the dependable fallback because the linker cannot strip JNI export names.
- Cocos **2.x** uses the `CCDirector::sharedDirector()` / `CC`-prefixed names; **3.x/4.x** use
  `cocos2d::Director::getInstance()`. Detect the major from which symbols/strings are present.
- Node/Scene field layouts change between minor versions — always derive offsets from the live
  instance on the specific build; keep them on the app branch.

---

## Unified strategy — how the template picks per detected engine

`EngineDetector` returns the engine; map it to a resolution strategy:

| `EngineDetector.Engine` | Primary lib | Rung-1 (exports) | Per-frame anchor (exported, no scan) | State read/write | Version tolerance |
|---|---|---|---|---|---|
| `GODOT` | `libgodot_android.so` | JNI only; **core not exported** | `Java_..._GodotLib_step` | pattern-hook `Object::set/get` or `GDScriptFunction::call`; clean ClassDB **only** if you steal a loaded extension's `p_get_proc_address` | read version string from `.so`; AOB table per version |
| `COCOS2DX` | `libcocos2dcpp.so` / `libcocos2dlua.so` | mangled `cocos2d::*` often present | `Java_..._Cocos2dxRenderer_nativeRender` | `Director::getInstance()` → scene graph; `Scheduler::update` for dt | detect 2.x vs 3.x by symbol/string; RTTI for virtuals |
| (Lua inside any) | `libluajit.so` / `liblua.so` / host `.so` | Lua C API almost always exported | hook `luaL_loadbuffer(x)` / `lua_pcall(k)` | flavor-bound `LuaApi`; `lua_run` for arbitrary Lua | `detect_lua()` by symbol presence |
| `UNITY` / `UNREAL` / `FLUTTER` / `REACT_NATIVE` / `XAMARIN` / `NATIVE` | — | see `docs/ENGINE_NATIVE_WORKFLOWS.md` | — | — | — |

Decision flow the template follows:

1. `EngineDetector.detect()` → engine enum (already implemented).
2. **Detect version/flavor first** (Lua: symbol presence; Godot/Cocos: version string in the
   `.so`, major from marker symbols).
3. **Resolve via the rung ladder** (§0): exports → JNI anchor/xref → RTTI/vtable → version-keyed
   AOB. Keep the AOB table on the app branch; keep the resolver generic on `main`.
4. **Install the cheapest useful hook first** — the exported per-frame anchor (`GodotLib_step`,
   `Cocos2dxRenderer_nativeRender`) or the Lua loader — gate it behind a feature flag, confirm one
   log hit, then layer state read/write on top.
5. Godot only: if the clean interface is required, **hook a bundled extension's init to steal
   `p_get_proc_address`**; otherwise stay on the intercept-in-place fallback and document the
   limitation on the branch.

**Honest bottom line:** Lua and Cocos2d-x both have a genuine runtime resolution path (stable C
API / exported+RTTI C++), so they are well-supported. **Godot does not** expose its core to an
injected module; the only clean ABI comes from being — or hijacking — a loaded GDExtension/GDNative
library, and everything else is exported-anchor hooking plus version-keyed pattern scanning. Treat
any offset/AOB as target- and version-specific and keep it off `main`.

---

## Sources

Godot:
- [Godot PR #105605 — Android native debug symbols (stripping behavior)][gd-syms]
- [gdextension_interface.h @ 4.4-stable — entry point, `classdb_get_method_bind`, `object_method_bind_call`][gd-iface]
- [hello-gdextension — raw C entry point using `p_get_proc_address`][gd-hello]
- [godot-proposals #8092 — method-bind hash requirement / version guard][gd-hash]
- [Godot 3.0 docs — GDNative C example (`godot_method_bind_get_method` / `..._ptrcall`)][gd-gdnative-c]
- [platform/android/java_godot_lib_jni.cpp — `GodotLib.step` → `Main::iteration`][gd-jni]
- [Godot GDScript VM — `GDScriptFunction::call`][gd-vm], [gdscript_function.h][gd-vm-h]
- [HackTricks — reversing Android native libraries (stripping, Frida, PAC/BTI)][ht]

Lua:
- [Lua 5.1 Reference Manual (C API, `LUA_GLOBALSINDEX`, macros)][lua51]
- [Lua 5.3 Reference Manual (`lua_pcallk`, `luaL_loadbufferx`, `lua_tonumberx`, `LUA_RIDX_GLOBALS`)][lua53]
- [lua-users wiki — Lua 5.2 changes (`lua_getglobal`/`pcall` split)][lua52wiki]
- [lua-compat-5.3 — which APIs are backported][lua-compat]
- [LuaJIT — extensions (5.1 API/ABI + 5.2 extras)][lj-ext], [LuaJIT C API overview][lj-api]

Cocos2d-x:
- [Cocos2dxRenderer JNI — `nativeRender` → `Director::getInstance()->mainLoop()`][cc-jni]
- [Cocos2d-x Director class reference (singleton, scene access)][cc-dir]
- [Cocos2d-x LuaEngine / LuaStack::getLuaState reference][cc-lua]

[gd-syms]: https://github.com/godotengine/godot/pull/105605
[gd-iface]: https://github.com/godotengine/godot/blob/4.4-stable/core/extension/gdextension_interface.h
[gd-hello]: https://github.com/gilzoide/hello-gdextension/blob/main/1.hello-c/README.md
[gd-hash]: https://github.com/godotengine/godot-proposals/discussions/8092
[gd-gdnative-c]: https://docs.godotengine.org/en/3.0/tutorials/plugins/gdnative/gdnative-c-example.html
[gd-jni]: https://github.com/godotengine/godot/blob/master/platform/android/java_godot_lib_jni.cpp
[gd-vm]: https://deepwiki.com/godotengine/godot/6.3-gdscript-compiler-and-vm
[gd-vm-h]: https://github.com/godotengine/godot/blob/master/modules/gdscript/gdscript_function.h
[ht]: https://hacktricks.wiki/en/mobile-pentesting/android-app-pentesting/reversing-native-libraries.html
[lua51]: https://www.lua.org/manual/5.1/manual.html
[lua53]: https://www.lua.org/manual/5.3/manual.html
[lua52wiki]: http://lua-users.org/wiki/LuaFiveTwo
[lua-compat]: https://github.com/lunarmodules/lua-compat-5.3
[lj-ext]: https://luajit.org/extensions.html
[lj-api]: https://deepwiki.com/LuaJIT/LuaJIT/6.2-lua-c-api
[cc-jni]: https://github.com/MicrosoftArchive/cocos2d-x-3.4-quickstart/blob/master/Cocos2dGame/cocos2d/cocos/platform/android/jni/Java_org_cocos2dx_lib_Cocos2dxRenderer.cpp
[cc-dir]: https://docs.cocos2d-x.org/api-ref/cplusplus/v3x/d7/df3/classcocos2d_1_1_director.html
[cc-lua]: https://docs.cocos2d-x.org/api-ref/cplusplus/v4x/d3/d78/classcocos2d_1_1_lua_engine.html
