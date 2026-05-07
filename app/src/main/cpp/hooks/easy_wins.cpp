#include "common.h"

#include <shadowhook.h>

#include <atomic>

#include "../settings.h"

namespace firestone {
namespace {

using CentralHaveCurrencyFn = bool (*)(int32_t, double, const void *);
using CentralPayCurrencyFn = void (*)(int32_t, double, const void *);
using CurrencyHaveCurrencyFn = bool (*)(void *, double, const void *);
using CurrencyRemoveCurFn = void (*)(void *, double, const void *);
using BigCurrencyHaveCurrencyFn = bool (*)(void *, const void *, const void *);
using BigCurrencyRemoveCurFn = void (*)(void *, const void *, const void *);

void *g_central_stub = nullptr;
void *g_central_resolved_stub = nullptr;
void *g_central_pay_stub = nullptr;
void *g_currency_stub = nullptr;
void *g_currency_remove_stub = nullptr;
void *g_big_currency_stub = nullptr;
void *g_big_currency_remove_stub = nullptr;
CentralHaveCurrencyFn g_central_orig = nullptr;
CentralHaveCurrencyFn g_central_resolved_orig = nullptr;
CentralPayCurrencyFn g_central_pay_orig = nullptr;
CurrencyHaveCurrencyFn g_currency_orig = nullptr;
CurrencyRemoveCurFn g_currency_remove_orig = nullptr;
BigCurrencyHaveCurrencyFn g_big_currency_orig = nullptr;
BigCurrencyRemoveCurFn g_big_currency_remove_orig = nullptr;
std::atomic<int> g_central_hits{0};
std::atomic<int> g_central_resolved_hits{0};
std::atomic<int> g_central_pay_hits{0};
std::atomic<int> g_currency_hits{0};
std::atomic<int> g_currency_remove_hits{0};
std::atomic<int> g_big_currency_hits{0};
std::atomic<int> g_big_currency_remove_hits{0};

bool free_currency_enabled() {
    Settings &s = settings();
    return s.enabled.load(std::memory_order_relaxed) &&
           s.native_hooks.load(std::memory_order_relaxed) &&
           s.free_currency.load(std::memory_order_relaxed);
}

bool central_have_currency_proxy(int32_t currency, double quantity, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (free_currency_enabled()) {
        int hit = g_central_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 5) ALOGI("easy-win FreeCurrency CentralCurrencyHandler.HaveCurrency hit #%d currency=%d qty=%.2f", hit, currency, quantity);
        return true;
    }
    return SHADOWHOOK_CALL_PREV(central_have_currency_proxy, currency, quantity, method);
}

bool central_have_resolved_currency_proxy(int32_t currency, double quantity, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (free_currency_enabled()) {
        int hit = g_central_resolved_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 5) ALOGI("easy-win FreeCurrency CentralCurrencyHandler.HaveResolvedCurrency hit #%d currency=%d qty=%.2f", hit, currency, quantity);
        return true;
    }
    return SHADOWHOOK_CALL_PREV(central_have_resolved_currency_proxy, currency, quantity, method);
}

void central_pay_currency_proxy(int32_t currency, double quantity, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (free_currency_enabled()) {
        int hit = g_central_pay_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 5) ALOGI("easy-win FreeCurrency CentralCurrencyHandler.PayCurrency no-spend #%d currency=%d qty=%.2f", hit, currency, quantity);
        return;
    }
    SHADOWHOOK_CALL_PREV(central_pay_currency_proxy, currency, quantity, method);
}

bool currency_have_currency_proxy(void *thiz, double requested_cost, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (free_currency_enabled()) {
        int hit = g_currency_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 5) ALOGI("easy-win FreeCurrency Currency.HaveCurrency hit #%d this=%p cost=%.2f", hit, thiz, requested_cost);
        return true;
    }
    return SHADOWHOOK_CALL_PREV(currency_have_currency_proxy, thiz, requested_cost, method);
}

void currency_remove_cur_proxy(void *thiz, double amount, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (free_currency_enabled()) {
        int hit = g_currency_remove_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 5) ALOGI("easy-win FreeCurrency Currency.RemoveCur no-spend #%d this=%p amount=%.2f", hit, thiz, amount);
        return;
    }
    SHADOWHOOK_CALL_PREV(currency_remove_cur_proxy, thiz, amount, method);
}

bool big_currency_have_currency_proxy(void *thiz, const void *requested_cost, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (free_currency_enabled()) {
        int hit = g_big_currency_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 10) ALOGI("easy-win FreeCurrency BigCurrency.HaveCurrency hit #%d this=%p cost_ptr=%p", hit, thiz, requested_cost);
        return true;
    }
    return SHADOWHOOK_CALL_PREV(big_currency_have_currency_proxy, thiz, requested_cost, method);
}

void big_currency_remove_cur_proxy(void *thiz, const void *amount, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (free_currency_enabled()) {
        int hit = g_big_currency_remove_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 10) ALOGI("easy-win FreeCurrency BigCurrency.RemoveCur no-spend #%d this=%p amount_ptr=%p", hit, thiz, amount);
        return;
    }
    SHADOWHOOK_CALL_PREV(big_currency_remove_cur_proxy, thiz, amount, method);
}

}  // namespace

bool install_easy_wins(uintptr_t il2cpp_base) {
    bool ok = true;
    ok &= install_rva_hook(il2cpp_base, 0x2AC2D98, reinterpret_cast<void *>(big_currency_have_currency_proxy),
                           reinterpret_cast<void **>(&g_big_currency_orig), &g_big_currency_stub,
                           "EasyWin.BigCurrency.HaveCurrency");
    ok &= install_rva_hook(il2cpp_base, 0x2AC30F8, reinterpret_cast<void *>(big_currency_remove_cur_proxy),
                           reinterpret_cast<void **>(&g_big_currency_remove_orig), &g_big_currency_remove_stub,
                           "EasyWin.BigCurrency.RemoveCur");
    ok &= install_rva_hook(il2cpp_base, 0x2BA5DB0, reinterpret_cast<void *>(central_pay_currency_proxy),
                           reinterpret_cast<void **>(&g_central_pay_orig), &g_central_pay_stub,
                           "EasyWin.CentralCurrencyHandler.PayCurrency");
    ok &= install_rva_hook(il2cpp_base, 0x2BA5E48, reinterpret_cast<void *>(central_have_currency_proxy),
                           reinterpret_cast<void **>(&g_central_orig), &g_central_stub,
                           "EasyWin.CentralCurrencyHandler.HaveCurrency");
    ok &= install_rva_hook(il2cpp_base, 0x2BA5EE8, reinterpret_cast<void *>(central_have_resolved_currency_proxy),
                           reinterpret_cast<void **>(&g_central_resolved_orig), &g_central_resolved_stub,
                           "EasyWin.CentralCurrencyHandler.HaveResolvedCurrency");
    ok &= install_rva_hook(il2cpp_base, 0x2BA5ED8, reinterpret_cast<void *>(currency_have_currency_proxy),
                           reinterpret_cast<void **>(&g_currency_orig), &g_currency_stub,
                           "EasyWin.Currency.HaveCurrency");
    ok &= install_rva_hook(il2cpp_base, 0x2BA5E3C, reinterpret_cast<void *>(currency_remove_cur_proxy),
                           reinterpret_cast<void **>(&g_currency_remove_orig), &g_currency_remove_stub,
                           "EasyWin.Currency.RemoveCur");
    if (ok && free_currency_enabled()) {
        ALOGI("easy-win FreeCurrency toggle active; affordability + no-spend hooks installed");
    }
    return ok;
}

}  // namespace firestone
