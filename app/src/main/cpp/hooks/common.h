#pragma once

#include <android/log.h>
#include <cstdint>
#include <string>

#ifndef TEMPLATE_VERBOSE_LOGS
#define TEMPLATE_VERBOSE_LOGS 0
#endif

#define FIRESTONE_LOG_TAG "FirestoneHooks"
#if TEMPLATE_VERBOSE_LOGS
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, FIRESTONE_LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, FIRESTONE_LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, FIRESTONE_LOG_TAG, __VA_ARGS__)
#else
#define ALOGI(...) ((void)0)
#define ALOGW(...) ((void)0)
#define ALOGE(...) ((void)0)
#endif

namespace firestone {

struct BigDouble {
    double mantissa;
    int64_t exponent;
};

struct ObscuredFloat {
    int32_t current_crypto_key;
    int32_t hidden_value;
    uint8_t hidden_value_old_byte4[4];
    bool inited;
    uint8_t pad0[3];
    float fake_value;
    bool fake_value_active;
    uint8_t pad1[3];
};
static_assert(sizeof(ObscuredFloat) == 24, "Unexpected ObscuredFloat ABI");

uintptr_t find_module_base(const char *soname);
void *rva_addr(uintptr_t base, uintptr_t rva);
bool install_rva_hook(uintptr_t base,
                      uintptr_t rva,
                      void *replacement,
                      void **original,
                      void **stub,
                      const char *name);
bool install_rva_intercept(uintptr_t base,
                           uintptr_t rva,
                           void *interceptor,
                           void **stub,
                           const char *name);

float read_obscured_float(const ObscuredFloat *value);
ObscuredFloat make_obscured_float(float value, uintptr_t il2cpp_base);
void write_obscured_float(void *address, float value, uintptr_t il2cpp_base);
float clamp_multiplier(float value, float fallback, float min_value, float max_value);

bool install_easy_wins(uintptr_t il2cpp_base);
bool install_god_mode(uintptr_t il2cpp_base);
bool install_game_speed(uintptr_t il2cpp_base);
bool install_one_hit_kill(uintptr_t il2cpp_base);
bool install_attack_speed(uintptr_t il2cpp_base);

}  // namespace firestone
