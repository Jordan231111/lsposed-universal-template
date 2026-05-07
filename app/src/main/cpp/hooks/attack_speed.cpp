#include "common.h"

#include <shadowhook.h>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "../settings.h"

namespace firestone {
namespace {

using FillAttackTimerFn = void (*)(void *, bool, const void *);
using CalcTotalFn = void (*)(void *, const void *);
using IsHeroFn = bool (*)(void *, const void *);

uintptr_t g_base = 0;
void *g_fill_stub = nullptr;
void *g_idle_update_stub = nullptr;
void *g_attack_update_stub = nullptr;
void *g_hero_calc_stub = nullptr;
void *g_enemy_calc_stub = nullptr;
void *g_hero_basic_stub = nullptr;
FillAttackTimerFn g_fill_orig = nullptr;
CalcTotalFn g_idle_update_orig = nullptr;
CalcTotalFn g_attack_update_orig = nullptr;
CalcTotalFn g_hero_calc_orig = nullptr;
CalcTotalFn g_enemy_calc_orig = nullptr;
CalcTotalFn g_hero_basic_orig = nullptr;
IsHeroFn g_is_hero = nullptr;
std::mutex g_stat_lock;
std::unordered_map<void *, float> g_roster_stat_multiplier;
std::atomic<int> g_timer_hits{0};
std::atomic<int> g_idle_timer_hits{0};
std::atomic<int> g_attack_timer_hits{0};
std::atomic<int> g_hero_calc_hits{0};
std::atomic<int> g_enemy_calc_hits{0};
std::atomic<int> g_hero_basic_hits{0};

bool hero_attack_enabled() {
    Settings &s = settings();
    return s.enabled.load(std::memory_order_relaxed) &&
           s.native_hooks.load(std::memory_order_relaxed) &&
           s.attack_speed.load(std::memory_order_relaxed);
}

bool slow_enemies_enabled() {
    Settings &s = settings();
    return s.enabled.load(std::memory_order_relaxed) &&
           s.native_hooks.load(std::memory_order_relaxed) &&
           s.slow_enemies.load(std::memory_order_relaxed);
}

float hero_attack_multiplier() {
    return clamp_multiplier(settings().attack_speed_multiplier.load(std::memory_order_relaxed),
                            2.0f, 1.0f, 20.0f);
}

bool battle_stat_enabled() {
    return hero_attack_enabled() &&
           settings().attack_speed_battle_stat.load(std::memory_order_relaxed);
}

bool idle_timer_enabled() {
    return hero_attack_enabled() &&
           settings().attack_speed_idle_timer.load(std::memory_order_relaxed);
}

bool attack_timer_enabled() {
    return hero_attack_enabled() &&
           settings().attack_speed_attack_timer.load(std::memory_order_relaxed);
}

bool roster_stat_enabled() {
    return hero_attack_enabled() &&
           settings().attack_speed_roster_stat.load(std::memory_order_relaxed);
}

bool plausible_positive_timer(float value) {
    return value > 0.000001f && value < 100000.0f;
}

void *battle_character_from_logic(void *logic) {
    if (logic == nullptr) return nullptr;
    void *battle_character = nullptr;
    std::memcpy(&battle_character, reinterpret_cast<uint8_t *>(logic) + 0x20, sizeof(battle_character));
    return battle_character;
}

void *hero_from_battle_main_hero(void *battle_main_hero) {
    if (battle_main_hero == nullptr) return nullptr;
    void *hero = nullptr;
    std::memcpy(&hero, reinterpret_cast<uint8_t *>(battle_main_hero) + 0x320, sizeof(hero));
    return hero;
}

bool is_hero_character(void *battle_character) {
    if (battle_character == nullptr || g_is_hero == nullptr) return false;
    return g_is_hero(battle_character, nullptr);
}

void remember_roster_stat_multiplier(void *hero, float multiplier) {
    if (hero == nullptr) return;
    std::lock_guard<std::mutex> guard(g_stat_lock);
    g_roster_stat_multiplier[hero] = multiplier;
}

float remembered_roster_stat_multiplier(void *hero) {
    if (hero == nullptr) return 1.0f;
    std::lock_guard<std::mutex> guard(g_stat_lock);
    auto it = g_roster_stat_multiplier.find(hero);
    return it == g_roster_stat_multiplier.end() ? 1.0f : it->second;
}

bool read_obscured_field(void *object, size_t offset, float *out) {
    if (object == nullptr || out == nullptr) return false;
    auto *field = reinterpret_cast<ObscuredFloat *>(reinterpret_cast<uint8_t *>(object) + offset);
    float value = read_obscured_float(field);
    if (value < -0.000001f || value > 100000.0f) return false;
    *out = value;
    return true;
}

bool write_obscured_field(void *object, size_t offset, float value) {
    if (object == nullptr || value < -0.000001f || value > 100000.0f) return false;
    write_obscured_float(reinterpret_cast<uint8_t *>(object) + offset, value, g_base);
    return true;
}

bool divide_attack_interval(void *battle_character, float divisor, float *before, float *after) {
    float current = 0.0f;
    if (!read_obscured_field(battle_character, 0x178, &current) || !plausible_positive_timer(current)) {
        return false;
    }
    float updated = current / divisor;
    if (!plausible_positive_timer(updated)) return false;
    if (!write_obscured_field(battle_character, 0x178, updated)) return false;
    if (before != nullptr) *before = current;
    if (after != nullptr) *after = updated;
    return true;
}

bool multiply_attack_interval(void *battle_character, float multiplier) {
    float current = 0.0f;
    if (!read_obscured_field(battle_character, 0x178, &current) || !plausible_positive_timer(current)) {
        return false;
    }
    float updated = current * multiplier;
    if (!plausible_positive_timer(updated)) return false;
    return write_obscured_field(battle_character, 0x178, updated);
}

bool prime_idle_timer_from_total(void *logic, float multiplier, float *before, float *after) {
    void *battle_character = battle_character_from_logic(logic);
    float total = 0.0f;
    float current = 0.0f;
    if (!read_obscured_field(battle_character, 0x178, &total) || !plausible_positive_timer(total)) {
        return false;
    }
    if (!read_obscured_field(logic, 0x38, &current) || current < -0.000001f || current > 100000.0f) {
        return false;
    }
    float target = total * (1.0f - (1.0f / multiplier));
    if (target < 0.0f) target = 0.0f;
    if (current >= target || target > 100000.0f) return false;
    if (!write_obscured_field(logic, 0x38, target)) return false;
    if (before != nullptr) *before = current;
    if (after != nullptr) *after = target;
    return true;
}

bool boost_attack_action_timer(void *logic, float multiplier, float *before, float *after) {
    float current = 0.0f;
    if (!read_obscured_field(logic, 0x68, &current) || !plausible_positive_timer(current)) {
        return false;
    }
    float updated = current * multiplier;
    if (updated > 60.0f) updated = 60.0f;
    if (updated <= current + 0.0001f) return false;
    if (!write_obscured_field(logic, 0x68, updated)) return false;
    if (before != nullptr) *before = current;
    if (after != nullptr) *after = updated;
    return true;
}

bool divide_plain_float_field(void *object, size_t offset, float divisor, float *before, float *after) {
    if (object == nullptr) return false;
    float current = 0.0f;
    std::memcpy(&current, reinterpret_cast<uint8_t *>(object) + offset, sizeof(current));
    if (!plausible_positive_timer(current)) return false;
    float updated = current / divisor;
    if (!plausible_positive_timer(updated)) return false;
    std::memcpy(reinterpret_cast<uint8_t *>(object) + offset, &updated, sizeof(updated));
    if (before != nullptr) *before = current;
    if (after != nullptr) *after = updated;
    return true;
}

void fill_attack_speed_timer_proxy(void *thiz, bool delay, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    void *battle_character = battle_character_from_logic(thiz);
    bool apply_to_hero = idle_timer_enabled() && is_hero_character(battle_character);
    SHADOWHOOK_CALL_PREV(fill_attack_speed_timer_proxy, thiz, delay, method);
    if (apply_to_hero && thiz != nullptr) {
        float before = 0.0f;
        float after = 0.0f;
        float desired = hero_attack_multiplier();
        prime_idle_timer_from_total(thiz, desired, &before, &after);
        int hit = g_timer_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 12) {
            ALOGI("attack-speed FillAttackSpeedTimer primed #%d logic=%p character=%p target=%.2fx timer %.4f->%.4f",
                  hit, thiz, battle_character, desired, before, after);
        }
    }
}

void idle_update_state_proxy(void *thiz, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    void *battle_character = battle_character_from_logic(thiz);
    bool apply_to_hero = idle_timer_enabled() && is_hero_character(battle_character);
    float before = 0.0f;
    float after = 0.0f;
    bool primed = false;
    if (apply_to_hero) {
        primed = prime_idle_timer_from_total(thiz, hero_attack_multiplier(), &before, &after);
    }
    SHADOWHOOK_CALL_PREV(idle_update_state_proxy, thiz, method);
    if (primed) {
        int hit = g_idle_timer_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 24) {
            ALOGI("attack-speed UpdateIdleState gauge #%d logic=%p character=%p timer %.4f->%.4f",
                  hit, thiz, battle_character, before, after);
        }
    }
}

