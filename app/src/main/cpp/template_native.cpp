#include <jni.h>
#include <android/log.h>
#include <shadowhook.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

constexpr const char *kTargetLibrary = "libil2cpp.so";

std::once_flag g_install_once;
std::atomic<int> g_install_result{7777};
std::atomic<int> g_damage_multiplier{1};
std::atomic<int> g_defense_multiplier{1};
std::atomic<bool> g_god_mode{false};
std::atomic<bool> g_free_shop{false};
std::atomic<bool> g_server_integrity_bypass{false};
std::atomic<bool> g_actk_bypass{false};
std::atomic<int> g_integrity_check_observed{0};
std::atomic<int> g_integrity_token_observed{0};

struct Il2CppObject {
    void *klass;
    void *monitor;
};

struct Il2CppClassHead {
    void *image;
    void *gc_desc;
    const char *name;
    const char *namespaze;
};

struct BigInteger {
    int64_t key;
    int64_t hidden_value;
};

using BigIntegerCtorDouble = void (*)(BigInteger *, double, const void *);
BigIntegerCtorDouble g_big_integer_ctor_double{nullptr};

void *g_orig_monster_damage_taken{nullptr};
void *g_orig_params_damage_taken{nullptr};
void *g_orig_player_damage_taken{nullptr};
void *g_orig_player_scaled_damage_taken{nullptr};
void *g_orig_unique_player_damage_taken{nullptr};
void *g_orig_fighter_decrease_hp{nullptr};
void *g_orig_fighter_decrease_hp_without_sp_guard{nullptr};
void *g_orig_shop_master_calc_price{nullptr};
void *g_orig_shop_master_get_price_type{nullptr};
void *g_orig_shop_master_get_is_iap{nullptr};
void *g_orig_purchase_can_purchase{nullptr};
void *g_orig_seasonal_shop_can_purchase{nullptr};
void *g_orig_game_event_can_purchase{nullptr};
void *g_orig_utils_check_if_is_enough{nullptr};
void *g_orig_utils_consume{nullptr};
void *g_orig_soldier_check_ap{nullptr};
void *g_orig_soldier_consume_ap{nullptr};
void *g_orig_rogue_server_code_is_integrity_error{nullptr};
void *g_orig_rogue_server_code_is_success{nullptr};
void *g_orig_server_manager_prepare_integrity_movenext{nullptr};
void *g_orig_server_manager_request_integrity_movenext{nullptr};

enum class TargetSide {
    Unknown,
    Player,
    Enemy,
};

enum : uintptr_t {
    kRvaMonsterParamsGetDamageTakenRate = 0x2ABA9A8,
    kRvaParamsGetDamageTakenRate = 0x2ABBE94,
    kRvaPlayerParamsGetDamageTakenRate = 0x2ABD678,
    kRvaPlayerScaledParamsGetDamageTakenRate = 0x2AC5DB8,
    kRvaUniquePlayerParamsGetDamageTakenRate = 0x2AD7AE0,
    kRvaFighterDecreaseHp = 0x2AB3A38,
    kRvaFighterDecreaseHpWithoutSpGuard = 0x2AB267C,
    kRvaShopMasterCalcPrice = 0x2BC62FC,
    kRvaShopMasterGetPriceType = 0x2BC62D8,
    kRvaShopMasterGetIsIap = 0x2BC55AC,
    kRvaUseCasePurchaseCanPurchase = 0x2FD09B4,
    kRvaUseCaseViewSeasonalShopMenuCanPurchase = 0x3092980,
    kRvaUseCaseGameEventCanPurchase = 0x2F7E92C,
    kRvaUtilsCheckIfIsEnough = 0x3112CF4,
    kRvaUtilsConsume = 0x3112F84,
    kRvaSoldierDataCheckIfApIsEnough = 0x2B53E64,
    kRvaSoldierDataConsume = 0x2B53ED8,
    kRvaBigIntegerCtorDouble = 0x30F0A54,
    // RogueServerCode.get_IsIntegrityError -> bool. Returning false anywhere a server response
    // is mis-tagged as integrity-related lets the cloud-save flow continue down the success path
    // when PIF gives us a passing verdict; if PIF fails this hook becomes a no-op because the
    // value is already false and we leave it alone.
    kRvaRogueServerCodeGetIsIntegrityError = 0x31F7DCC,
    // RogueServerCode.get_IsSuccess -> bool. Force-true only when we're inside a known cloud-save
    // call path tagged via Java -> ServerIntegrityBypass.armForCloudSave(). Keeping the toggle
    // gated avoids accidentally hiding genuine non-integrity errors elsewhere.
    kRvaRogueServerCodeGetIsSuccess = 0x31F7D14,
    // ServerManager.PrepareIntegrityCheck async state machine MoveNext. Hooked only to record
    // that the integrity flow ran; the meaningful bypass is on get_IsIntegrityError below.
    kRvaServerManagerPrepareIntegrityCheckMoveNext = 0x2CB03D8,
    // ServerManager.RequestIntegrityTokenAsync async state machine MoveNext. Same diagnostic
    // purpose as PrepareIntegrityCheck above.
    kRvaServerManagerRequestIntegrityTokenMoveNext = 0x2CB14BC,
};

