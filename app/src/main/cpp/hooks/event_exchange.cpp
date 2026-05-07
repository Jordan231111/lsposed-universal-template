#include "common.h"

#include <shadowhook.h>

#include <atomic>
#include <cstdint>

#include "../settings.h"

namespace firestone {
namespace {

constexpr uintptr_t kIl2CppStringNewRva = 0x2017830;
constexpr uintptr_t kTmpTextSetTextRva = 0x494D5F8;

constexpr uintptr_t kPremiumProductCodeOffset = 0x18;
constexpr uintptr_t kPremiumProductCostOffset = 0x34;
constexpr uintptr_t kInteractionProductOffset = 0x20;
constexpr uintptr_t kInteractionCostTextOffset = 0x48;
[[maybe_unused]] constexpr uintptr_t kInteractionQuantityToExchangeOffset = 0x98;
constexpr uintptr_t kInteractionPremiumProductCodeOffset = 0x9C;

using AnniversaryCanExchangeFn = bool (*)(void *, const void *);
using AnniversaryDataCanExchangeFn = bool (*)(void *, int32_t, const void *);
using AnniversaryDataPayCurrencyFn = void (*)(void *, int32_t, int32_t, const void *);
using AnniversarySetQuantityFn = void (*)(void *, int32_t, const void *);
using AnniversarySetProductValuesFn = void (*)(void *, int32_t, const void *);
using AnniversaryPurchaseItemFn = void (*)(void *, const void *);
using PremiumProductCanPurchaseFn = bool (*)(void *, int32_t, const void *);
using PremiumProductTryPurchaseFn = void (*)(void *, void *, const void *);
using PremiumProductSetCustomCostFn = void (*)(void *, int32_t, const void *);
using AnniversaryExchangeCallbackFn = void (*)(int32_t, int32_t, const void *);
using ExchangeCalendarCurrencyFn = void (*)(void *, bool, int32_t, const void *);
using Il2CppStringNewFn = void *(*)(const char *);
using TmpTextSetTextFn = void (*)(void *, void *, const void *);

uintptr_t g_il2cpp_base = 0;
void *g_can_exchange_stub = nullptr;
void *g_data_can_exchange_stub = nullptr;
void *g_data_pay_currency_stub = nullptr;
void *g_set_quantity_stub = nullptr;
void *g_set_product_values_stub = nullptr;
void *g_purchase_item_stub = nullptr;
void *g_product_can_purchase_stub = nullptr;
void *g_product_try_purchase_stub = nullptr;
void *g_product_set_custom_cost_stub = nullptr;
void *g_anniv_callback_stub = nullptr;
void *g_socket_exchange_stub = nullptr;

AnniversaryCanExchangeFn g_can_exchange_orig = nullptr;
AnniversaryDataCanExchangeFn g_data_can_exchange_orig = nullptr;
AnniversaryDataPayCurrencyFn g_data_pay_currency_orig = nullptr;
AnniversarySetQuantityFn g_set_quantity_orig = nullptr;
AnniversarySetProductValuesFn g_set_product_values_orig = nullptr;
AnniversaryPurchaseItemFn g_purchase_item_orig = nullptr;
PremiumProductCanPurchaseFn g_product_can_purchase_orig = nullptr;
PremiumProductTryPurchaseFn g_product_try_purchase_orig = nullptr;
PremiumProductSetCustomCostFn g_product_set_custom_cost_orig = nullptr;
AnniversaryExchangeCallbackFn g_anniv_callback_orig = nullptr;
ExchangeCalendarCurrencyFn g_socket_exchange_orig = nullptr;

std::atomic<int> g_can_exchange_hits{0};
std::atomic<int> g_data_can_exchange_hits{0};
std::atomic<int> g_data_pay_hits{0};
std::atomic<int> g_ui_zero_hits{0};
std::atomic<int> g_purchase_hits{0};
std::atomic<int> g_can_purchase_hits{0};
std::atomic<int> g_try_purchase_hits{0};
std::atomic<int> g_callback_hits{0};
std::atomic<int> g_socket_hits{0};

bool base_enabled() {
    Settings &s = settings();
    return s.enabled.load(std::memory_order_relaxed) &&
           s.native_hooks.load(std::memory_order_relaxed);
}

bool zero_cost_enabled() {
    Settings &s = settings();
    return base_enabled() &&
           s.free_currency.load(std::memory_order_relaxed) &&
           s.event_exchange_zero_cost.load(std::memory_order_relaxed);
}

bool local_only_enabled() {
    return zero_cost_enabled() &&
           settings().event_exchange_local_only.load(std::memory_order_relaxed);
}

int32_t read_i32(void *base, uintptr_t off, int32_t fallback = 0) {
    if (base == nullptr) return fallback;
    return *reinterpret_cast<int32_t *>(reinterpret_cast<uintptr_t>(base) + off);
}

void write_i32(void *base, uintptr_t off, int32_t value) {
    if (base == nullptr) return;
    *reinterpret_cast<int32_t *>(reinterpret_cast<uintptr_t>(base) + off) = value;
}

void *read_ptr(void *base, uintptr_t off) {
    if (base == nullptr) return nullptr;
    return *reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(base) + off);
}

bool is_anniversary_product_code(int32_t code) {
    return (code >= 69 && code <= 80) || code == 104 || code == 111 || code == 112;
}

bool is_calendar_exchange_product_code(int32_t code) {
    return (code >= 41 && code <= 49) || code == 81 || code == 105 || code == 107 || code == 109;
}

bool is_event_exchange_product_code(int32_t code) {
    return is_anniversary_product_code(code) || is_calendar_exchange_product_code(code);
}

bool product_is_event_exchange(void *product) {
    return is_event_exchange_product_code(read_i32(product, kPremiumProductCodeOffset, -1));
}

void zero_product_cost(void *product) {
    if (product_is_event_exchange(product)) write_i32(product, kPremiumProductCostOffset, 0);
}

void set_tmp_text_zero(void *tmp_text) {
    if (tmp_text == nullptr || g_il2cpp_base == 0) return;
    auto string_new = reinterpret_cast<Il2CppStringNewFn>(rva_addr(g_il2cpp_base, kIl2CppStringNewRva));
    auto set_text = reinterpret_cast<TmpTextSetTextFn>(rva_addr(g_il2cpp_base, kTmpTextSetTextRva));
    if (string_new == nullptr || set_text == nullptr) return;
    void *zero = string_new("0");
    if (zero == nullptr) return;
    set_text(tmp_text, zero, nullptr);
}

void zero_interaction_cost_ui(void *interaction) {
    if (!zero_cost_enabled() || interaction == nullptr) return;
    int32_t code = read_i32(interaction, kInteractionPremiumProductCodeOffset, -1);
    if (!is_anniversary_product_code(code)) return;
    zero_product_cost(read_ptr(interaction, kInteractionProductOffset));
    set_tmp_text_zero(read_ptr(interaction, kInteractionCostTextOffset));
    int hit = g_ui_zero_hits.fetch_add(1, std::memory_order_relaxed) + 1;
    if (hit > 0 && hit <= 10) {
        ALOGI("event exchange zero-cost UI #%d product=%d quantity=%d",
              hit, code, read_i32(interaction, kInteractionQuantityToExchangeOffset, -1));
    }
}

bool anniversary_can_exchange_proxy(void *thiz, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (zero_cost_enabled()) {
        int hit = g_can_exchange_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 10) {
            ALOGI("event exchange CanExchange forced true #%d product=%d",
                  hit, read_i32(thiz, kInteractionPremiumProductCodeOffset, -1));
        }
        return true;
    }
    return SHADOWHOOK_CALL_PREV(anniversary_can_exchange_proxy, thiz, method);
}