void attack_update_state_proxy(void *thiz, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    void *battle_character = battle_character_from_logic(thiz);
    bool apply_to_hero = attack_timer_enabled() && is_hero_character(battle_character);
    float before = 0.0f;
    float after = 0.0f;
    bool boosted = false;
    if (apply_to_hero) {
        boosted = boost_attack_action_timer(thiz, hero_attack_multiplier(), &before, &after);
    }
    SHADOWHOOK_CALL_PREV(attack_update_state_proxy, thiz, method);
    if (apply_to_hero) {
        float post_before = 0.0f;
        float post_after = 0.0f;
        bool post_boosted = boost_attack_action_timer(thiz, hero_attack_multiplier(), &post_before, &post_after);
        if (!boosted && post_boosted) {
            before = post_before;
            after = post_after;
            boosted = true;
        }
    }
    if (boosted) {
        int hit = g_attack_timer_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 24) {
            ALOGI("attack-speed UpdateAttackState timer #%d logic=%p character=%p timer %.4f->%.4f",
                  hit, thiz, battle_character, before, after);
        }
    }
}

void hero_calculate_attack_speed_total_proxy(void *thiz, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    SHADOWHOOK_CALL_PREV(hero_calculate_attack_speed_total_proxy, thiz, method);
    if (thiz == nullptr) return;
    if (!hero_attack_enabled()) return;
    float mult = hero_attack_multiplier();
    void *hero = hero_from_battle_main_hero(thiz);
    float stat_scale = remembered_roster_stat_multiplier(hero);
    float before = 0.0f;
    float after = 0.0f;
    bool scaled_here = false;
    if (battle_stat_enabled() && stat_scale <= 1.0001f) {
        scaled_here = divide_attack_interval(thiz, mult, &before, &after);
        if (scaled_here) stat_scale = mult;
    }
    int hit = g_hero_calc_hits.fetch_add(1, std::memory_order_relaxed) + 1;
    if (hit > 0 && hit <= 24) {
        ALOGI("attack-speed BattleMainHero.CalculateAttackSpeedTotal #%d this=%p hero=%p target=%.2fx stat=%.2fx%s %.4f->%.4f",
              hit, thiz, hero, mult, stat_scale, scaled_here ? " scaled" : "", before, after);
    }
}

