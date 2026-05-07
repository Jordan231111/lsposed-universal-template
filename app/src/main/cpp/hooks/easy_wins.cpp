#include "common.h"

#include <shadowhook.h>

#include <atomic>

#include "../settings.h"

namespace firestone {
namespace {

using CentralHaveCurrencyFn = bool (*)(int32_t, double, const void *);
using CurrencyHaveCurrencyFn = bool (*)(void *, double, const void *);

void *g_central_stub = nullptr;
void *g_currency_stub = nullptr;
CentralHaveCurrencyFn g_central_orig = nullptr;
CurrencyHaveCurrencyFn g_currency_orig = nullptr;
std::atomic<int> g_central_hits{0};
std::atomic<int> g_currency_hits{0};

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

bool currency_have_currency_proxy(void *thiz, double requested_cost, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (free_currency_enabled()) {
        int hit = g_currency_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 5) ALOGI("easy-win FreeCurrency Currency.HaveCurrency hit #%d this=%p cost=%.2f", hit, thiz, requested_cost);
        return true;
    }
    return SHADOWHOOK_CALL_PREV(currency_have_currency_proxy, thiz, requested_cost, method);
}

}  // namespace

bool install_easy_wins(uintptr_t il2cpp_base) {
    bool ok = true;
    ok &= install_rva_hook(il2cpp_base, 0x2BA5E48, reinterpret_cast<void *>(central_have_currency_proxy),
                           reinterpret_cast<void **>(&g_central_orig), &g_central_stub,
                           "EasyWin.CentralCurrencyHandler.HaveCurrency");
    ok &= install_rva_hook(il2cpp_base, 0x2BA5ED8, reinterpret_cast<void *>(currency_have_currency_proxy),
                           reinterpret_cast<void **>(&g_currency_orig), &g_currency_stub,
                           "EasyWin.Currency.HaveCurrency");
    if (ok && free_currency_enabled()) {
        ALOGI("easy-win FreeCurrency toggle active; affordability hooks installed");
    }
    return ok;
}

}  // namespace firestone