bool anniversary_data_can_exchange_proxy(void *thiz, int32_t product_code, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (zero_cost_enabled() && is_anniversary_product_code(product_code)) {
        int hit = g_data_can_exchange_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 10) {
            ALOGI("event exchange DataHandler.CanExchangeCurrency forced true #%d product=%d",
                  hit, product_code);
        }
        return true;
    }
    return SHADOWHOOK_CALL_PREV(anniversary_data_can_exchange_proxy, thiz, product_code, method);
}

void anniversary_data_pay_currency_proxy(void *thiz, int32_t product_code, int32_t quantity, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (zero_cost_enabled() && is_anniversary_product_code(product_code)) {
        int hit = g_data_pay_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 10) {
            ALOGI("event exchange DataHandler.PayCurrency no-spend #%d product=%d quantity=%d",
                  hit, product_code, quantity);
        }
        return;
    }
    SHADOWHOOK_CALL_PREV(anniversary_data_pay_currency_proxy, thiz, product_code, quantity, method);
}

void anniversary_set_quantity_proxy(void *thiz, int32_t exchange_quantity, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    SHADOWHOOK_CALL_PREV(anniversary_set_quantity_proxy, thiz, exchange_quantity, method);
    zero_interaction_cost_ui(thiz);
}

void anniversary_set_product_values_proxy(void *thiz, int32_t product_code, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    SHADOWHOOK_CALL_PREV(anniversary_set_product_values_proxy, thiz, product_code, method);
    zero_interaction_cost_ui(thiz);
}

