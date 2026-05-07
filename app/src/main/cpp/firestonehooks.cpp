#include <jni.h>
#include <shadowhook.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

#include "hooks/common.h"
#include "native_utils.h"
#include "settings.h"

namespace {

std::once_flag g_install_once;
std::atomic<int> g_install_result{7777};

std::string jstring_to_string(JNIEnv *env, jstring value) {
    if (value == nullptr) return {};
    const char *chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) return {};
    std::string out(chars);
    env->ReleaseStringUTFChars(value, chars);
    return out;
}

void install_worker(std::string package_name, std::string settings_path) {
    ALOGI("install worker package=%s settings=%s shadowhook=%s",
          package_name.c_str(), settings_path.c_str(), shadowhook_get_version());
    firestone::load_settings_once(settings_path);
    firestone::start_settings_thread(settings_path);

    int init_errno = shadowhook_init(SHADOWHOOK_MODE_SHARED, false);
    if (init_errno != SHADOWHOOK_ERRNO_OK && init_errno != SHADOWHOOK_ERRNO_MODE_CONFLICT) {
        ALOGE("shadowhook init failed in native: %d %s", init_errno, shadowhook_to_errmsg(init_errno));
        g_install_result.store(init_errno, std::memory_order_relaxed);
        return;
    }
    shadowhook_set_recordable(true);

    uintptr_t base = 0;
    for (int i = 0; i < 120; ++i) {
        base = firestone::find_module_base("libil2cpp.so");
        if (base != 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    if (base == 0) {
        ALOGE("libil2cpp.so base not found");
        g_install_result.store(-101, std::memory_order_relaxed);
        return;
    }
    ALOGI("libil2cpp.so base=%p", reinterpret_cast<void *>(base));

    bool ok = true;
    ok &= firestone::install_easy_wins(base);
    ok &= firestone::install_event_exchange(base);
    ok &= firestone::install_god_mode(base);
    ok &= firestone::install_game_speed(base);
    ok &= firestone::install_one_hit_kill(base);
    ok &= firestone::install_attack_speed(base);

    g_install_result.store(ok ? 0 : -102, std::memory_order_relaxed);
    ALOGI("native hook install complete result=%d", g_install_result.load(std::memory_order_relaxed));
}

void install_once(const std::string &package_name, const std::string &settings_path) {
    std::thread(install_worker, package_name, settings_path).detach();
    g_install_result.store(0, std::memory_order_relaxed);
}

jint native_install_hooks(JNIEnv *env, jclass, jstring package_name, jstring settings_path) {
    std::string package = jstring_to_string(env, package_name);
    std::string settings = jstring_to_string(env, settings_path);
    std::call_once(g_install_once, install_once, package, settings);
    return g_install_result.load(std::memory_order_relaxed);
}

jstring native_get_shadowhook_records(JNIEnv *env, jclass) {
    char *records = shadowhook_get_records(SHADOWHOOK_RECORD_ITEM_ALL);
    if (records == nullptr) return env->NewStringUTF("No ShadowHook records yet");
    std::string out(records);
    std::free(records);
    return env->NewStringUTF(out.c_str());
}

}  // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
    ALOGI("JNI_OnLoad firestonehooks");
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
        ALOGW("NativeUtils registration failed");
    }
    return JNI_VERSION_1_6;
}