bool ends_with(const std::string &value, const char *suffix) {
    if (suffix == nullptr) return false;
    size_t suffix_len = std::strlen(suffix);
    return value.size() >= suffix_len
            && value.compare(value.size() - suffix_len, suffix_len, suffix) == 0;
}

bool contains(const char *value, const char *needle) {
    return value != nullptr && needle != nullptr && std::strstr(value, needle) != nullptr;
}

std::string find_loaded_library_path(const char *name, uintptr_t *base_out = nullptr) {
    FILE *f = std::fopen("/proc/self/maps", "r");
    if (f == nullptr) return {};
    char line[1024];
    std::string out;
    uintptr_t best_base = 0;
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        unsigned long map_start = 0;
        unsigned long map_end = 0;
        unsigned long map_offset = 0;
        char perms[5] = {0};
        char path[512] = {0};
        if (std::sscanf(line, "%lx-%lx %4s %lx %*s %*lu %511[^\n]",
                        &map_start, &map_end, perms, &map_offset, path) != 5) {
            continue;
        }
        uintptr_t start = static_cast<uintptr_t>(map_start);
        uintptr_t offset = static_cast<uintptr_t>(map_offset);
        const char *trim = path;
        while (*trim == ' ' || *trim == '\t') ++trim;
        std::string candidate(trim);
        if (!ends_with(candidate, name)) continue;
        if (out.empty()) out = candidate;
        if (offset == 0 || best_base == 0) {
            best_base = start - offset;
            if (offset == 0) break;
        }
    }
    std::fclose(f);
    if (base_out != nullptr) *base_out = best_base;
    return out;
}

uintptr_t find_loaded_library_base(const char *name) {
    uintptr_t base = 0;
    find_loaded_library_path(name, &base);
    return base;
}

[[maybe_unused]] uintptr_t wait_for_loaded_library_base(const char *name, int attempts,
                                                        useconds_t sleep_us) {
    for (int i = 0; i < attempts; ++i) {
        uintptr_t base = find_loaded_library_base(name);
        if (base != 0) return base;
        usleep(sleep_us);
    }
    return 0;
}

bool is_executable_address(uintptr_t address) {
    FILE *f = std::fopen("/proc/self/maps", "r");
    if (f == nullptr) return false;
    char line[1024];
    bool executable = false;
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        unsigned long map_start = 0;
        unsigned long map_end = 0;
        char perms[5] = {0};
        if (std::sscanf(line, "%lx-%lx %4s", &map_start, &map_end, perms) != 3) continue;
        uintptr_t start = static_cast<uintptr_t>(map_start);
        uintptr_t end = static_cast<uintptr_t>(map_end);
        if (address >= start && address < end) {
            executable = std::strchr(perms, 'x') != nullptr;
            break;
        }
    }
    std::fclose(f);
    return executable;
}

int load_multiplier(const std::atomic<int> &value) {
    int current = value.load(std::memory_order_relaxed);
    return std::clamp(current, 1, 100);
}

const char *object_class_name(void *object) {
    if (object == nullptr) return "";
    auto *managed = reinterpret_cast<Il2CppObject *>(object);
    auto *klass = reinterpret_cast<Il2CppClassHead *>(managed->klass);
    if (klass == nullptr || klass->name == nullptr) return "";
    return klass->name;
}

