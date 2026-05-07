#pragma once

#include <atomic>
#include <string>

namespace firestone {

struct Settings {
    std::atomic<bool> enabled{true};
    std::atomic<bool> native_hooks{true};
    std::atomic<bool> free_currency{true};
    std::atomic<bool> god_mode{true};
    std::atomic<bool> game_speed{false};
    std::atomic<bool> wave_speed{false};
    std::atomic<bool> one_hit_kill{false};
    std::atomic<bool> attack_speed{false};
    std::atomic<bool> attack_speed_battle_stat{true};
    std::atomic<bool> attack_speed_idle_timer{true};
    std::atomic<bool> attack_speed_attack_timer{true};
    std::atomic<bool> attack_speed_roster_stat{true};
    std::atomic<bool> slow_enemies{false};
    std::atomic<float> game_speed_multiplier{2.0f};
    std::atomic<float> wave_speed_multiplier{2.0f};
    std::atomic<float> damage_multiplier{1000.0f};
    std::atomic<float> attack_speed_multiplier{2.0f};
    std::atomic<float> enemy_attack_speed_multiplier{2.0f};
};

Settings &settings();
void load_settings_once(const std::string &path);
void start_settings_thread(const std::string &path);

}  // namespace firestone
