#include <jni.h>
#include <shadowhook.h>
#include <cstdlib>
#include <string>

#include "native_utils.h"
#include "il2cpp_observer.h"

// Engine-neutral by default. Target features belong in the consuming module; no demonstration
// hooks, guessed function boundaries, automatic byte patches or application offsets are shipped.
namespace {
std::string string_value(JNIEnv* env, jstring value) {
    if (!value) return {};
    const char* text = env->GetStringUTFChars(value, nullptr);
    if (!text) return {};
    std::string result(text);
    env->ReleaseStringUTFChars(value, text);
    return result;
}

jint install(JNIEnv* env, jclass, jstring package, jstring data_dir,
             jboolean il2cpp_enabled, jstring library_name) {
    (void)package;
    (void)data_dir;
    try {
        // Java initializes the same process-wide SHARED instance. Repeating this with the
        // same mode is supported; mixing UNIQUE/SHARED defaults is deliberately avoided.
        const int error = shadowhook_init(SHADOWHOOK_MODE_SHARED, false);
        if (error) return error;
        if (il2cpp_enabled) {
            std::string reason;
            if (!il2cpp::start_observer(string_value(env, library_name), reason)) return -20;
        }
        return 0; // Startup accepted, not proof that a target engine or feature is ready.
    } catch (...) { return -21; }
}

jstring records(JNIEnv* env, jclass) {
    try {
        std::string out = "Native helpers: available\nIL2CPP: " + il2cpp::observer_status();
        char* detail = shadowhook_get_records(SHADOWHOOK_RECORD_ITEM_ALL);
        if (detail) { out += '\n'; out += detail; std::free(detail); }
        return env->NewStringUTF(out.c_str());
    } catch (...) { return env->NewStringUTF("Native status unavailable"); }
}
} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || !env) return JNI_ERR;
    jclass cls = env->FindClass("com/template/lsposed/NativeBridge");
    if (!cls) return JNI_ERR;
    static JNINativeMethod methods[] = {
        {"nativeInstallHooks", "(Ljava/lang/String;Ljava/lang/String;ZLjava/lang/String;)I", reinterpret_cast<void*>(install)},
        {"nativeGetShadowHookRecords", "()Ljava/lang/String;", reinterpret_cast<void*>(records)},
    };
    if (env->RegisterNatives(cls, methods, sizeof(methods) / sizeof(methods[0])) != JNI_OK) return JNI_ERR;
    if (!native_utils::register_natives(env)) return JNI_ERR;
    return JNI_VERSION_1_6;
}