void *read_object_field(void *object, uintptr_t offset) {
    if (object == nullptr) return nullptr;
    return *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(object) + offset);
}

bool read_bool_field(void *object, uintptr_t offset) {
    if (object == nullptr) return false;
    return *reinterpret_cast<uint8_t *>(reinterpret_cast<uint8_t *>(object) + offset) != 0;
}

TargetSide side_from_params(void *params) {
    const char *name = object_class_name(params);
    if (contains(name, "MonsterParams")) return TargetSide::Enemy;
    if (contains(name, "PlayerParams") || contains(name, "PlayerScaledParams")
            || contains(name, "UniquePlayerParams")) {
        return TargetSide::Player;
    }
    if (std::strcmp(name, "Params") == 0) {
        if (read_bool_field(params, 0x1A9)) return TargetSide::Player;
        if (read_bool_field(params, 0x1AA)) return TargetSide::Enemy;
    }
    return TargetSide::Unknown;
}

TargetSide side_from_fighter(void *fighter) {
    const char *name = object_class_name(fighter);
    if (contains(name, "MonsterFighter")) return TargetSide::Enemy;
    if (contains(name, "PlayerFighter") || contains(name, "UniquePlayerFighter")) {
        return TargetSide::Player;
    }
    return side_from_params(read_object_field(fighter, 0x48));
}

float adjusted_damage_taken_rate(float original, TargetSide side) {
    if (!std::isfinite(original)) return original;
    if (side == TargetSide::Player) {
        if (g_god_mode.load(std::memory_order_relaxed)) return 0.0f;
        int defense = load_multiplier(g_defense_multiplier);
        return defense > 1 ? original / static_cast<float>(defense) : original;
    }
    if (side == TargetSide::Enemy) {
        int damage = load_multiplier(g_damage_multiplier);
        return damage > 1 ? original * static_cast<float>(damage) : original;
    }
    return original;
}

BigInteger make_zero_big_integer() {
    BigInteger out{0, 0};
    if (g_big_integer_ctor_double != nullptr) {
        g_big_integer_ctor_double(&out, 0.0, nullptr);
    }
    return out;
}

float proxy_monster_damage_taken(void *self, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    float original = SHADOWHOOK_CALL_PREV(proxy_monster_damage_taken, self, method);
    return adjusted_damage_taken_rate(original, TargetSide::Enemy);
}

float proxy_params_damage_taken(void *self, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    float original = SHADOWHOOK_CALL_PREV(proxy_params_damage_taken, self, method);
    return adjusted_damage_taken_rate(original, side_from_params(self));
}

float proxy_player_damage_taken(void *self, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    float original = SHADOWHOOK_CALL_PREV(proxy_player_damage_taken, self, method);
    return adjusted_damage_taken_rate(original, TargetSide::Player);
}

float proxy_player_scaled_damage_taken(void *self, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    float original = SHADOWHOOK_CALL_PREV(proxy_player_scaled_damage_taken, self, method);
    return adjusted_damage_taken_rate(original, TargetSide::Player);
}

float proxy_unique_player_damage_taken(void *self, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    float original = SHADOWHOOK_CALL_PREV(proxy_unique_player_damage_taken, self, method);
    return adjusted_damage_taken_rate(original, TargetSide::Player);
}

void proxy_fighter_decrease_hp(void *self, BigInteger value, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (g_god_mode.load(std::memory_order_relaxed)
            && side_from_fighter(self) == TargetSide::Player) {
        return;
    }
    SHADOWHOOK_CALL_PREV(proxy_fighter_decrease_hp, self, value, method);
}

void proxy_fighter_decrease_hp_without_sp_guard(void *self, BigInteger value, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (g_god_mode.load(std::memory_order_relaxed)
            && side_from_fighter(self) == TargetSide::Player) {
        return;
    }
    SHADOWHOOK_CALL_PREV(proxy_fighter_decrease_hp_without_sp_guard, self, value, method);
}

BigInteger proxy_shop_master_calc_price(void *self, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (g_free_shop.load(std::memory_order_relaxed)) return make_zero_big_integer();
    return SHADOWHOOK_CALL_PREV(proxy_shop_master_calc_price, self, method);
}

