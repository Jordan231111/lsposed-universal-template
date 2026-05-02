#include <jni.h>
#include <android/log.h>
#include <shadowhook.h>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unistd.h>

#include "native_utils.h"

#ifndef TEMPLATE_VERBOSE_LOGS
#define TEMPLATE_VERBOSE_LOGS 0
#endif

#if TEMPLATE_VERBOSE_LOGS
#define LOG_TAG "AppRuntime"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define ALOGI(...) ((void)0)
#define ALOGW(...) ((void)0)
#define ALOGE(...) ((void)0)
#endif

namespace {
std::once_flag g_install_once;
std::atomic<int> g_install_result{7777};
std::atomic<int> g_getpid_hits{0};
void *g_getpid_stub = nullptr;
void *g_getpid_orig = nullptr;

pid_t getpid_proxy() {
    // Shared mode requires this scope so ShadowHook clears its per-call proxy state.
    SHADOWHOOK_STACK_SCOPE();
    g_getpid_hits.fetch_add(1, std::memory_order_relaxed);
    return SHADOWHOOK_CALL_PREV(getpid_proxy);
}

void hook_finished(int error_number,
                   const char *lib_name,
                   const char *sym_name,
                   void *sym_addr,
                   void *new_addr,
                   void *orig_addr,
                   void *arg) {
    if (error_number == 0) {
        ALOGI("hook ready: %s!%s target=%p replacement=%p orig=%p", lib_name, sym_name, sym_addr, new_addr, orig_addr);
    } else {
        ALOGW("hook failed later: %s!%s errno=%d message=%s", lib_name, sym_name,
              error_number, shadowhook_to_errmsg(error_number));
    }
}

void install_once(const std::string &package_name, const std::string &data_dir) {
    ALOGI("native scaffold install package=%s dataDir=%s shadowhook=%s",
          package_name.c_str(), data_dir.c_str(), shadowhook_get_version());

    // Java should initialize first, but calling init again is safe; only the first init takes effect.
    // Avoid newer helper APIs here so this scaffold links cleanly against Maven ShadowHook 2.0.0.
    int init_errno = shadowhook_init(SHADOWHOOK_MODE_SHARED, false);
    if (init_errno != SHADOWHOOK_ERRNO_OK) {
        ALOGE("shadowhook not ready: %d %s", init_errno, shadowhook_to_errmsg(init_errno));
        g_install_result.store(init_errno, std::memory_order_relaxed);
        return;
    }

    // Safe smoke-test hook: observe libc.getpid() and return the original value unchanged.
    // Replace this with your real app-specific native hook after finding stable symbols/addresses with Frida.
    g_getpid_stub = shadowhook_hook_sym_name_callback_2(
            "libc.so",
            "getpid",
            reinterpret_cast<void *>(getpid_proxy),
            &g_getpid_orig,
            SHADOWHOOK_HOOK_WITH_SHARED_MODE,
            hook_finished,
            nullptr);

    int err = shadowhook_get_errno();
    if (g_getpid_stub == nullptr) {
        ALOGE("getpid hook failed: %d %s", err, shadowhook_to_errmsg(err));
        g_install_result.store(err == 0 ? -1 : err, std::memory_order_relaxed);
        return;
    }

    // errno 1 means pending: ShadowHook will install automatically when that ELF appears.
    ALOGI("getpid hook stub=%p status=%d %s", g_getpid_stub, err, shadowhook_to_errmsg(err));
    g_install_result.store(err, std::memory_order_relaxed);
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
    std::string package = jstring_to_string(env, package_name);
    std::string data = jstring_to_string(env, data_dir);
    std::call_once(g_install_once, install_once, package, data);
    return g_install_result.load(std::memory_order_relaxed);
}

jstring native_get_shadowhook_records(JNIEnv *env, jclass) {
    char *records = shadowhook_get_records(SHADOWHOOK_RECORD_ITEM_ALL);
    if (records == nullptr) {
        return env->NewStringUTF("No records or ShadowHook unavailable");
    }
    std::string out(records);
    std::free(records);
    out += "\ngetpid hits=" + std::to_string(g_getpid_hits.load(std::memory_order_relaxed));
    return env->NewStringUTF(out.c_str());
}

} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
    JNIEnv *env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK || env == nullptr) {
        return JNI_ERR;
    }

    jclass cls = env->FindClass("com/template/lsposed/NativeBridge");
    if (cls == nullptr) {
        return JNI_ERR;
    }

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
