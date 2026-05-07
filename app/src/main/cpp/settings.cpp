#include "settings.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#include "hooks/common.h"

namespace firestone {
namespace {

Settings g_settings;
std::once_flag g_thread_once;

std::string read_file(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return {};
    std::string out;
    char buf[1024];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

bool json_bool(const std::string &json, const char *key, bool fallback) {
    std::string needle = "\"";
    needle += key;
    needle += "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return fallback;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n')) ++pos;
    if (json.compare(pos, 4, "true") == 0) return true;
    if (json.compare(pos, 5, "false") == 0) return false;
    return fallback;
}

float json_float(const std::string &json, const char *key, float fallback, float min_v, float max_v) {
    std::string needle = "\"";
    needle += key;
    needle += "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return fallback;
    ++pos;
    char *end = nullptr;
    float value = std::strtof(json.c_str() + pos, &end);
    if (end == json.c_str() + pos || !std::isfinite(value)) return fallback;
    return std::max(min_v, std::min(max_v, value));
}

void apply_json(const std::string &json) {
    if (json.empty()) return;
    Settings &s = settings();
    // The legacy master flag is intentionally ignored. LSPosed Manager disables the module;
    // runtime JSON only controls individual features after hooks are installed.
    s.enabled.store(true, std::memory_order_relaxed);
    s.native_hooks.store(json_bool(json, "native_hooks", true), std::memory_order_relaxed);
    s.free_currency.store(json_bool(json, "free_currency", true), std::memory_order_relaxed);
    s.event_exchange_zero_cost.store(json_bool(json, "event_exchange_zero_cost", true), std::memory_order_relaxed);
    s.event_exchange_local_only.store(json_bool(json, "event_exchange_local_only", false), std::memory_order_relaxed);
    s.god_mode.store(json_bool(json, "god_mode", true), std::memory_order_relaxed);
    s.game_speed.store(json_bool(json, "game_speed", false), std::memory_order_relaxed);
    s.wave_speed.store(json_bool(json, "wave_speed", false), std::memory_order_relaxed);
    s.one_hit_kill.store(json_bool(json, "one_hit_kill", false), std::memory_order_relaxed);
    s.attack_speed.store(json_bool(json, "attack_speed", false), std::memory_order_relaxed);
    s.attack_speed_battle_stat.store(json_bool(json, "attack_speed_battle_stat", true), std::memory_order_relaxed);
    s.attack_speed_idle_timer.store(json_bool(json, "attack_speed_idle_timer", true), std::memory_order_relaxed);
    s.attack_speed_attack_timer.store(json_bool(json, "attack_speed_attack_timer", true), std::memory_order_relaxed);
    s.attack_speed_roster_stat.store(json_bool(json, "attack_speed_roster_stat", true), std::memory_order_relaxed);
    s.slow_enemies.store(json_bool(json, "slow_enemies", false), std::memory_order_relaxed);
    s.game_speed_multiplier.store(json_float(json, "game_speed_multiplier", 2.0f, 0.25f, 10.0f),
                                  std::memory_order_relaxed);
    s.wave_speed_multiplier.store(json_float(json, "wave_speed_multiplier", 2.0f, 0.25f, 10.0f),
                                  std::memory_order_relaxed);
    s.damage_multiplier.store(json_float(json, "damage_multiplier", 1000.0f, 1.0f, 1000000.0f),
                              std::memory_order_relaxed);
    s.attack_speed_multiplier.store(json_float(json, "attack_speed_multiplier", 2.0f, 1.0f, 20.0f),
                                    std::memory_order_relaxed);
    s.enemy_attack_speed_multiplier.store(json_float(json, "enemy_attack_speed_multiplier", 2.0f, 1.0f, 25.0f),
                                          std::memory_order_relaxed);
}

}  // namespace

Settings &settings() {
    return g_settings;
}

void load_settings_once(const std::string &path) {
    apply_json(read_file(path));
}

void start_settings_thread(const std::string &path) {
    std::call_once(g_thread_once, [path]() {
        std::thread([path]() {
            ALOGI("settings refresh thread path=%s", path.c_str());
            while (true) {
                load_settings_once(path);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }).detach();
    });
}

}  // namespace firestone