int proxy_shop_master_get_price_type(void *self, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (g_free_shop.load(std::memory_order_relaxed)) return 13; // CurrencyType.Free
    return SHADOWHOOK_CALL_PREV(proxy_shop_master_get_price_type, self, method);
}

bool proxy_shop_master_get_is_iap(void *self, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (g_free_shop.load(std::memory_order_relaxed)) return false;
    return SHADOWHOOK_CALL_PREV(proxy_shop_master_get_is_iap, self, method);
}

bool proxy_purchase_can_purchase(void *self, void *master, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (g_free_shop.load(std::memory_order_relaxed)) return true;
    return SHADOWHOOK_CALL_PREV(proxy_purchase_can_purchase, self, master, method);
}

bool proxy_seasonal_shop_can_purchase(void *self, void *master, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (g_free_shop.load(std::memory_order_relaxed)) return true;
    return SHADOWHOOK_CALL_PREV(proxy_seasonal_shop_can_purchase, self, master, method);
}

bool proxy_game_event_can_purchase(void *self, int shop_id, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (g_free_shop.load(std::memory_order_relaxed)) return true;
    return SHADOWHOOK_CALL_PREV(proxy_game_event_can_purchase, self, shop_id, method);
}

bool proxy_utils_check_if_is_enough(void *game_mode, int currency_type,
                                    BigInteger cost, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (g_free_shop.load(std::memory_order_relaxed)) return true;
    return SHADOWHOOK_CALL_PREV(proxy_utils_check_if_is_enough,
                                game_mode, currency_type, cost, method);
}

void proxy_utils_consume(void *game_mode, int currency_type, BigInteger cost, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (g_free_shop.load(std::memory_order_relaxed)) return;
    SHADOWHOOK_CALL_PREV(proxy_utils_consume, game_mode, currency_type, cost, method);
}

bool proxy_soldier_check_ap(void *self, int cost, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (g_free_shop.load(std::memory_order_relaxed)) return true;
    return SHADOWHOOK_CALL_PREV(proxy_soldier_check_ap, self, cost, method);
}

bool proxy_soldier_consume_ap(void *self, int cost, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (g_free_shop.load(std::memory_order_relaxed)) return true;
    return SHADOWHOOK_CALL_PREV(proxy_soldier_consume_ap, self, cost, method);
}

using RogueServerCodeIsIntegrityErrorFn = bool (*)(void *, const void *);
using RogueServerCodeIsSuccessFn = bool (*)(void *, const void *);

// RogueServerCode.get_IsIntegrityError() override.
//
// When ENABLE_SERVER_INTEGRITY_BYPASS is on, we return false unconditionally so the cloud-save
// flow does not surface the "Failed to register transfer data" toast on a passing verdict that
// got mis-tagged. When the toggle is off we forward to the original implementation.
bool proxy_rogue_server_code_is_integrity_error(void *self, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (g_server_integrity_bypass.load(std::memory_order_relaxed)) {
        return false;
    }
    return SHADOWHOOK_CALL_PREV(proxy_rogue_server_code_is_integrity_error, self, method);
}

// RogueServerCode.get_IsSuccess() override.
//
// When the bypass is enabled and the original code is an integrity-related error, force the
// success result so the cloud-save flow short-circuits past the "Failed to register transfer
// data" popup. Non-integrity errors still pass through unchanged so genuine server errors stay
// visible.
bool proxy_rogue_server_code_is_success(void *self, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    bool original = SHADOWHOOK_CALL_PREV(proxy_rogue_server_code_is_success, self, method);
    if (original) return true;
    if (!g_server_integrity_bypass.load(std::memory_order_relaxed)) return original;
    auto integrity_fn = reinterpret_cast<RogueServerCodeIsIntegrityErrorFn>(
            g_orig_rogue_server_code_is_integrity_error);
    if (integrity_fn != nullptr && integrity_fn(self, method)) {
        return true;
    }
    return original;
}