void anniversary_purchase_item_proxy(void *thiz, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (zero_cost_enabled() && thiz != nullptr) {
        void *product = read_ptr(thiz, kInteractionProductOffset);
        zero_product_cost(product);
        int hit = g_purchase_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 20) {
            ALOGI("event exchange PurchaseItem zero-cost entry #%d product=%d quantity=%d productCost=%d",
                  hit,
                  read_i32(thiz, kInteractionPremiumProductCodeOffset, -1),
                  read_i32(thiz, kInteractionQuantityToExchangeOffset, -1),
                  read_i32(product, kPremiumProductCostOffset, -1));
        }
    }
    SHADOWHOOK_CALL_PREV(anniversary_purchase_item_proxy, thiz, method);
}

bool premium_product_can_purchase_proxy(void *thiz, int32_t quantity, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (zero_cost_enabled() && product_is_event_exchange(thiz)) {
        write_i32(thiz, kPremiumProductCostOffset, 0);
        int hit = g_can_purchase_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 20) {
            ALOGI("event exchange PremiumProduct.CanPurchase forced true #%d product=%d quantity=%d",
                  hit, read_i32(thiz, kPremiumProductCodeOffset, -1), quantity);
        }
        return true;
    }
    return SHADOWHOOK_CALL_PREV(premium_product_can_purchase_proxy, thiz, quantity, method);
}

void premium_product_try_purchase_proxy(void *thiz, void *metadata, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (zero_cost_enabled() && product_is_event_exchange(thiz)) {
        write_i32(thiz, kPremiumProductCostOffset, 0);
        int hit = g_try_purchase_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 20) {
            ALOGI("event exchange PremiumProduct.TryToPurchaseProduct cost=0 #%d product=%d metadata=%p",
                  hit, read_i32(thiz, kPremiumProductCodeOffset, -1), metadata);
        }
    }
    SHADOWHOOK_CALL_PREV(premium_product_try_purchase_proxy, thiz, metadata, method);
}

void premium_product_set_custom_cost_proxy(void *thiz, int32_t custom_cost, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (zero_cost_enabled() && product_is_event_exchange(thiz)) {
        SHADOWHOOK_CALL_PREV(premium_product_set_custom_cost_proxy, thiz, 0, method);
        return;
    }
    SHADOWHOOK_CALL_PREV(premium_product_set_custom_cost_proxy, thiz, custom_cost, method);
}

void anniversary_exchange_callback_proxy(int32_t product_code, int32_t quantity, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (zero_cost_enabled() && is_anniversary_product_code(product_code)) {
        int hit = g_callback_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 20) {
            ALOGI("event exchange AnniversaryEventExchangeCallback #%d product=%d quantity=%d",
                  hit, product_code, quantity);
        }
    }
    SHADOWHOOK_CALL_PREV(anniversary_exchange_callback_proxy, product_code, quantity, method);
}

void exchange_calendar_currency_proxy(void *product_id, bool is_anniversary, int32_t quantity, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (zero_cost_enabled() && is_anniversary) {
        int hit = g_socket_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 20) {
            ALOGI("event exchange socket ExchangeCalendarCurrency #%d productString=%p anniversary=%d quantity=%d localOnly=%d",
                  hit, product_id, is_anniversary ? 1 : 0, quantity, local_only_enabled() ? 1 : 0);
        }
        if (local_only_enabled()) return;
    }
    SHADOWHOOK_CALL_PREV(exchange_calendar_currency_proxy, product_id, is_anniversary, quantity, method);
}

}  // namespace

