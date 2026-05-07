#include "common.h"

#include <shadowhook.h>

#include <atomic>
#include <cmath>
#include <cstring>

#include "../settings.h"

namespace firestone {
namespace {

using IsHeroFn = bool (*)(void *, const void *);
using ApplyDamageShortFn = void (*)(void *, void *, BigDouble, bool, int32_t, const void *);
using ApplyDamageFullFn = void (*)(void *, void *, BigDouble, bool, bool, int32_t, const void *);
using ReceiveDamageFn = BigDouble (*)(void *, BigDouble, void *, const void *);

uintptr_t g_base = 0;
void *g_apply_short_stub = nullptr;
void *g_apply_full_stub = nullptr;
void *g_receive_stub = nullptr;
ApplyDamageShortFn g_apply_short_orig = nullptr;
ApplyDamageFullFn g_apply_full_orig = nullptr;
ReceiveDamageFn g_receive_orig = nullptr;
IsHeroFn g_is_hero = nullptr;
std::atomic<int> g_hits{0};

constexpr double kLethalMantissa = 9.999999999999998;
constexpr float kDefaultLethalExponent = 1000000.0f;
constexpr float kMinLethalExponent = 1000.0f;
constexpr float kMaxLethalExponent = 1000000.0f;

bool ohk_enabled() {
    Settings &s = settings();
    return s.enabled.load(std::memory_order_relaxed) &&
           s.native_hooks.load(std::memory_order_relaxed) &&
           s.one_hit_kill.load(std::memory_order_relaxed);
}

BigDouble lethal_damage() {
    float exponent_value = clamp_multiplier(settings().damage_multiplier.load(std::memory_order_relaxed),
                                            kDefaultLethalExponent,
                                            kMinLethalExponent,
                                            kMaxLethalExponent);
    return BigDouble{kLethalMantissa, static_cast<int64_t>(std::llround(exponent_value))};
}

bool target_is_enemy_logic(void *target_logic) {
    if (target_logic == nullptr || g_is_hero == nullptr) return false;
    void *battle_character = nullptr;
    std::memcpy(&battle_character, reinterpret_cast<uint8_t *>(target_logic) + 0x20, sizeof(battle_character));
    if (battle_character == nullptr) return false;
    bool is_hero = g_is_hero(battle_character, nullptr);
    return !is_hero;
}

void apply_damage_short_proxy(void *thiz, void *target_logic, BigDouble damage,
                              bool force_allow, int32_t hero_code, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (ohk_enabled() && target_is_enemy_logic(target_logic)) {
        BigDouble payload = lethal_damage();
        int hit = g_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 8) ALOGI("OHK ApplyDamage(short) enemy hit #%d target=%p payload=%.3fe%lld",
                                       hit, target_logic, kLethalMantissa,
                                       static_cast<long long>(payload.exponent));
        damage = payload;
    }
    SHADOWHOOK_CALL_PREV(apply_damage_short_proxy, thiz, target_logic, damage, force_allow, hero_code, method);
}

void apply_damage_full_proxy(void *thiz, void *target_logic, BigDouble damage,
                             bool is_critical, bool force_allow, int32_t hero_code, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (ohk_enabled() && target_is_enemy_logic(target_logic)) {
        BigDouble payload = lethal_damage();
        int hit = g_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 8) ALOGI("OHK ApplyDamage(full) enemy hit #%d target=%p payload=%.3fe%lld",
                                       hit, target_logic, kLethalMantissa,
                                       static_cast<long long>(payload.exponent));
        damage = payload;
    }
    SHADOWHOOK_CALL_PREV(apply_damage_full_proxy, thiz, target_logic, damage, is_critical, force_allow, hero_code, method);
}

BigDouble receive_damage_proxy(void *thiz, BigDouble damage, void *attack_result, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (ohk_enabled() && thiz != nullptr && g_is_hero != nullptr && !g_is_hero(thiz, nullptr)) {
        BigDouble payload = lethal_damage();
        int hit = g_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 8) ALOGI("OHK BattleCharacter.ReceiveDamage enemy hit #%d this=%p payload=%.3fe%lld",
                                       hit, thiz, kLethalMantissa,
                                       static_cast<long long>(payload.exponent));
        damage = payload;
    }
    return SHADOWHOOK_CALL_PREV(receive_damage_proxy, thiz, damage, attack_result, method);
}

}  // namespace

bool install_one_hit_kill(uintptr_t il2cpp_base) {
    g_base = il2cpp_base;
    g_is_hero = reinterpret_cast<IsHeroFn>(rva_addr(g_base, 0x29E2610));
    bool ok = true;
    ok &= install_rva_hook(il2cpp_base, 0x27C4E00, reinterpret_cast<void *>(apply_damage_short_proxy),
                           reinterpret_cast<void **>(&g_apply_short_orig), &g_apply_short_stub,
                           "OHK.BattleController.ApplyDamage.short");
    ok &= install_rva_hook(il2cpp_base, 0x27C4E10, reinterpret_cast<void *>(apply_damage_full_proxy),
                           reinterpret_cast<void **>(&g_apply_full_orig), &g_apply_full_stub,
                           "OHK.BattleController.ApplyDamage.full");
    ok &= install_rva_hook(il2cpp_base, 0x29E1AC8, reinterpret_cast<void *>(receive_damage_proxy),
                           reinterpret_cast<void **>(&g_receive_orig), &g_receive_stub,
                           "OHK.BattleCharacter.ReceiveDamage");
    return ok;
}

}  // namespace firestone