// ServerManager.<PrepareIntegrityCheck>d__113.MoveNext - diagnostic counter only.
//
// Hooking the state machine MoveNext is intentionally read-only because the awaitable contract
// requires the original logic to run to completion. The counter helps tell whether the flow
// reached this point at all when debugging cloud-save attempts.
void proxy_server_manager_prepare_integrity_movenext(void *self, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    g_integrity_check_observed.fetch_add(1, std::memory_order_relaxed);
    SHADOWHOOK_CALL_PREV(proxy_server_manager_prepare_integrity_movenext, self, method);
}

void proxy_server_manager_request_integrity_movenext(void *self, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    g_integrity_token_observed.fetch_add(1, std::memory_order_relaxed);
    SHADOWHOOK_CALL_PREV(proxy_server_manager_request_integrity_movenext, self, method);
}

struct HookSpec {
    const char *name;
    uintptr_t rva;
    void *proxy;
    void **original;
};

bool install_hook(uintptr_t base, const HookSpec &spec) {
    uintptr_t address = base + spec.rva;
    if (!is_executable_address(address)) {
        ALOGW("skip non-executable hook target %s @ %p", spec.name,
              reinterpret_cast<void *>(address));
        return false;
    }
    void *stub = shadowhook_hook_func_addr_2(
            reinterpret_cast<void *>(address),
            spec.proxy,
            spec.original,
            SHADOWHOOK_HOOK_WITH_SHARED_MODE | SHADOWHOOK_HOOK_RECORD,
            kTargetLibrary,
            spec.name);
    if (stub == nullptr) {
        int err = shadowhook_get_errno();
        ALOGW("hook failed %s rva=0x%zx err=%d %s", spec.name, spec.rva, err,
              shadowhook_to_errmsg(err));
        return false;
    }
    ALOGI("hooked %s rva=0x%zx stub=%p", spec.name, spec.rva, stub);
    return true;
}

void log_recovered_state() {
    ALOGI("recovered feature state: damage=%d defense=%d god=%d free_shop=%d "
          "server_integrity_bypass=%d actk_bypass=%d integrity_check_obs=%d token_obs=%d",
          g_damage_multiplier.load(std::memory_order_relaxed),
          g_defense_multiplier.load(std::memory_order_relaxed),
          g_god_mode.load(std::memory_order_relaxed) ? 1 : 0,
          g_free_shop.load(std::memory_order_relaxed) ? 1 : 0,
          g_server_integrity_bypass.load(std::memory_order_relaxed) ? 1 : 0,
          g_actk_bypass.load(std::memory_order_relaxed) ? 1 : 0,
          g_integrity_check_observed.load(std::memory_order_relaxed),
          g_integrity_token_observed.load(std::memory_order_relaxed));
    ALOGI("old module feature numbers: 0=damage, 1=defense, 2=god mode, 3=free shop");
}