void enemy_calculate_attack_speed_total_proxy(void *thiz, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    SHADOWHOOK_CALL_PREV(enemy_calculate_attack_speed_total_proxy, thiz, method);
    if (!slow_enemies_enabled() || thiz == nullptr) return;
    float mult = clamp_multiplier(settings().enemy_attack_speed_multiplier.load(std::memory_order_relaxed),
                                  2.0f, 1.0f, 25.0f);
    multiply_attack_interval(thiz, mult);
    int hit = g_enemy_calc_hits.fetch_add(1, std::memory_order_relaxed) + 1;
    if (hit > 0 && hit <= 24) {
        ALOGI("attack-speed BattleMainEnemy.CalculateAttackSpeedTotal slowed this=%p x%.2f", thiz, mult);
    }
}

void hero_calculate_basic_attack_speed_proxy(void *thiz, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    SHADOWHOOK_CALL_PREV(hero_calculate_basic_attack_speed_proxy, thiz, method);
    if (thiz == nullptr) return;
    if (!roster_stat_enabled()) {
        remember_roster_stat_multiplier(thiz, 1.0f);
        return;
    }
    float mult = hero_attack_multiplier();
    float before = 0.0f;
    float after = 0.0f;
    if (!divide_plain_float_field(thiz, 0x3F0, mult, &before, &after)) return;
    remember_roster_stat_multiplier(thiz, mult);
    int hit = g_hero_basic_hits.fetch_add(1, std::memory_order_relaxed) + 1;
    if (hit > 0 && hit <= 24) {
        ALOGI("attack-speed Hero.CalculateBasicAttackSpeed roster #%d hero=%p attackSpeedBasic %.4f->%.4f x%.2f",
              hit, thiz, before, after, mult);
    }
}

}  // namespace

