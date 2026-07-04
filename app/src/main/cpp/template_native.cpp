#include <jni.h>
#include <android/log.h>
#include <shadowhook.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

#include "native_utils.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Reusable native core for the LSPosed universal template.
//
//  This file is engine-AGNOSTIC. It ships three reusable toolkits plus a ShadowHook
//  smoke-test, so a real module for ANY native/C++ Android app is mostly "fill in the
//  anchors":
//    1. A guarded ARM64 byte-patch engine (force a constant return / NOP a check).
//    2. Version-independent SYMBOL RESOLVERS (string-xref and RTTI) built on native_utils —
//       resolve a function/vtable at runtime from a stable string or class name instead of a
//       hardcoded offset, so patches survive app updates / offset shifts.
//    3. An auto-resolving, toggleable CODE FEATURE framework (apply AND revert live).
//
//  For managed engines (Unity IL2CPP, Godot, Lua, Cocos2d-x) see docs/ENGINE_*.md — those use
//  the engine's own C API first (il2cpp_*, lua_*, ClassDB) and fall back to the resolvers here.
//
//  This is a proven design: the same three toolkits shipped a full mod for a hardened King
//  (Candy Crush) title — see docs/REVERSE_ENGINEERING_PLAYBOOK.md.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TEMPLATE_VERBOSE_LOGS
#define TEMPLATE_VERBOSE_LOGS 0
#endif

#if TEMPLATE_VERBOSE_LOGS
#define LOG_TAG "AppRuntime"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOG_TAG "AppRuntime"
#define ALOGI(...) ((void)0)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#endif