[[maybe_unused]] int install_recovered_hooks(uintptr_t base) {
    g_big_integer_ctor_double = reinterpret_cast<BigIntegerCtorDouble>(
            base + kRvaBigIntegerCtorDouble);
    if (!is_executable_address(reinterpret_cast<uintptr_t>(g_big_integer_ctor_double))) {
        ALOGW("BigInteger(double) constructor target is not executable");
        g_big_integer_ctor_double = nullptr;
    }

    HookSpec hooks[] = {
            {"MonsterParams.get_damageTakenRate", kRvaMonsterParamsGetDamageTakenRate,
             reinterpret_cast<void *>(proxy_monster_damage_taken), &g_orig_monster_damage_taken},
            {"Params.get_damageTakenRate", kRvaParamsGetDamageTakenRate,
             reinterpret_cast<void *>(proxy_params_damage_taken), &g_orig_params_damage_taken},
            {"PlayerParams.get_damageTakenRate", kRvaPlayerParamsGetDamageTakenRate,
             reinterpret_cast<void *>(proxy_player_damage_taken), &g_orig_player_damage_taken},
            {"PlayerScaledParams.get_damageTakenRate", kRvaPlayerScaledParamsGetDamageTakenRate,
             reinterpret_cast<void *>(proxy_player_scaled_damage_taken),
             &g_orig_player_scaled_damage_taken},
            {"UniquePlayerParams.get_damageTakenRate", kRvaUniquePlayerParamsGetDamageTakenRate,
             reinterpret_cast<void *>(proxy_unique_player_damage_taken),
             &g_orig_unique_player_damage_taken},
            {"Fighter.DecreaseHp", kRvaFighterDecreaseHp,
             reinterpret_cast<void *>(proxy_fighter_decrease_hp), &g_orig_fighter_decrease_hp},
            {"Fighter.DecreaseHpWithoutSpGuard", kRvaFighterDecreaseHpWithoutSpGuard,
             reinterpret_cast<void *>(proxy_fighter_decrease_hp_without_sp_guard),
             &g_orig_fighter_decrease_hp_without_sp_guard},
            {"ShopMaster.CalcPrice", kRvaShopMasterCalcPrice,
             reinterpret_cast<void *>(proxy_shop_master_calc_price), &g_orig_shop_master_calc_price},
            {"ShopMaster.GetPriceType", kRvaShopMasterGetPriceType,
             reinterpret_cast<void *>(proxy_shop_master_get_price_type),
             &g_orig_shop_master_get_price_type},
            {"ShopMaster.get_IsIAP", kRvaShopMasterGetIsIap,
             reinterpret_cast<void *>(proxy_shop_master_get_is_iap), &g_orig_shop_master_get_is_iap},
            {"UseCase_Purchase.CanPurchase", kRvaUseCasePurchaseCanPurchase,
             reinterpret_cast<void *>(proxy_purchase_can_purchase), &g_orig_purchase_can_purchase},
            {"UseCase_ViewSeasonalShopMenu.CanPurchase",
             kRvaUseCaseViewSeasonalShopMenuCanPurchase,
             reinterpret_cast<void *>(proxy_seasonal_shop_can_purchase),
             &g_orig_seasonal_shop_can_purchase},
            {"UseCase_GameEvent.CanPurchase", kRvaUseCaseGameEventCanPurchase,
             reinterpret_cast<void *>(proxy_game_event_can_purchase), &g_orig_game_event_can_purchase},
            {"Utils.CheckIfIsEnough", kRvaUtilsCheckIfIsEnough,
             reinterpret_cast<void *>(proxy_utils_check_if_is_enough),
             &g_orig_utils_check_if_is_enough},
            {"Utils.Consume", kRvaUtilsConsume,
             reinterpret_cast<void *>(proxy_utils_consume), &g_orig_utils_consume},
            {"SoldierData.CheckIfApIsEnough", kRvaSoldierDataCheckIfApIsEnough,
             reinterpret_cast<void *>(proxy_soldier_check_ap), &g_orig_soldier_check_ap},
            {"SoldierData.Consume", kRvaSoldierDataConsume,
             reinterpret_cast<void *>(proxy_soldier_consume_ap), &g_orig_soldier_consume_ap},
            {"RogueServerCode.get_IsIntegrityError", kRvaRogueServerCodeGetIsIntegrityError,
             reinterpret_cast<void *>(proxy_rogue_server_code_is_integrity_error),
             &g_orig_rogue_server_code_is_integrity_error},
            {"RogueServerCode.get_IsSuccess", kRvaRogueServerCodeGetIsSuccess,
             reinterpret_cast<void *>(proxy_rogue_server_code_is_success),
             &g_orig_rogue_server_code_is_success},
            {"ServerManager.<PrepareIntegrityCheck>.MoveNext",
             kRvaServerManagerPrepareIntegrityCheckMoveNext,
             reinterpret_cast<void *>(proxy_server_manager_prepare_integrity_movenext),
             &g_orig_server_manager_prepare_integrity_movenext},
            {"ServerManager.<RequestIntegrityTokenAsync>.MoveNext",
             kRvaServerManagerRequestIntegrityTokenMoveNext,
             reinterpret_cast<void *>(proxy_server_manager_request_integrity_movenext),
             &g_orig_server_manager_request_integrity_movenext},
    };

    int installed = 0;
    for (const HookSpec &hook : hooks) {
        if (install_hook(base, hook)) ++installed;
    }
    return installed;
}