bool install_event_exchange(uintptr_t il2cpp_base) {
    g_il2cpp_base = il2cpp_base;
    bool ok = true;
    ok &= install_rva_hook(il2cpp_base, 0x29273BC, reinterpret_cast<void *>(anniversary_can_exchange_proxy),
                           reinterpret_cast<void **>(&g_can_exchange_orig), &g_can_exchange_stub,
                           "EventExchange.PremiumProductAnniversaryInteraction.CanExchange");
    ok &= install_rva_hook(il2cpp_base, 0x26536FC, reinterpret_cast<void *>(anniversary_data_can_exchange_proxy),
                           reinterpret_cast<void **>(&g_data_can_exchange_orig), &g_data_can_exchange_stub,
                           "EventExchange.AnniversaryEventDataHandler.CanExchangeCurrency");
    ok &= install_rva_hook(il2cpp_base, 0x2653760, reinterpret_cast<void *>(anniversary_data_pay_currency_proxy),
                           reinterpret_cast<void **>(&g_data_pay_currency_orig), &g_data_pay_currency_stub,
                           "EventExchange.AnniversaryEventDataHandler.PayCurrency");
    ok &= install_rva_hook(il2cpp_base, 0x2927438, reinterpret_cast<void *>(anniversary_set_quantity_proxy),
                           reinterpret_cast<void **>(&g_set_quantity_orig), &g_set_quantity_stub,
                           "EventExchange.PremiumProductAnniversaryInteraction.SetQuantityToExchange");
    ok &= install_rva_hook(il2cpp_base, 0x2926168, reinterpret_cast<void *>(anniversary_set_product_values_proxy),
                           reinterpret_cast<void **>(&g_set_product_values_orig), &g_set_product_values_stub,
                           "EventExchange.PremiumProductAnniversaryInteraction.SetProductValues");
    ok &= install_rva_hook(il2cpp_base, 0x29277AC, reinterpret_cast<void *>(anniversary_purchase_item_proxy),
                           reinterpret_cast<void **>(&g_purchase_item_orig), &g_purchase_item_stub,
                           "EventExchange.PremiumProductAnniversaryInteraction.PurchaseItem");
    ok &= install_rva_hook(il2cpp_base, 0x2923A10, reinterpret_cast<void *>(premium_product_can_purchase_proxy),
                           reinterpret_cast<void **>(&g_product_can_purchase_orig), &g_product_can_purchase_stub,
                           "EventExchange.PremiumProduct.CanPurchase");
    ok &= install_rva_hook(il2cpp_base, 0x29231BC, reinterpret_cast<void *>(premium_product_try_purchase_proxy),
                           reinterpret_cast<void **>(&g_product_try_purchase_orig), &g_product_try_purchase_stub,
                           "EventExchange.PremiumProduct.TryToPurchaseProduct");
    // This setter is only two AArch64 instructions. Shadowhook can usually handle it, but it is
    // not required for the purchase path because TryToPurchaseProduct and CanPurchase are hooked.
    install_rva_hook(il2cpp_base, 0x2923A08, reinterpret_cast<void *>(premium_product_set_custom_cost_proxy),
                     reinterpret_cast<void **>(&g_product_set_custom_cost_orig), &g_product_set_custom_cost_stub,
                     "EventExchange.PremiumProduct.SetCustomCost");
    ok &= install_rva_hook(il2cpp_base, 0x2930CDC, reinterpret_cast<void *>(anniversary_exchange_callback_proxy),
                           reinterpret_cast<void **>(&g_anniv_callback_orig), &g_anniv_callback_stub,
                           "EventExchange.PremiumProductHandler.AnniversaryEventExchangeCallback");
    ok &= install_rva_hook(il2cpp_base, 0x26D3CF0, reinterpret_cast<void *>(exchange_calendar_currency_proxy),
                           reinterpret_cast<void **>(&g_socket_exchange_orig), &g_socket_exchange_stub,
                           "EventExchange.SocketFunctions.ExchangeCalendarCurrency");
    if (ok && zero_cost_enabled()) {
        ALOGI("event exchange zero-cost hooks active; localOnly=%d",
              settings().event_exchange_local_only.load(std::memory_order_relaxed) ? 1 : 0);
    }
    return ok;
}

}  // namespace firestone
