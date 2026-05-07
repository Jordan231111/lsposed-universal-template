#include "common.h"

#include <shadowhook.h>

#include <atomic>

#include "../settings.h"

namespace firestone {
namespace {

using IsImmuneFn = bool (*)(void *, const void *);

void *g_immune_stub = nullptr;
IsImmuneFn g_immune_orig = nullptr;
std::atomic<int> g_hits{0};

bool god_mode_enabled() {
    Settings &s = settings();
    return s.enabled.load(std::memory_order_relaxed) &&
           s.native_hooks.load(std::memory_order_relaxed) &&
           s.god_mode.load(std::memory_order_relaxed);
}

bool battle_hero_is_immune_proxy(void *thiz, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (god_mode_enabled()) {
        int hit = g_hits.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit > 0 && hit <= 5) ALOGI("god-mode BattleHero.IsImmune hit #%d this=%p", hit, thiz);
        return true;
    }
    return SHADOWHOOK_CALL_PREV(battle_hero_is_immune_proxy, thiz, method);
}

}  // namespace

bool install_god_mode(uintptr_t il2cpp_base) {
    return install_rva_hook(il2cpp_base, 0x2AC0170, reinterpret_cast<void *>(battle_hero_is_immune_proxy),
                            reinterpret_cast<void **>(&g_immune_orig), &g_immune_stub,
                            "GodMode.BattleHero.IsImmune");
}

}  // namespace firestone