namespace {

std::once_flag g_install_once;
std::atomic<int> g_install_result{7777};
std::atomic<uintptr_t> g_module_base{0};

// ═══════════════════════════════════════════════════════════════════════════════
//  1. ARM64 BYTE-PATCH ENGINE
//  Overwrite instructions at a function entry — no hook/trampoline. This is the right tool
//  for "force a constant return" or "skip a check": nothing to detect, no relocation.
//  (For ARM32 targets add Thumb/ARM encodings; most modern games are arm64-v8a only.)
// ═══════════════════════════════════════════════════════════════════════════════
constexpr uint32_t A64_RET = 0xD65F03C0;  // ret
constexpr uint32_t A64_NOP = 0xD503201F;  // nop
constexpr uint32_t MOV_W(int rd, uint16_t imm) {  // movz Wd, #imm
    return 0x52800000u | (static_cast<uint32_t>(imm) << 5) | (static_cast<uint32_t>(rd) & 0x1F);
}
constexpr uint32_t MOV_W_ZR(int rd) {  // mov Wd, wzr  (i.e. #0)
    return 0x2A1F03E0u | (static_cast<uint32_t>(rd) & 0x1F);
}
constexpr uint32_t MOV_W0_1  = MOV_W(0, 1);   // return true / 1
constexpr uint32_t MOV_W0_0  = MOV_W_ZR(0);   // return false / 0

// Write one ARM64 word to code (mprotect RWX -> memcpy -> restore RX -> flush i-cache).
void write_code(uintptr_t addr, uint32_t word) {
    if (*reinterpret_cast<uint32_t *>(addr) == word) return;
    uintptr_t page = addr & ~0xFFFULL;
    if (mprotect(reinterpret_cast<void *>(page), 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        ALOGW("write_code mprotect failed at %p", reinterpret_cast<void *>(addr));
        return;
    }
    std::memcpy(reinterpret_cast<void *>(addr), &word, sizeof(word));
    mprotect(reinterpret_cast<void *>(page), 0x1000, PROT_READ | PROT_EXEC);
    __builtin___clear_cache(reinterpret_cast<char *>(addr), reinterpret_cast<char *>(addr + 4));
}

// Guarded patch: only writes if the current word equals `expected` (the known original), so a
// wrong/shifted address is skipped instead of corrupting code. Returns true if applied/already-on.
bool patch_instruction(uintptr_t addr, uint32_t expected, uint32_t replacement) {
    uint32_t cur = *reinterpret_cast<uint32_t *>(addr);
    if (cur == replacement) return true;
    if (cur != expected) {
        ALOGW("patch guard failed at %p: got 0x%08x expected 0x%08x",
              reinterpret_cast<void *>(addr), cur, expected);
        return false;
    }
    write_code(addr, replacement);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  2. SYMBOL RESOLVERS (version-independent). Stripped binaries keep RTTI type-info names and
//  __PRETTY_FUNCTION__/assert strings — anchor on those, then walk to the function. RELATIVE
//  relocations are already applied in the mapped image, so vtables/typeinfo are walked by value.
// ═══════════════════════════════════════════════════════════════════════════════

// Decode the target of the first BL within [fn, fn+maxInsn*4); returns absolute addr or 0.
uintptr_t first_bl_target(uintptr_t fn, uintptr_t lo, uintptr_t hi, int maxInsn) {
    for (int i = 0; i < maxInsn; ++i) {
        uintptr_t at = fn + i * 4;
        if (at < lo || at + 4 > hi) break;
        uint32_t insn = *reinterpret_cast<uint32_t *>(at);
        if ((insn & 0xFC000000u) == 0x94000000u) {  // BL
            int32_t off = insn & 0x03FFFFFF;
            if (off & 0x02000000) off |= 0xFC000000;
            uintptr_t t = at + (static_cast<intptr_t>(off) << 2);
            return (t >= lo && t < hi) ? t : 0;
        }
    }
    return 0;
}

// Resolve a function by a UNIQUE string it references (e.g. its own __func__/assert literal).
// Optionally guard on its expected first opcode. Returns absolute address, or 0.
uintptr_t resolve_by_string_xref(const native_utils::ModuleInfo &info, const char *needle,
                                 uint32_t expect_prologue /*0 = no guard*/) {
    uintptr_t s = native_utils::find_string_va(info.rodata_start, info.rodata_end, needle);
    if (!s) return 0;
    auto xrefs = native_utils::find_adrp_add_xrefs(info.text_start, info.text_end, s, 8);
    for (uintptr_t x : xrefs) {
        uintptr_t fn = native_utils::find_function_start(x, 0x1000);
        if (!fn) continue;
        if (expect_prologue == 0 || *reinterpret_cast<uint32_t *>(fn) == expect_prologue) return fn;
    }
    return 0;
}

// Resolve a class vtable by its Itanium RTTI type-info name (e.g. "24CMemoryTamperingDetector",
// where 24 = strlen of the class name). Returns the vtable's slot-0 address, or 0.
uintptr_t resolve_rtti_vtable(const native_utils::ModuleInfo &info, const char *typeinfo_name) {
    uintptr_t nm = native_utils::find_string_va(info.rodata_start, info.rodata_end, typeinfo_name);
    if (!nm) return 0;
    uintptr_t ti8 = native_utils::find_ptr_in_range(info.data_start, info.data_end, nm);  // typeinfo+8 (name ptr)
    if (!ti8) return 0;
    uintptr_t vtti = native_utils::find_ptr_in_range(info.data_start, info.data_end, ti8 - 8);  // vtable-8 (typeinfo slot)
    if (!vtti) return 0;
    return vtti + 8;  // slot 0
}

// Read vtable slot `idx` (its resolved function pointer). RELATIVE reloc already applied.
uintptr_t vtable_slot(uintptr_t vtable, int idx) {
    return *reinterpret_cast<uintptr_t *>(vtable + static_cast<uintptr_t>(idx) * 8);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  3. AUTO-RESOLVING CODE FEATURE FRAMEWORK
//  Each feature = one guarded code patch whose address is resolved at init from a stable anchor.
//  Toggles genuinely apply (write replacement) AND revert (restore captured original). If
//  resolution or the opcode guard fails, the feature no-ops (fail-closed — never corrupts code).
// ═══════════════════════════════════════════════════════════════════════════════
struct CodeFeature {
    const char *name;
    std::atomic<bool> enabled;
    uintptr_t (*resolve)(const native_utils::ModuleInfo &);  // -> ABSOLUTE addr (already guarded) or 0
    int nwords;            // 1 or 2 instructions to overwrite
    uint32_t on[2];        // replacement words when ON
    // runtime state:
    uintptr_t addr;        // resolved absolute addr (0 = unavailable)
    uint32_t orig[2];      // captured originals
    bool captured;
};

void feature_init(CodeFeature &f, const native_utils::ModuleInfo &info) {
    f.addr = 0; f.captured = false;
    uintptr_t a = f.resolve(info);
    if (!a) { ALOGW("feature %s: UNRESOLVED (disabled)", f.name); return; }
    f.addr = a;
    f.orig[0] = *reinterpret_cast<uint32_t *>(a);
    if (f.nwords == 2) f.orig[1] = *reinterpret_cast<uint32_t *>(a + 4);
    f.captured = true;
    ALOGI("feature %s: resolved @%p", f.name, reinterpret_cast<void *>(a));
}

void feature_apply(CodeFeature &f, bool on) {
    if (!f.captured || f.addr == 0) return;
    write_code(f.addr, on ? f.on[0] : f.orig[0]);
    if (f.nwords == 2) write_code(f.addr + 4, on ? f.on[1] : f.orig[1]);
    f.enabled.store(on, std::memory_order_relaxed);
    ALOGI("feature %s -> %s", f.name, on ? "ON" : "OFF");
}

// ── EXAMPLE features (disabled placeholders) ────────────────────────────────────────────────
//  Fill these in for your target, then add them to g_features[]. Two canonical shapes:
//
//  // (A) force a bool getter to return true, resolved by a nearby string:
//  uintptr_t resolve_is_unlocked(const native_utils::ModuleInfo &i) {
//      return resolve_by_string_xref(i, "IsFeatureUnlocked", /*prologue guard*/ 0);
//  }
//  // ... CodeFeature{ "unlock", {true}, resolve_is_unlocked, 2, {MOV_W0_1, A64_RET}, ... }
//
//  // (B) neutralise a detector via RTTI -> vtable -> method -> the check it calls:
//  uintptr_t resolve_detector_predicate(const native_utils::ModuleInfo &i) {
//      uintptr_t vt = resolve_rtti_vtable(i, "20CSomeAntiTamperCheck");
//      if (!vt) return 0;
//      uintptr_t detect = vtable_slot(vt, 2);            // e.g. Detect() at slot 2
//      return first_bl_target(detect, i.text_start, i.text_end, 40);  // the predicate it calls
//  }
//  // ... CodeFeature{ "ac", {true}, resolve_detector_predicate, 2, {MOV_W0_0, A64_RET}, ... }

CodeFeature g_features[] = {
    // (empty in the template — add your resolved features here; the framework above is ready)
};
constexpr int NUM_FEATURES = sizeof(g_features) / sizeof(g_features[0]);

// ═══════════════════════════════════════════════════════════════════════════════
//  ShadowHook smoke-test — proves inline hooking works on this build. Replace with a real
//  hook (shadowhook_hook_func_addr on a resolved code pointer) when you need to intercept,
//  not just patch a constant. (Kept minimal so it links against Maven ShadowHook 2.0.x.)
// ═══════════════════════════════════════════════════════════════════════════════
std::atomic<int> g_getpid_hits{0};
void *g_getpid_stub = nullptr;
void *g_getpid_orig = nullptr;

pid_t getpid_proxy() {
    SHADOWHOOK_STACK_SCOPE();
    g_getpid_hits.fetch_add(1, std::memory_order_relaxed);
    return SHADOWHOOK_CALL_PREV(getpid_proxy);
}

void hook_finished(int error_number, const char *lib_name, const char *sym_name,
                   void *sym_addr, void *new_addr, void *orig_addr, void *) {
    if (error_number == 0) {
        ALOGI("hook ready: %s!%s target=%p", lib_name, sym_name, sym_addr);
    } else {
        ALOGW("hook failed later: %s!%s errno=%d %s", lib_name, sym_name,
              error_number, shadowhook_to_errmsg(error_number));
    }
}

// Hook a RESOLVED code pointer (the real technique for a stripped game function — you can't hook it
// by symbol name because there is none). Pass an address from the resolvers above (base + rva) and
// a proxy that uses SHADOWHOOK_STACK_SCOPE()/SHADOWHOOK_CALL_PREV(). `orig` receives the trampoline
// to call through. Returns the hook stub, or nullptr on failure.
[[maybe_unused]] void *hook_resolved(uintptr_t addr, void *proxy, void **orig) {
    if (!addr || !proxy) return nullptr;
    void *stub = shadowhook_hook_func_addr(reinterpret_cast<void *>(addr), proxy, orig);
    if (stub == nullptr) {
        ALOGW("hook_resolved failed at %p errno=%d %s", reinterpret_cast<void *>(addr),
              shadowhook_get_errno(), shadowhook_to_errmsg(shadowhook_get_errno()));
    }
    return stub;
}

// ── Your target library. Set this to the game's main native lib to auto-resolve g_features[].
//    Left empty in the template (feature resolution is skipped until you set it). ──
constexpr const char *TARGET_LIB = "";  // e.g. "libil2cpp.so" or "libyourgame.so"

void resolve_and_apply_features() {
    if (TARGET_LIB[0] == '\0' || NUM_FEATURES == 0) return;
    native_utils::ModuleInfo info{};
    for (int attempt = 0; attempt < 120; ++attempt) {  // the .so may load after us
        info = native_utils::find_module_info(TARGET_LIB);
        if (info.valid) break;
        usleep(250000);
    }
    if (!info.valid) { ALOGW("target lib %s not found", TARGET_LIB); return; }
    g_module_base.store(info.base, std::memory_order_relaxed);
    for (int i = 0; i < NUM_FEATURES; ++i) {
        feature_init(g_features[i], info);
        feature_apply(g_features[i], g_features[i].enabled.load(std::memory_order_relaxed));
    }
}

void install_once(const std::string &package_name, const std::string &data_dir) {
    ALOGI("native install package=%s dataDir=%s shadowhook=%s",
          package_name.c_str(), data_dir.c_str(), shadowhook_get_version());
    int init_errno = shadowhook_init(SHADOWHOOK_MODE_SHARED, false);
    if (init_errno != SHADOWHOOK_ERRNO_OK) {
        ALOGE("shadowhook not ready: %d %s", init_errno, shadowhook_to_errmsg(init_errno));
        g_install_result.store(init_errno, std::memory_order_relaxed);
        return;
    }
    // Smoke-test hook (safe, returns the original value).
    g_getpid_stub = shadowhook_hook_sym_name_callback_2(
            "libc.so", "getpid", reinterpret_cast<void *>(getpid_proxy), &g_getpid_orig,
            SHADOWHOOK_HOOK_WITH_SHARED_MODE, hook_finished, nullptr);
    ALOGI("getpid hook stub=%p", g_getpid_stub);

    // Resolve + apply the auto-resolving code features against TARGET_LIB (no-op if unset).
    resolve_and_apply_features();

    g_install_result.store(0, std::memory_order_relaxed);
}

std::string jstring_to_string(JNIEnv *env, jstring value) {
    if (value == nullptr) return {};
    const char *chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) return {};
    std::string out(chars);
    env->ReleaseStringUTFChars(value, chars);
    return out;
}

jint native_install_hooks(JNIEnv *env, jclass, jstring package_name, jstring data_dir) {
    std::call_once(g_install_once, install_once, jstring_to_string(env, package_name),
                   jstring_to_string(env, data_dir));
    return g_install_result.load(std::memory_order_relaxed);
}

jstring native_get_shadowhook_records(JNIEnv *env, jclass) {
    std::string out;
    char *records = shadowhook_get_records(SHADOWHOOK_RECORD_ITEM_ALL);
    if (records != nullptr) { out = records; std::free(records); }
    out += "\ngetpid hits=" + std::to_string(g_getpid_hits.load(std::memory_order_relaxed));
    out += "\n--- Code features (auto-resolved) ---";
    for (int i = 0; i < NUM_FEATURES; ++i) {
        out += "\n  " + std::string(g_features[i].name) +
               (g_features[i].captured ? (g_features[i].enabled.load() ? " ON" : " OFF") : " UNRESOLVED");
    }
    return env->NewStringUTF(out.c_str());
}

}  // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
    JNIEnv *env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK || env == nullptr) {
        return JNI_ERR;
    }
    jclass cls = env->FindClass("com/template/lsposed/NativeBridge");
    if (cls == nullptr) return JNI_ERR;
    static JNINativeMethod methods[] = {
            {"nativeInstallHooks", "(Ljava/lang/String;Ljava/lang/String;)I",
             reinterpret_cast<void *>(native_install_hooks)},
            {"nativeGetShadowHookRecords", "()Ljava/lang/String;",
             reinterpret_cast<void *>(native_get_shadowhook_records)},
    };
    if (env->RegisterNatives(cls, methods, sizeof(methods) / sizeof(methods[0])) != JNI_OK) {
        return JNI_ERR;
    }
    if (!native_utils::register_natives(env)) {
        ALOGW("NativeUtils registration failed; utility helpers will return fallbacks");
    }
    return JNI_VERSION_1_6;
}