void install_once(const std::string &package_name, const std::string &data_dir) {
    ALOGI("native recovered install package=%s dataDir=%s shadowhook=%s",
          package_name.c_str(), data_dir.c_str(), shadowhook_get_version());

#if !defined(__aarch64__)
    ALOGW("native recovered hooks currently require arm64");
    g_install_result.store(-30, std::memory_order_relaxed);
    return;
#else
    int init_errno = shadowhook_init(SHADOWHOOK_MODE_SHARED, false);
    if (init_errno != SHADOWHOOK_ERRNO_OK) {
        ALOGE("shadowhook not ready: %d %s", init_errno, shadowhook_to_errmsg(init_errno));
        g_install_result.store(init_errno, std::memory_order_relaxed);
        return;
    }

    uintptr_t il2cpp_base = wait_for_loaded_library_base(kTargetLibrary, 120, 100 * 1000);
    if (il2cpp_base == 0) {
        ALOGW("%s was not loaded before timeout", kTargetLibrary);
        g_install_result.store(-20, std::memory_order_relaxed);
        return;
    }

    ALOGI("%s base=%p", kTargetLibrary, reinterpret_cast<void *>(il2cpp_base));
    int installed = install_recovered_hooks(il2cpp_base);
    if (installed <= 0) {
        g_install_result.store(-22, std::memory_order_relaxed);
        return;
    }

    ALOGI("installed %d recovered hooks", installed);
    log_recovered_state();
    g_install_result.store(0, std::memory_order_relaxed);
#endif
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

void native_sync_feature_state(JNIEnv *, jclass, jint damage, jint defense,
                               jboolean god_mode, jboolean free_shop,
                               jboolean server_integrity_bypass, jboolean actk_bypass) {
    g_damage_multiplier.store(damage < 1 ? 1 : damage, std::memory_order_relaxed);
    g_defense_multiplier.store(defense < 1 ? 1 : defense, std::memory_order_relaxed);
    g_god_mode.store(god_mode == JNI_TRUE, std::memory_order_relaxed);
    g_free_shop.store(free_shop == JNI_TRUE, std::memory_order_relaxed);
    g_server_integrity_bypass.store(server_integrity_bypass == JNI_TRUE, std::memory_order_relaxed);
    g_actk_bypass.store(actk_bypass == JNI_TRUE, std::memory_order_relaxed);
    log_recovered_state();
}

jstring native_get_shadowhook_records(JNIEnv *env, jclass) {
    char *records = shadowhook_get_records(SHADOWHOOK_RECORD_ITEM_ALL);
    std::string out;
    if (records != nullptr) {
        out.assign(records);
        std::free(records);
    }
    if (out.empty()) out = "No ShadowHook records yet";
    out += "\ninstall_result=" + std::to_string(g_install_result.load(std::memory_order_relaxed));
    out += "\ndamage=" + std::to_string(g_damage_multiplier.load(std::memory_order_relaxed));
    out += "\ndefense=" + std::to_string(g_defense_multiplier.load(std::memory_order_relaxed));
    out += "\ngod=" + std::string(g_god_mode.load(std::memory_order_relaxed) ? "1" : "0");
    out += "\nfree_shop=" + std::string(g_free_shop.load(std::memory_order_relaxed) ? "1" : "0");
    out += "\nserver_integrity_bypass="
            + std::string(g_server_integrity_bypass.load(std::memory_order_relaxed) ? "1" : "0");
    out += "\nactk_bypass="
            + std::string(g_actk_bypass.load(std::memory_order_relaxed) ? "1" : "0");
    out += "\nintegrity_check_observed="
            + std::to_string(g_integrity_check_observed.load(std::memory_order_relaxed));
    out += "\nintegrity_token_observed="
            + std::to_string(g_integrity_token_observed.load(std::memory_order_relaxed));
    return env->NewStringUTF(out.c_str());
}

} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
    JNIEnv *env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK || env == nullptr) {
        return JNI_ERR;
    }

    jclass cls = env->FindClass("com/jordan/rogue/recovery/NativeBridge");
    if (cls == nullptr) {
        return JNI_ERR;
    }

    static JNINativeMethod methods[] = {
            {"nativeInstallHooks", "(Ljava/lang/String;Ljava/lang/String;)I",
             reinterpret_cast<void *>(native_install_hooks)},
            {"nativeSyncFeatureState", "(IIZZZZ)V",
             reinterpret_cast<void *>(native_sync_feature_state)},
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