bool install_attack_speed(uintptr_t il2cpp_base) {
    g_base = il2cpp_base;
    g_is_hero = reinterpret_cast<IsHeroFn>(rva_addr(g_base, 0x29E2610));
    bool ok = true;
    ok &= install_rva_hook(il2cpp_base, 0x2AC82E8, reinterpret_cast<void *>(fill_attack_speed_timer_proxy),
                           reinterpret_cast<void **>(&g_fill_orig), &g_fill_stub,
                           "AttackSpeed.CharacterBattleLogic.FillAttackSpeedTimer");
    ok &= install_rva_hook(il2cpp_base, 0x2AC71F0, reinterpret_cast<void *>(idle_update_state_proxy),
                           reinterpret_cast<void **>(&g_idle_update_orig), &g_idle_update_stub,
                           "AttackSpeed.CharacterBattleLogic.UpdateIdleState");
    ok &= install_rva_hook(il2cpp_base, 0x2AC708C, reinterpret_cast<void *>(attack_update_state_proxy),
                           reinterpret_cast<void **>(&g_attack_update_orig), &g_attack_update_stub,
                           "AttackSpeed.CharacterBattleLogic.UpdateAttackState");
    ok &= install_rva_hook(il2cpp_base, 0x2AC2AF8, reinterpret_cast<void *>(hero_calculate_attack_speed_total_proxy),
                           reinterpret_cast<void **>(&g_hero_calc_orig), &g_hero_calc_stub,
                           "AttackSpeed.BattleMainHero.CalculateAttackSpeedTotal");
    ok &= install_rva_hook(il2cpp_base, 0x29E5B94, reinterpret_cast<void *>(enemy_calculate_attack_speed_total_proxy),
                           reinterpret_cast<void **>(&g_enemy_calc_orig), &g_enemy_calc_stub,
                           "AttackSpeed.BattleMainEnemy.CalculateAttackSpeedTotal");
    ok &= install_rva_hook(il2cpp_base, 0x2B34FB0, reinterpret_cast<void *>(hero_calculate_basic_attack_speed_proxy),
                           reinterpret_cast<void **>(&g_hero_basic_orig), &g_hero_basic_stub,
                           "AttackSpeed.Hero.CalculateBasicAttackSpeed");
    return ok;
}

}  // namespace firestone
