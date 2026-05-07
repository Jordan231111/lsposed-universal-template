#include "common.h"

#include <shadowhook.h>

#include "../settings.h"

namespace firestone {
namespace {

using TimeSetScaleFn = void (*)(float, const void *);
using TimeGetFloatFn = float (*)(const void *);
using TeamApplyWaveFn = float (*)(void *, float, const void *);
using TeamSetWaveFn = void (*)(float, const void *);
using SpineSetTimeScaleFn = void (*)(void *, float, const void *);

void *g_time_set_stub = nullptr;
void *g_fixed_get_stub = nullptr;
void *g_team_apply_stub = nullptr;
void *g_team_set_stub = nullptr;
void *g_spine_set_stub = nullptr;
TimeSetScaleFn g_time_set_orig = nullptr;
TimeGetFloatFn g_fixed_get_orig = nullptr;
TeamApplyWaveFn g_team_apply_orig = nullptr;
TeamSetWaveFn g_team_set_orig = nullptr;
SpineSetTimeScaleFn g_spine_set_orig = nullptr;

float game_multiplier() {
    Settings &s = settings();
    if (!s.enabled.load(std::memory_order_relaxed) || !s.native_hooks.load(std::memory_order_relaxed) ||
        !s.game_speed.load(std::memory_order_relaxed)) {
        return 1.0f;
    }
    return clamp_multiplier(s.game_speed_multiplier.load(std::memory_order_relaxed), 1.0f, 0.25f, 32.0f);
}

float wave_multiplier() {
    Settings &s = settings();
    if (!s.enabled.load(std::memory_order_relaxed) || !s.native_hooks.load(std::memory_order_relaxed) ||
        !s.wave_speed.load(std::memory_order_relaxed)) {
        return 1.0f;
    }
    return clamp_multiplier(s.wave_speed_multiplier.load(std::memory_order_relaxed), 1.0f, 0.25f, 10.0f);
}

void time_set_scale_proxy(float value, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    float mult = game_multiplier();
    SHADOWHOOK_CALL_PREV(time_set_scale_proxy, value * mult, method);
}

float time_get_fixed_delta_proxy(const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    float out = SHADOWHOOK_CALL_PREV(time_get_fixed_delta_proxy, method);
    float mult = game_multiplier();
    return mult == 1.0f ? out : out / mult;
}

float team_apply_wave_proxy(void *thiz, float base_duration, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    float out = SHADOWHOOK_CALL_PREV(team_apply_wave_proxy, thiz, base_duration, method);
    float mult = wave_multiplier();
    return mult == 1.0f ? out : out / mult;
}

void team_set_wave_proxy(float value, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    float mult = wave_multiplier();
    SHADOWHOOK_CALL_PREV(team_set_wave_proxy, value * mult, method);
}

void spine_set_time_scale_proxy(void *thiz, float value, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    float mult = game_multiplier();
    SHADOWHOOK_CALL_PREV(spine_set_time_scale_proxy, thiz, value * mult, method);
}

}  // namespace

bool install_game_speed(uintptr_t il2cpp_base) {
    bool ok = true;
    ok &= install_rva_hook(il2cpp_base, 0x4A0EAA4, reinterpret_cast<void *>(time_set_scale_proxy),
                           reinterpret_cast<void **>(&g_time_set_orig), &g_time_set_stub,
                           "GameSpeed.Time.set_timeScale");
    ok &= install_rva_hook(il2cpp_base, 0x4A0E9F4, reinterpret_cast<void *>(time_get_fixed_delta_proxy),
                           reinterpret_cast<void **>(&g_fixed_get_orig), &g_fixed_get_stub,
                           "GameSpeed.Time.get_fixedDeltaTime");
    ok &= install_rva_hook(il2cpp_base, 0x2AC99DC, reinterpret_cast<void *>(team_apply_wave_proxy),
                           reinterpret_cast<void **>(&g_team_apply_orig), &g_team_apply_stub,
                           "GameSpeed.TeamLogic.ApplyWaveTransitionSpeedModifierToDuration");
    ok &= install_rva_hook(il2cpp_base, 0x2AC9814, reinterpret_cast<void *>(team_set_wave_proxy),
                           reinterpret_cast<void **>(&g_team_set_orig), &g_team_set_stub,
                           "GameSpeed.TeamLogic.set_WaveTransitionSpeedMultiplier");
    ok &= install_rva_hook(il2cpp_base, 0x436AAC8, reinterpret_cast<void *>(spine_set_time_scale_proxy),
                           reinterpret_cast<void **>(&g_spine_set_orig), &g_spine_set_stub,
                           "GameSpeed.Spine.AnimationState.set_TimeScale");
    return ok;
}

}  // namespace firestone
