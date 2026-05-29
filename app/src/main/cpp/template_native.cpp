#include <jni.h>
#include <android/log.h>
#include <shadowhook.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <dlfcn.h>
#include <elf.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
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
constexpr int kInstallPending = 7777;

std::once_flag g_install_once;
std::once_flag g_il2cpp_metadata_install_once;
std::atomic<int> g_install_result{kInstallPending};
std::atomic<int> g_damage_multiplier{1};
std::atomic<int> g_defense_multiplier{1};
std::atomic<bool> g_god_mode{false};
std::atomic<bool> g_free_shop{false};
std::atomic<bool> g_server_integrity_bypass{false};
std::atomic<bool> g_actk_bypass{false};
std::atomic<bool> g_forge_backup_success{false};
// Game speed multiplier. 1.0 = original speed (no override). The slider in the floating panel
// writes here on every onProgressChanged tick, so dragging the slider feeds the value through to
// the IL2CPP `UseCase_SwitchGameSpeed.speedUpValue` field in realtime.
std::atomic<float> g_game_speed_multiplier{1.0f};
// Live pointer to the singleton instance of UseCase_SwitchGameSpeed, captured from SetupNewMode.
// The realtime slider writes the resolved speedUpValue field through this pointer so the game's
// update loop picks up the new multiplier on its next frame.
std::atomic<void *> g_switch_game_speed_instance{nullptr};
std::atomic<bool> g_launch_game_speed_applied{false};
std::atomic<int> g_integrity_check_observed{0};
std::atomic<int> g_integrity_token_observed{0};
std::atomic<int> g_integrity_project_number_injected{0};
std::atomic<int> g_backup_success_forged{0};
std::atomic<int> g_verify_backup_success_forged{0};
std::atomic<void *> g_rogue_server_code_success{nullptr};
// Captured user-typed values from ServerManager.<VerifyBackupKeyAsync>d__119.MoveNext.
// VerifyBackup.OnSuccess uses these as the forged msg so the calling code can proceed with the
// correct PlayFab userid.
std::atomic<void *> g_last_verify_playfab_id{nullptr};
std::atomic<void *> g_last_verify_backup_key{nullptr};
using Il2CppStringNewFn = void *(*)(const char *);
Il2CppStringNewFn g_il2cpp_string_new{nullptr};
uintptr_t g_il2cpp_base{0};
std::string g_il2cpp_path;
void *g_orig_il2cpp_init{nullptr};
void *g_orig_il2cpp_init_utf16{nullptr};
void *g_orig_il2cpp_runtime_invoke{nullptr};

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

struct Il2CppString {
    void *klass;
    void *monitor;
    int32_t length;
    char16_t chars[0];
};

struct Il2CppByteArray {
    void *klass;
    void *monitor;
    void *bounds;
    uintptr_t max_length;
    uint8_t vector[0];
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
void *g_orig_prepare_integrity_on_success{nullptr};
void *g_orig_playfab_execute_function{nullptr};
void *g_orig_playfab_instance_execute_function{nullptr};
void *g_orig_playfab_http_make_api_call_object{nullptr};
void *g_orig_playfab_unity_make_api_call{nullptr};
void *g_orig_playfab_unity_post{nullptr};
void *g_orig_playfab_unity_on_response{nullptr};
void *g_orig_playfab_unity_on_error{nullptr};
void *g_orig_playfab_http_on_api_result{nullptr};
void *g_orig_integrity_manager_request_token{nullptr};
void *g_orig_integrity_token_request_ctor{nullptr};
void *g_orig_integrity_token_response_ctor{nullptr};
void *g_orig_issue_backup_key_on_success{nullptr};
void *g_orig_issue_backup_key_on_error{nullptr};
void *g_orig_verify_backup_key_on_success{nullptr};
void *g_orig_verify_backup_key_on_error{nullptr};
void *g_orig_rogue_server_code_ctor{nullptr};
void *g_orig_server_manager_verify_backup_key_movenext{nullptr};
void *g_orig_reflect_speed_up{nullptr};
void *g_orig_switch_game_speed_setup{nullptr};
void *g_orig_unity_time_set_time_scale{nullptr};

enum class TargetSide {
    Unknown,
    Player,
    Enemy,
};

struct Il2CppMethodInfoHead {
    void *methodPointer;
    void *virtualMethodPointer;
};

using Il2CppDomainGetFn = void *(*)();
using Il2CppDomainGetAssembliesFn = void **(*)(void *, size_t *);
using Il2CppAssemblyGetImageFn = void *(*)(void *);
using Il2CppImageGetNameFn = const char *(*)(void *);
using Il2CppImageGetClassCountFn = size_t (*)(const void *);
using Il2CppImageGetClassFn = void *(*)(const void *, size_t);
using Il2CppClassFromNameFn = void *(*)(const void *, const char *, const char *);
using Il2CppClassGetNameFn = const char *(*)(void *);
using Il2CppClassGetNamespaceFn = const char *(*)(void *);
using Il2CppClassGetDeclaringTypeFn = void *(*)(void *);
using Il2CppClassGetMethodFromNameFn = void *(*)(void *, const char *, int);
using Il2CppClassGetMethodsFn = void *(*)(void *, void **);
using Il2CppMethodGetNameFn = const char *(*)(void *);
using Il2CppMethodGetParamCountFn = uint32_t (*)(void *);
using Il2CppClassGetFieldFromNameFn = void *(*)(void *, const char *);
using Il2CppClassGetFieldsFn = void *(*)(void *, void **);
using Il2CppFieldGetNameFn = const char *(*)(void *);
using Il2CppFieldGetOffsetFn = size_t (*)(void *);

struct Il2CppApi {
    Il2CppDomainGetFn domain_get{nullptr};
    Il2CppDomainGetAssembliesFn domain_get_assemblies{nullptr};
    Il2CppAssemblyGetImageFn assembly_get_image{nullptr};
    Il2CppImageGetNameFn image_get_name{nullptr};
    Il2CppImageGetClassCountFn image_get_class_count{nullptr};
    Il2CppImageGetClassFn image_get_class{nullptr};
    Il2CppClassFromNameFn class_from_name{nullptr};
    Il2CppClassGetNameFn class_get_name{nullptr};
    Il2CppClassGetNamespaceFn class_get_namespace{nullptr};
    Il2CppClassGetDeclaringTypeFn class_get_declaring_type{nullptr};
    Il2CppClassGetMethodFromNameFn class_get_method_from_name{nullptr};
    Il2CppClassGetMethodsFn class_get_methods{nullptr};
    Il2CppMethodGetNameFn method_get_name{nullptr};
    Il2CppMethodGetParamCountFn method_get_param_count{nullptr};
    Il2CppClassGetFieldFromNameFn class_get_field_from_name{nullptr};
    Il2CppClassGetFieldsFn class_get_fields{nullptr};
    Il2CppFieldGetNameFn field_get_name{nullptr};
    Il2CppFieldGetOffsetFn field_get_offset{nullptr};
};

Il2CppApi g_il2cpp{};

constexpr ptrdiff_t kMissingField = -1;

struct FieldOffsets {
    ptrdiff_t rogue_code_value{kMissingField};
    ptrdiff_t rogue_code_name{kMissingField};
    ptrdiff_t playfab_error_api_endpoint{kMissingField};
    ptrdiff_t playfab_error_http_code{kMissingField};
    ptrdiff_t playfab_error_http_status{kMissingField};
    ptrdiff_t playfab_error_error{kMissingField};
    ptrdiff_t playfab_error_error_message{kMissingField};
    ptrdiff_t call_container_api_endpoint{kMissingField};
    ptrdiff_t call_container_full_url{kMissingField};
    ptrdiff_t call_container_payload{kMissingField};
    ptrdiff_t call_container_json_response{kMissingField};
    ptrdiff_t call_container_api_request{kMissingField};
    ptrdiff_t call_container_error{kMissingField};
    ptrdiff_t execute_function_name{kMissingField};
    ptrdiff_t execute_function_parameter{kMissingField};
    ptrdiff_t integrity_cloud_project_number{kMissingField};
    ptrdiff_t integrity_token_response_token{kMissingField};
    ptrdiff_t params_is_player{kMissingField};
    ptrdiff_t params_is_enemy{kMissingField};
    ptrdiff_t fighter_param{kMissingField};
    ptrdiff_t prepare_integrity_result{kMissingField};
    ptrdiff_t rogue_function_return_code{kMissingField};
    ptrdiff_t backup_issue_error_code{kMissingField};
    ptrdiff_t backup_issue_msg{kMissingField};
    ptrdiff_t backup_verify_error_code{kMissingField};
    ptrdiff_t backup_verify_msg{kMissingField};
    ptrdiff_t verify_state_backup_key{kMissingField};
    ptrdiff_t verify_state_playfab_id{kMissingField};
    ptrdiff_t switch_game_speed_value{kMissingField};
};

FieldOffsets g_fields{};

constexpr int64_t kIntegrityCloudProjectNumber = 814383916740LL;

bool ends_with(const std::string &value, const char *suffix) {
    if (suffix == nullptr) return false;
    size_t suffix_len = std::strlen(suffix);
    return value.size() >= suffix_len
            && value.compare(value.size() - suffix_len, suffix_len, suffix) == 0;
}

bool contains(const char *value, const char *needle) {
    return value != nullptr && needle != nullptr && std::strstr(value, needle) != nullptr;
}

uintptr_t align_down(uintptr_t value, uintptr_t alignment) {
    return alignment == 0 ? value : value & ~(alignment - 1);
}

uintptr_t align_up(uintptr_t value, uintptr_t alignment) {
    return alignment == 0 ? value : (value + alignment - 1) & ~(alignment - 1);
}

bool is_executable_address(uintptr_t address);

bool read_elf_exec_delta_for_offset(const char *path, uintptr_t file_offset,
                                    intptr_t *delta_out) {
    if (path == nullptr || delta_out == nullptr) return false;
    FILE *f = std::fopen(path, "rb");
    if (f == nullptr) return false;

    Elf64_Ehdr ehdr{};
    bool ok = std::fread(&ehdr, sizeof(ehdr), 1, f) == 1
            && ehdr.e_ident[EI_MAG0] == ELFMAG0
            && ehdr.e_ident[EI_MAG1] == ELFMAG1
            && ehdr.e_ident[EI_MAG2] == ELFMAG2
            && ehdr.e_ident[EI_MAG3] == ELFMAG3
            && ehdr.e_ident[EI_CLASS] == ELFCLASS64
            && ehdr.e_phentsize == sizeof(Elf64_Phdr)
            && ehdr.e_phnum > 0;
    if (!ok || std::fseek(f, static_cast<long>(ehdr.e_phoff), SEEK_SET) != 0) {
        std::fclose(f);
        return false;
    }

    long page_size = sysconf(_SC_PAGESIZE);
    uintptr_t page = page_size > 0 ? static_cast<uintptr_t>(page_size) : 0x4000;
    for (uint16_t i = 0; i < ehdr.e_phnum; ++i) {
        Elf64_Phdr phdr{};
        if (std::fread(&phdr, sizeof(phdr), 1, f) != 1) break;
        if (phdr.p_type != PT_LOAD || (phdr.p_flags & PF_X) == 0) continue;

        uintptr_t offset_start = align_down(static_cast<uintptr_t>(phdr.p_offset), page);
        uintptr_t offset_end = align_up(static_cast<uintptr_t>(phdr.p_offset + phdr.p_filesz),
                                        page);
        if (file_offset < offset_start || file_offset >= offset_end) continue;

        uintptr_t vaddr_start = align_down(static_cast<uintptr_t>(phdr.p_vaddr), page);
        *delta_out = static_cast<intptr_t>(vaddr_start) - static_cast<intptr_t>(offset_start);
        std::fclose(f);
        return true;
    }

    std::fclose(f);
    return false;
}

bool read_file_chunk(FILE *f, uint64_t offset, void *buffer, size_t size) {
    if (f == nullptr || buffer == nullptr) return false;
    if (size == 0) return true;
    if (std::fseek(f, static_cast<long>(offset), SEEK_SET) != 0) return false;
    return std::fread(buffer, 1, size, f) == size;
}

uintptr_t resolve_elf_symbol_from_sections(uintptr_t base, const std::string &path,
                                           const char *symbol) {
    if (base == 0 || path.empty() || symbol == nullptr) return 0;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return 0;

    Elf64_Ehdr ehdr{};
    bool ok = read_file_chunk(f, 0, &ehdr, sizeof(ehdr))
            && ehdr.e_ident[EI_MAG0] == ELFMAG0
            && ehdr.e_ident[EI_MAG1] == ELFMAG1
            && ehdr.e_ident[EI_MAG2] == ELFMAG2
            && ehdr.e_ident[EI_MAG3] == ELFMAG3
            && ehdr.e_ident[EI_CLASS] == ELFCLASS64
            && ehdr.e_shentsize == sizeof(Elf64_Shdr)
            && ehdr.e_shnum > 0;
    if (!ok) {
        std::fclose(f);
        return 0;
    }

    std::vector<Elf64_Shdr> sections(ehdr.e_shnum);
    if (!read_file_chunk(f, ehdr.e_shoff, sections.data(),
                         sizeof(Elf64_Shdr) * sections.size())) {
        std::fclose(f);
        return 0;
    }

    uintptr_t resolved = 0;
    for (size_t i = 0; i < sections.size() && resolved == 0; ++i) {
        const Elf64_Shdr &sym_section = sections[i];
        if (sym_section.sh_type != SHT_DYNSYM && sym_section.sh_type != SHT_SYMTAB) continue;
        if (sym_section.sh_link >= sections.size() || sym_section.sh_entsize == 0) continue;
        const Elf64_Shdr &str_section = sections[sym_section.sh_link];
        if (str_section.sh_size == 0) continue;

        std::vector<char> strings(str_section.sh_size);
        if (!read_file_chunk(f, str_section.sh_offset, strings.data(), strings.size())) continue;

        size_t count = static_cast<size_t>(sym_section.sh_size / sym_section.sh_entsize);
        for (size_t j = 0; j < count; ++j) {
            Elf64_Sym sym{};
            uint64_t off = sym_section.sh_offset + j * sym_section.sh_entsize;
            if (!read_file_chunk(f, off, &sym, sizeof(sym))) break;
            if (sym.st_name >= strings.size() || sym.st_value == 0) continue;
            const char *name = strings.data() + sym.st_name;
            if (std::strcmp(name, symbol) == 0) {
                resolved = base + static_cast<uintptr_t>(sym.st_value);
                break;
            }
        }
    }

    std::fclose(f);
    return resolved;
}

uintptr_t resolve_loaded_symbol(uintptr_t base, const std::string &path, const char *symbol,
                                bool require_executable = true) {
    if (symbol == nullptr) return 0;
    void *from_default = dlsym(RTLD_DEFAULT, symbol);
    if (from_default != nullptr) {
        uintptr_t address = reinterpret_cast<uintptr_t>(from_default);
        if (!require_executable || is_executable_address(address)) return address;
    }

    uintptr_t address = resolve_elf_symbol_from_sections(base, path, symbol);
    if (address != 0 && (!require_executable || is_executable_address(address))) return address;
    return 0;
}

std::string find_loaded_library_path(const char *name, uintptr_t *base_out = nullptr) {
    FILE *f = std::fopen("/proc/self/maps", "r");
    if (f == nullptr) return {};
    char line[1024];
    std::string out;
    uintptr_t best_base = 0;
    uintptr_t fallback_base = 0;
    uintptr_t first_exec_start = 0;
    uintptr_t first_exec_offset = 0;
    std::string first_exec_path;
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
        if (fallback_base == 0 && offset == 0) fallback_base = start;
        if (first_exec_start == 0 && std::strchr(perms, 'x') != nullptr) {
            first_exec_start = start;
            first_exec_offset = offset;
            first_exec_path = candidate;
        }
    }
    std::fclose(f);
    if (first_exec_start != 0) {
        intptr_t elf_delta = 0;
        if (read_elf_exec_delta_for_offset(first_exec_path.c_str(), first_exec_offset,
                                           &elf_delta)) {
            best_base = first_exec_start - (first_exec_offset + elf_delta);
            ALOGI("%s ELF load base resolved via exec map: start=%p fileOff=0x%zx delta=0x%zx base=%p",
                  name,
                  reinterpret_cast<void *>(first_exec_start),
                  first_exec_offset,
                  static_cast<size_t>(elf_delta),
                  reinterpret_cast<void *>(best_base));
        } else {
            best_base = first_exec_start - first_exec_offset;
            ALOGW("%s ELF delta unavailable; falling back to start-fileOff base=%p",
                  name, reinterpret_cast<void *>(best_base));
        }
    }
    if (best_base == 0) best_base = fallback_base;
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

template <typename T>
bool resolve_il2cpp_export(uintptr_t base, const std::string &path, const char *symbol,
                           T *out) {
    uintptr_t address = resolve_loaded_symbol(base, path, symbol);
    if (address == 0) {
        ALOGW("missing IL2CPP export %s", symbol);
        if (out != nullptr) *out = nullptr;
        return false;
    }
    if (out != nullptr) *out = reinterpret_cast<T>(address);
    ALOGI("resolved IL2CPP export %s @ %p", symbol, reinterpret_cast<void *>(address));
    return true;
}

bool resolve_il2cpp_api(uintptr_t base, const std::string &path) {
    bool ok = true;
    ok &= resolve_il2cpp_export(base, path, "il2cpp_domain_get", &g_il2cpp.domain_get);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_domain_get_assemblies",
                                &g_il2cpp.domain_get_assemblies);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_assembly_get_image",
                                &g_il2cpp.assembly_get_image);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_image_get_name", &g_il2cpp.image_get_name);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_image_get_class_count",
                                &g_il2cpp.image_get_class_count);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_image_get_class",
                                &g_il2cpp.image_get_class);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_class_from_name",
                                &g_il2cpp.class_from_name);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_class_get_name",
                                &g_il2cpp.class_get_name);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_class_get_namespace",
                                &g_il2cpp.class_get_namespace);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_class_get_declaring_type",
                                &g_il2cpp.class_get_declaring_type);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_class_get_method_from_name",
                                &g_il2cpp.class_get_method_from_name);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_class_get_methods",
                                &g_il2cpp.class_get_methods);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_method_get_name",
                                &g_il2cpp.method_get_name);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_method_get_param_count",
                                &g_il2cpp.method_get_param_count);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_class_get_field_from_name",
                                &g_il2cpp.class_get_field_from_name);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_class_get_fields",
                                &g_il2cpp.class_get_fields);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_field_get_name",
                                &g_il2cpp.field_get_name);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_field_get_offset",
                                &g_il2cpp.field_get_offset);
    ok &= resolve_il2cpp_export(base, path, "il2cpp_string_new", &g_il2cpp_string_new);
    return ok;
}

bool image_name_matches(const char *actual, const char *wanted) {
    if (actual == nullptr || wanted == nullptr) return false;
    if (std::strcmp(actual, wanted) == 0) return true;
    size_t wanted_len = std::strlen(wanted);
    if (std::strncmp(actual, wanted, wanted_len) != 0) return false;
    return actual[wanted_len] == '\0' || std::strcmp(actual + wanted_len, ".dll") == 0;
}

void *find_il2cpp_image(const char *wanted_name) {
    if (wanted_name == nullptr || g_il2cpp.domain_get == nullptr
            || g_il2cpp.domain_get_assemblies == nullptr
            || g_il2cpp.assembly_get_image == nullptr || g_il2cpp.image_get_name == nullptr) {
        return nullptr;
    }
    void *domain = g_il2cpp.domain_get();
    if (domain == nullptr) return nullptr;
    size_t count = 0;
    void **assemblies = g_il2cpp.domain_get_assemblies(domain, &count);
    if (assemblies == nullptr || count == 0 || count > 4096) return nullptr;
    for (size_t i = 0; i < count; ++i) {
        void *image = g_il2cpp.assembly_get_image(assemblies[i]);
        const char *name = image != nullptr ? g_il2cpp.image_get_name(image) : nullptr;
        if (image_name_matches(name, wanted_name)) return image;
    }
    return nullptr;
}

bool declaring_type_matches(void *klass, const char *declaring_name) {
    if (declaring_name == nullptr || declaring_name[0] == '\0') return true;
    if (klass == nullptr || g_il2cpp.class_get_declaring_type == nullptr
            || g_il2cpp.class_get_name == nullptr) {
        return false;
    }
    void *declaring = g_il2cpp.class_get_declaring_type(klass);
    const char *name = declaring != nullptr ? g_il2cpp.class_get_name(declaring) : nullptr;
    return std::strcmp(name != nullptr ? name : "", declaring_name) == 0;
}

bool class_name_matches(const char *actual, const char *class_name, const char *declaring_name) {
    if (actual == nullptr || class_name == nullptr) return false;
    if (std::strcmp(actual, class_name) == 0) return true;
    if (declaring_name == nullptr || declaring_name[0] == '\0') return false;
    std::string dotted = std::string(declaring_name) + "." + class_name;
    std::string slashed = std::string(declaring_name) + "/" + class_name;
    return dotted == actual || slashed == actual;
}

void *find_il2cpp_class(const char *image_name, const char *namespaze, const char *class_name,
                       const char *declaring_name = nullptr) {
    void *image = find_il2cpp_image(image_name);
    if (image == nullptr || class_name == nullptr) return nullptr;
    const char *ns = namespaze != nullptr ? namespaze : "";

    if (g_il2cpp.class_from_name != nullptr) {
        void *klass = g_il2cpp.class_from_name(image, ns, class_name);
        if (klass != nullptr && declaring_type_matches(klass, declaring_name)) return klass;
        if (declaring_name != nullptr && declaring_name[0] != '\0') {
            std::string slashed = std::string(declaring_name) + "/" + class_name;
            klass = g_il2cpp.class_from_name(image, ns, slashed.c_str());
            if (klass != nullptr) return klass;
            std::string dotted = std::string(declaring_name) + "." + class_name;
            klass = g_il2cpp.class_from_name(image, ns, dotted.c_str());
            if (klass != nullptr) return klass;
        }
    }

    if (g_il2cpp.image_get_class_count == nullptr || g_il2cpp.image_get_class == nullptr
            || g_il2cpp.class_get_name == nullptr || g_il2cpp.class_get_namespace == nullptr) {
        return nullptr;
    }
    size_t count = g_il2cpp.image_get_class_count(image);
    if (count > 200000) return nullptr;
    for (size_t i = 0; i < count; ++i) {
        void *klass = g_il2cpp.image_get_class(image, i);
        if (klass == nullptr) continue;
        const char *actual_name = g_il2cpp.class_get_name(klass);
        const char *actual_ns = g_il2cpp.class_get_namespace(klass);
        if (std::strcmp(actual_ns != nullptr ? actual_ns : "", ns) != 0) continue;
        if (!class_name_matches(actual_name, class_name, declaring_name)) continue;
        if (!declaring_type_matches(klass, declaring_name)) continue;
        return klass;
    }
    return nullptr;
}

void *find_il2cpp_method(void *klass, const char *method_name, int param_count) {
    if (klass == nullptr || method_name == nullptr) return nullptr;
    if (g_il2cpp.class_get_method_from_name != nullptr) {
        void *method = g_il2cpp.class_get_method_from_name(klass, method_name, param_count);
        if (method != nullptr) return method;
    }
    if (g_il2cpp.class_get_methods == nullptr || g_il2cpp.method_get_name == nullptr
            || g_il2cpp.method_get_param_count == nullptr) {
        return nullptr;
    }
    void *iter = nullptr;
    while (void *method = g_il2cpp.class_get_methods(klass, &iter)) {
        const char *name = g_il2cpp.method_get_name(method);
        uint32_t argc = g_il2cpp.method_get_param_count(method);
        if (std::strcmp(name != nullptr ? name : "", method_name) == 0
                && static_cast<int>(argc) == param_count) {
            return method;
        }
    }
    return nullptr;
}

void *method_pointer(void *method) {
    auto *head = reinterpret_cast<Il2CppMethodInfoHead *>(method);
    if (head == nullptr) return nullptr;
    uintptr_t direct = reinterpret_cast<uintptr_t>(head->methodPointer);
    if (direct != 0 && is_executable_address(direct)) return head->methodPointer;
    uintptr_t virt = reinterpret_cast<uintptr_t>(head->virtualMethodPointer);
    if (virt != 0 && is_executable_address(virt)) return head->virtualMethodPointer;
    return nullptr;
}

bool il2cpp_metadata_ready() {
    return find_il2cpp_class("Assembly-CSharp", "", "RogueServerCode") != nullptr
            && find_il2cpp_class("Assembly-CSharp", "", "UseCase_SwitchGameSpeed") != nullptr;
}

bool wait_for_il2cpp_metadata(int attempts, useconds_t sleep_us) {
    for (int i = 0; i < attempts; ++i) {
        if (il2cpp_metadata_ready()) return true;
        usleep(sleep_us);
    }
    return false;
}

int load_multiplier(const std::atomic<int> &value) {
    int current = value.load(std::memory_order_relaxed);
    return std::clamp(current, 1, 100);
}

float sanitize_game_speed_multiplier(float value) {
    if (!std::isfinite(value) || value < 0.1f) return 1.0f;
    if (value > 100.0f) return 100.0f;
    return value;
}

bool has_game_speed_override(float value) {
    return std::fabs(value - 1.0f) > 0.001f;
}

void apply_launch_game_speed_if_needed(const char *reason);
void install_after_il2cpp_init(const char *reason);

const char *object_class_name(void *object) {
    if (object == nullptr) return "";
    auto *managed = reinterpret_cast<Il2CppObject *>(object);
    auto *klass = reinterpret_cast<Il2CppClassHead *>(managed->klass);
    if (klass == nullptr || klass->name == nullptr) return "";
    return klass->name;
}

void *object_class(void *object) {
    if (object == nullptr) return nullptr;
    return reinterpret_cast<Il2CppObject *>(object)->klass;
}

void *read_object_field(void *object, ptrdiff_t offset) {
    if (object == nullptr || offset < 0) return nullptr;
    return *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(object) + offset);
}

bool read_bool_field(void *object, ptrdiff_t offset) {
    if (object == nullptr || offset < 0) return false;
    return *reinterpret_cast<uint8_t *>(reinterpret_cast<uint8_t *>(object) + offset) != 0;
}

int read_int_field(void *object, ptrdiff_t offset, int fallback = 0) {
    if (object == nullptr || offset < 0) return fallback;
    return *reinterpret_cast<int32_t *>(reinterpret_cast<uint8_t *>(object) + offset);
}

void write_object_field(void *object, ptrdiff_t offset, void *value) {
    if (object == nullptr || offset < 0) return;
    *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(object) + offset) = value;
}

ptrdiff_t find_il2cpp_field_offset_on_class(void *klass, const char *field_name) {
    if (klass == nullptr || field_name == nullptr) return kMissingField;

    void *field = nullptr;
    if (g_il2cpp.class_get_field_from_name != nullptr) {
        field = g_il2cpp.class_get_field_from_name(klass, field_name);
    }
    if (field == nullptr && g_il2cpp.class_get_fields != nullptr
            && g_il2cpp.field_get_name != nullptr) {
        void *iter = nullptr;
        while (void *candidate = g_il2cpp.class_get_fields(klass, &iter)) {
            const char *name = g_il2cpp.field_get_name(candidate);
            if (std::strcmp(name != nullptr ? name : "", field_name) == 0) {
                field = candidate;
                break;
            }
        }
    }
    if (field == nullptr || g_il2cpp.field_get_offset == nullptr) return kMissingField;
    size_t offset = g_il2cpp.field_get_offset(field);
    if (offset == static_cast<size_t>(-1) || offset >= 0x80000000u) return kMissingField;
    return static_cast<ptrdiff_t>(offset);
}

ptrdiff_t find_il2cpp_field_offset(const char *image_name, const char *namespaze,
                                   const char *class_name, const char *field_name,
                                   const char *declaring_name = nullptr) {
    void *klass = find_il2cpp_class(image_name, namespaze, class_name, declaring_name);
    return find_il2cpp_field_offset_on_class(klass, field_name);
}

bool require_field(ptrdiff_t &slot, const char *image_name, const char *namespaze,
                   const char *class_name, const char *field_name,
                   const char *declaring_name = nullptr) {
    slot = find_il2cpp_field_offset(image_name, namespaze, class_name, field_name,
                                    declaring_name);
    if (slot < 0) {
        ALOGW("required field unresolved: %s/%s.%s",
              image_name != nullptr ? image_name : "<image>",
              class_name != nullptr ? class_name : "<class>",
              field_name != nullptr ? field_name : "<field>");
        return false;
    }
    ALOGI("resolved field %s/%s.%s offset=0x%zx",
          image_name != nullptr ? image_name : "<image>",
          class_name != nullptr ? class_name : "<class>",
          field_name != nullptr ? field_name : "<field>",
          static_cast<size_t>(slot));
    return true;
}

void optional_field(ptrdiff_t &slot, const char *image_name, const char *namespaze,
                    const char *class_name, const char *field_name,
                    const char *declaring_name = nullptr) {
    slot = find_il2cpp_field_offset(image_name, namespaze, class_name, field_name,
                                    declaring_name);
    if (slot >= 0) {
        ALOGI("resolved optional field %s/%s.%s offset=0x%zx",
              image_name != nullptr ? image_name : "<image>",
              class_name != nullptr ? class_name : "<class>",
              field_name != nullptr ? field_name : "<field>",
              static_cast<size_t>(slot));
    } else {
        ALOGW("optional field unresolved: %s/%s.%s",
              image_name != nullptr ? image_name : "<image>",
              class_name != nullptr ? class_name : "<class>",
              field_name != nullptr ? field_name : "<field>");
    }
}

void adjust_unboxed_value_type_field(ptrdiff_t &slot, const char *name) {
    ptrdiff_t header = static_cast<ptrdiff_t>(sizeof(Il2CppObject));
    if (slot >= header) {
        slot -= header;
        ALOGI("adjusted unboxed value-type field %s offset=0x%zx",
              name != nullptr ? name : "<field>", static_cast<size_t>(slot));
    }
}

bool resolve_managed_field_offsets() {
    bool ok = true;
    ok &= require_field(g_fields.rogue_code_value, "Assembly-CSharp", "",
                        "RogueServerCode", "value");
    ok &= require_field(g_fields.rogue_code_name, "Assembly-CSharp", "",
                        "RogueServerCode", "name");
    ok &= require_field(g_fields.params_is_player, "Assembly-CSharp", "",
                        "Params", "<isPlayer>k__BackingField");
    ok &= require_field(g_fields.params_is_enemy, "Assembly-CSharp", "",
                        "Params", "<isEnemy>k__BackingField");
    ok &= require_field(g_fields.fighter_param, "Assembly-CSharp", "",
                        "Fighter", "param");
    ok &= require_field(g_fields.integrity_cloud_project_number, "Google.Play.Integrity",
                        "Google.Play.Integrity", "IntegrityTokenRequest",
                        "<CloudProjectNumber>k__BackingField");
    ok &= require_field(g_fields.backup_issue_error_code, "Assembly-CSharp", "",
                        "<>c__DisplayClass118_0", "errorCode", "ServerManager");
    ok &= require_field(g_fields.backup_issue_msg, "Assembly-CSharp", "",
                        "<>c__DisplayClass118_0", "msg", "ServerManager");
    ok &= require_field(g_fields.backup_verify_error_code, "Assembly-CSharp", "",
                        "<>c__DisplayClass119_0", "errorCode", "ServerManager");
    ok &= require_field(g_fields.backup_verify_msg, "Assembly-CSharp", "",
                        "<>c__DisplayClass119_0", "msg", "ServerManager");
    ok &= require_field(g_fields.verify_state_backup_key, "Assembly-CSharp", "",
                        "<VerifyBackupKeyAsync>d__119", "backupKey", "ServerManager");
    ok &= require_field(g_fields.verify_state_playfab_id, "Assembly-CSharp", "",
                        "<VerifyBackupKeyAsync>d__119", "playFabId", "ServerManager");
    adjust_unboxed_value_type_field(g_fields.verify_state_backup_key,
                                    "ServerManager.<VerifyBackupKeyAsync>.backupKey");
    adjust_unboxed_value_type_field(g_fields.verify_state_playfab_id,
                                    "ServerManager.<VerifyBackupKeyAsync>.playFabId");
    ok &= require_field(g_fields.switch_game_speed_value, "Assembly-CSharp", "",
                        "UseCase_SwitchGameSpeed", "speedUpValue");

    optional_field(g_fields.playfab_error_api_endpoint, "PlayFab", "PlayFab",
                   "PlayFabError", "ApiEndpoint");
    optional_field(g_fields.playfab_error_http_code, "PlayFab", "PlayFab",
                   "PlayFabError", "HttpCode");
    optional_field(g_fields.playfab_error_http_status, "PlayFab", "PlayFab",
                   "PlayFabError", "HttpStatus");
    optional_field(g_fields.playfab_error_error, "PlayFab", "PlayFab",
                   "PlayFabError", "Error");
    optional_field(g_fields.playfab_error_error_message, "PlayFab", "PlayFab",
                   "PlayFabError", "ErrorMessage");
    optional_field(g_fields.call_container_api_endpoint, "PlayFab", "PlayFab.Internal",
                   "CallRequestContainer", "ApiEndpoint");
    optional_field(g_fields.call_container_full_url, "PlayFab", "PlayFab.Internal",
                   "CallRequestContainer", "FullUrl");
    optional_field(g_fields.call_container_payload, "PlayFab", "PlayFab.Internal",
                   "CallRequestContainer", "Payload");
    optional_field(g_fields.call_container_json_response, "PlayFab", "PlayFab.Internal",
                   "CallRequestContainer", "JsonResponse");
    optional_field(g_fields.call_container_api_request, "PlayFab", "PlayFab.Internal",
                   "CallRequestContainer", "ApiRequest");
    optional_field(g_fields.call_container_error, "PlayFab", "PlayFab.Internal",
                   "CallRequestContainer", "Error");
    optional_field(g_fields.execute_function_name, "PlayFab", "PlayFab.CloudScriptModels",
                   "ExecuteFunctionRequest", "FunctionName");
    optional_field(g_fields.execute_function_parameter, "PlayFab", "PlayFab.CloudScriptModels",
                   "ExecuteFunctionRequest", "FunctionParameter");
    optional_field(g_fields.integrity_token_response_token, "Google.Play.Integrity",
                   "Google.Play.Integrity", "IntegrityTokenResponse",
                   "<Token>k__BackingField");
    optional_field(g_fields.prepare_integrity_result, "Assembly-CSharp", "",
                   "<>c__DisplayClass113_0", "result", "ServerManager");
    optional_field(g_fields.rogue_function_return_code, "Assembly-CSharp", "",
                   "RogueServerFunctionResult", "returnCode");
    return ok;
}

// Generate a backup code that matches the game's real format: 8 characters, lowercase letters
// plus digits (`giu2urx7` is a sample of a server-issued one). The forged value is purely
// cosmetic - the matching VerifyBackupKey forge accepts any 8-char input on the restore side.
std::string make_forged_backup_key() {
    static constexpr char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    constexpr size_t kCharsLen = sizeof(kChars) - 1;
    uint64_t seed = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    seed ^= static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&seed));
    std::string out;
    out.reserve(8);
    for (int i = 0; i < 8; ++i) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        out.push_back(kChars[(seed >> 33) % kCharsLen]);
    }
    return out;
}

std::string managed_string_to_ascii(void *string_object) {
    auto *s = reinterpret_cast<Il2CppString *>(string_object);
    if (s == nullptr || s->length <= 0) return {};
    int32_t length = std::min(s->length, 256);
    std::string out;
    out.reserve(static_cast<size_t>(length));
    for (int32_t i = 0; i < length; ++i) {
        char16_t ch = s->chars[i];
        out.push_back(ch >= 0x20 && ch <= 0x7e ? static_cast<char>(ch) : '?');
    }
    return out;
}

[[maybe_unused]] int32_t managed_string_length(void *string_object) {
    auto *s = reinterpret_cast<Il2CppString *>(string_object);
    return s != nullptr ? s->length : -1;
}

[[maybe_unused]] int count_ascii_char(void *string_object, char needle) {
    auto *s = reinterpret_cast<Il2CppString *>(string_object);
    if (s == nullptr || s->length <= 0) return 0;
    int count = 0;
    for (int32_t i = 0; i < s->length; ++i) {
        if (s->chars[i] == static_cast<char16_t>(needle)) ++count;
    }
    return count;
}

std::string sanitize_ascii(const uint8_t *data, size_t length, size_t limit) {
    if (data == nullptr || length == 0 || limit == 0) return {};
    size_t safe_len = std::min(length, limit);
    std::string out;
    out.reserve(safe_len);
    for (size_t i = 0; i < safe_len; ++i) {
        uint8_t ch = data[i];
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            out.push_back(' ');
        } else if (ch >= 0x20 && ch <= 0x7e) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('.');
        }
    }
    return out;
}

[[maybe_unused]] int32_t byte_array_length(void *array_object) {
    auto *array = reinterpret_cast<Il2CppByteArray *>(array_object);
    if (array == nullptr) return -1;
    if (array->max_length > 1024 * 1024) return -1;
    return static_cast<int32_t>(array->max_length);
}

std::string byte_array_prefix(void *array_object, size_t limit) {
    auto *array = reinterpret_cast<Il2CppByteArray *>(array_object);
    if (array == nullptr || array->max_length == 0 || array->max_length > 1024 * 1024) {
        return {};
    }
    return sanitize_ascii(array->vector, static_cast<size_t>(array->max_length), limit);
}

std::string managed_string_prefix(void *string_object, int32_t limit) {
    auto *s = reinterpret_cast<Il2CppString *>(string_object);
    if (s == nullptr || s->length <= 0 || limit <= 0) return {};
    int32_t length = std::min(s->length, limit);
    std::string out;
    out.reserve(static_cast<size_t>(length));
    for (int32_t i = 0; i < length; ++i) {
        char16_t ch = s->chars[i];
        out.push_back(ch >= 0x20 && ch <= 0x7e ? static_cast<char>(ch) : '?');
    }
    return out;
}

bool is_interesting_playfab_container(const std::string &endpoint,
                                      const std::string &url,
                                      const std::string &payload,
                                      const std::string &response,
                                      void *api_request) {
    const char *api_type = object_class_name(api_request);
    return endpoint.find("CloudScript") != std::string::npos
            || url.find("CloudScript") != std::string::npos
            || payload.find("ExecuteFunction") != std::string::npos
            || payload.find("PrepareIntegrityCheck") != std::string::npos
            || payload.find("IssueBackupKey") != std::string::npos
            || payload.find("FunctionName") != std::string::npos
            || response.find("IssueBackupKey") != std::string::npos
            || response.find("VerifyIntegrity") != std::string::npos
            || contains(api_type, "ExecuteFunctionRequest");
}

bool is_interesting_playfab_text(const std::string &value) {
    return value.find("CloudScript") != std::string::npos
            || value.find("ExecuteFunction") != std::string::npos
            || value.find("PrepareIntegrityCheck") != std::string::npos
            || value.find("IssueBackupKey") != std::string::npos
            || value.find("VerifyIntegrity") != std::string::npos
            || value.find("ServerFunctionException") != std::string::npos
            || value.find("errorCode") != std::string::npos
            || value.find("errorMessage") != std::string::npos;
}

[[maybe_unused]] std::string rogue_server_code_label(void *code) {
    if (code == nullptr) return "<null>";
    int value = read_int_field(code, g_fields.rogue_code_value, -1);
    std::string name = managed_string_to_ascii(read_object_field(code, g_fields.rogue_code_name));
    if (name.empty()) name = "?";
    char buffer[320];
    std::snprintf(buffer, sizeof(buffer), "%s(%d)", name.c_str(), value);
    return buffer;
}

void log_playfab_error(void *error, const char *source) {
    if (error == nullptr) return;
    std::string endpoint = managed_string_to_ascii(
            read_object_field(error, g_fields.playfab_error_api_endpoint));
    [[maybe_unused]] int http_code = read_int_field(
            error, g_fields.playfab_error_http_code, -1);
    std::string http_status = managed_string_to_ascii(
            read_object_field(error, g_fields.playfab_error_http_status));
    [[maybe_unused]] int error_code = read_int_field(
            error, g_fields.playfab_error_error, -1);
    std::string error_message = managed_string_to_ascii(
            read_object_field(error, g_fields.playfab_error_error_message));
    ALOGW("%s PlayFabError endpoint=%s http=%d/%s code=%d msg=%s",
          source != nullptr ? source : "PlayFab",
          endpoint.empty() ? "<empty>" : endpoint.c_str(),
          http_code,
          http_status.empty() ? "<empty>" : http_status.c_str(),
          error_code,
          error_message.empty() ? "<empty>" : error_message.c_str());
}

void log_playfab_request_container(void *req_container, const char *source) {
    if (!TEMPLATE_VERBOSE_LOGS || req_container == nullptr) return;
    std::string endpoint = managed_string_to_ascii(
            read_object_field(req_container, g_fields.call_container_api_endpoint));
    std::string url = managed_string_to_ascii(
            read_object_field(req_container, g_fields.call_container_full_url));
    void *payload_array = read_object_field(req_container, g_fields.call_container_payload);
    std::string payload = byte_array_prefix(payload_array, 768);
    std::string response = managed_string_prefix(
            read_object_field(req_container, g_fields.call_container_json_response), 768);
    void *api_request = read_object_field(req_container, g_fields.call_container_api_request);
    static std::atomic<int> generic_logs{0};
    if (!is_interesting_playfab_container(endpoint, url, payload, response, api_request)) {
        int previous = generic_logs.fetch_add(1, std::memory_order_relaxed);
        if (previous < 16) {
            ALOGI("%s PlayFab container sample endpoint=%s url=%s apiReq=%s payloadLen=%d responseLen=%d",
                  source != nullptr ? source : "PlayFab",
                  endpoint.empty() ? "<empty>" : endpoint.c_str(),
                  url.empty() ? "<empty>" : url.c_str(),
                  object_class_name(api_request),
                  byte_array_length(payload_array),
                  managed_string_length(read_object_field(
                          req_container, g_fields.call_container_json_response)));
        }
        return;
    }

    ALOGI("%s PlayFab container endpoint=%s url=%s apiReq=%s payloadLen=%d responseLen=%d",
          source != nullptr ? source : "PlayFab",
          endpoint.empty() ? "<empty>" : endpoint.c_str(),
          url.empty() ? "<empty>" : url.c_str(),
          object_class_name(api_request),
          byte_array_length(payload_array),
          managed_string_length(read_object_field(
                  req_container, g_fields.call_container_json_response)));
    if (!payload.empty()) {
        ALOGI("%s PlayFab payload=%s",
              source != nullptr ? source : "PlayFab", payload.c_str());
    }
    if (!response.empty()) {
        ALOGI("%s PlayFab response=%s",
              source != nullptr ? source : "PlayFab", response.c_str());
    }
    log_playfab_error(read_object_field(req_container, g_fields.call_container_error), source);
}

void log_execute_function_request(void *request, const char *source) {
    if (!TEMPLATE_VERBOSE_LOGS || request == nullptr) return;
    std::string function_name = managed_string_to_ascii(
            read_object_field(request, g_fields.execute_function_name));
    if (function_name == "PrepareIntegrityCheck"
            || function_name.rfind("IssueBackupKey_", 0) == 0
            || function_name.rfind("VerifyBackupKey_", 0) == 0) {
        ALOGI("%s ExecuteFunction %s", source != nullptr ? source : "PlayFab",
              function_name.c_str());
        if (function_name.rfind("IssueBackupKey_", 0) == 0) {
            void *param = read_object_field(request, g_fields.execute_function_parameter);
            void *param_class = object_class(param);
            ptrdiff_t message_hash_offset = find_il2cpp_field_offset_on_class(
                    param_class, "<MessageHash>i__Field");
            ptrdiff_t signed_token_offset = find_il2cpp_field_offset_on_class(
                    param_class, "<SignedToken>i__Field");
            if (message_hash_offset >= 0
                    && message_hash_offset < static_cast<ptrdiff_t>(sizeof(Il2CppObject))) {
                message_hash_offset = kMissingField;
            }
            if (signed_token_offset >= 0
                    && signed_token_offset < static_cast<ptrdiff_t>(sizeof(Il2CppObject))) {
                signed_token_offset = kMissingField;
            }
            [[maybe_unused]] void *message_hash = read_object_field(param, message_hash_offset);
            [[maybe_unused]] void *signed_token = read_object_field(param, signed_token_offset);
            ALOGI("IssueBackupKey param hashLen=%d hashPrefix=%s tokenLen=%d tokenDots=%d tokenPrefix=%s",
                  managed_string_length(message_hash),
                  managed_string_prefix(message_hash, 16).c_str(),
                  managed_string_length(signed_token),
                  count_ascii_char(signed_token, '.'),
                  managed_string_prefix(signed_token, 24).c_str());
        }
    }
}

void log_playfab_make_api_call(void *api_endpoint, void *full_url, void *request,
                               int auth_type, bool allow_queueing, const char *source) {
    if (!TEMPLATE_VERBOSE_LOGS) return;
    std::string endpoint = managed_string_to_ascii(api_endpoint);
    std::string url = managed_string_to_ascii(full_url);
    const char *request_type = object_class_name(request);
    bool interesting = endpoint.find("CloudScript") != std::string::npos
            || url.find("CloudScript") != std::string::npos
            || contains(request_type, "ExecuteFunctionRequest");
    static std::atomic<int> generic_logs{0};
    if (!interesting) {
        int previous = generic_logs.fetch_add(1, std::memory_order_relaxed);
        if (previous < 16) {
            ALOGI("%s _MakeApiCall sample endpoint=%s url=%s req=%s auth=%d queue=%d",
                  source != nullptr ? source : "PlayFab",
                  endpoint.empty() ? "<empty>" : endpoint.c_str(),
                  url.empty() ? "<empty>" : url.c_str(),
                  request_type,
                  auth_type,
                  allow_queueing ? 1 : 0);
        }
        return;
    }
    ALOGI("%s _MakeApiCall endpoint=%s url=%s req=%s auth=%d queue=%d",
          source != nullptr ? source : "PlayFab",
          endpoint.empty() ? "<empty>" : endpoint.c_str(),
          url.empty() ? "<empty>" : url.c_str(),
          request_type,
          auth_type,
          allow_queueing ? 1 : 0);
    if (contains(request_type, "ExecuteFunctionRequest")) {
        log_execute_function_request(request, source);
    }
}

bool patch_integrity_token_request_project(void *request, const char *source) {
    if (request == nullptr || !g_server_integrity_bypass.load(std::memory_order_relaxed)) {
        return false;
    }
    if (g_fields.integrity_cloud_project_number < 0) return false;
    auto *field = reinterpret_cast<uint8_t *>(request) + g_fields.integrity_cloud_project_number;
    auto *has_value = reinterpret_cast<bool *>(field);
    auto *value = reinterpret_cast<int64_t *>(field + sizeof(int64_t));
    bool changed = !*has_value || *value != kIntegrityCloudProjectNumber;
    *has_value = true;
    *value = kIntegrityCloudProjectNumber;
    if (changed) {
        g_integrity_project_number_injected.fetch_add(1, std::memory_order_relaxed);
        ALOGI("IntegrityTokenRequest cloudProjectNumber patched from %s -> %lld",
              source != nullptr ? source : "?",
              static_cast<long long>(kIntegrityCloudProjectNumber));
    }
    return changed;
}

TargetSide side_from_params(void *params) {
    const char *name = object_class_name(params);
    if (contains(name, "MonsterParams")) return TargetSide::Enemy;
    if (contains(name, "PlayerParams") || contains(name, "PlayerScaledParams")
            || contains(name, "UniquePlayerParams")) {
        return TargetSide::Player;
    }
    if (std::strcmp(name, "Params") == 0) {
        if (read_bool_field(params, g_fields.params_is_player)) return TargetSide::Player;
        if (read_bool_field(params, g_fields.params_is_enemy)) return TargetSide::Enemy;
    }
    return TargetSide::Unknown;
}

TargetSide side_from_fighter(void *fighter) {
    const char *name = object_class_name(fighter);
    if (contains(name, "MonsterFighter")) return TargetSide::Enemy;
    if (contains(name, "PlayerFighter") || contains(name, "UniquePlayerFighter")) {
        return TargetSide::Player;
    }
    return side_from_params(read_object_field(fighter, g_fields.fighter_param));
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

// RogueServerCode.get_IsIntegrityError() guard.
//
// This deliberately returns the original result. The bypass rewrites the earlier
// PrepareIntegrityCheck result instead of hiding the final server error classification.
bool proxy_rogue_server_code_is_integrity_error(void *self, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    return SHADOWHOOK_CALL_PREV(proxy_rogue_server_code_is_integrity_error, self, method);
}

// RogueServerCode.get_IsSuccess() guard.
//
// Returns the original result. We also opportunistically capture the Success singleton here: the
// .ctor hook only sees constructions that happen after install, but the type's static cctor
// usually fires much earlier (Login at startup is the first caller). Every IsSuccess check on a
// value==0 instance is therefore Success - exactly the pointer the backup-key forge needs.
bool proxy_rogue_server_code_is_success(void *self, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    bool result = SHADOWHOOK_CALL_PREV(proxy_rogue_server_code_is_success, self, method);
    if (result && self != nullptr
            && g_rogue_server_code_success.load(std::memory_order_acquire) == nullptr) {
        if (read_int_field(self, g_fields.rogue_code_value, -1) == 0) {
            g_rogue_server_code_success.store(self, std::memory_order_release);
            ALOGI("RogueServerCode.Success captured via IsSuccess @ %p", self);
        }
    }
    if (result) {
        apply_launch_game_speed_if_needed("RogueServerCode.IsSuccess");
    }
    return result;
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

// ServerManager.<>c__DisplayClass113_0.<PrepareIntegrityCheck>g__OnSuccess|0.
//
// Keep this as a diagnostic hook. Earlier builds rewrote RequiredIntegrityCheck to
// SkipIntegrityCheck, but that only produced an empty MessageHash/SignedToken pair and the later
// IssueBackupKey_* CloudScript call failed with ServerFunctionException.
void proxy_prepare_integrity_on_success(void *self, void *result, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    SHADOWHOOK_CALL_PREV(proxy_prepare_integrity_on_success, self, result, method);

    void *rogue_result = read_object_field(self, g_fields.prepare_integrity_result);
    void *current_code = read_object_field(rogue_result, g_fields.rogue_function_return_code);
    if (current_code != nullptr) {
        ALOGI("PrepareIntegrityCheck code %s", rogue_server_code_label(current_code).c_str());
    }
}

void *proxy_integrity_manager_request_token(void *self, void *request, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    patch_integrity_token_request_project(request, "RequestIntegrityToken");
    return SHADOWHOOK_CALL_PREV(proxy_integrity_manager_request_token, self, request, method);
}

// Google.Play.Integrity.IntegrityTokenRequest..ctor(string nonce, Nullable<long> cloudProject).
//
// The object field is patched after construction so we do not depend on the platform ABI used
// for the nullable value-type argument.
void proxy_integrity_token_request_ctor(void *self, void *nonce, uint64_t nullable_header,
                                        int64_t nullable_value, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    SHADOWHOOK_CALL_PREV(proxy_integrity_token_request_ctor,
                         self, nonce, nullable_header, nullable_value, method);
    patch_integrity_token_request_project(self, "ctor");
}

void proxy_integrity_token_response_ctor(void *self, void *token_response, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    SHADOWHOOK_CALL_PREV(proxy_integrity_token_response_ctor, self, token_response, method);
    [[maybe_unused]] void *token = read_object_field(
            self, g_fields.integrity_token_response_token);
    ALOGI("IntegrityTokenResponse token len=%d dots=%d prefix=%s",
          managed_string_length(token),
          count_ascii_char(token, '.'),
          managed_string_prefix(token, 24).c_str());
}

// RogueServerCode..ctor(int value, string name). Captures the Success singleton (value == 0) at
// process startup so the IssueBackupKey forge can swap a closure's errorCode pointer to it without
// mutating any static field in place.
void proxy_rogue_server_code_ctor(void *self, int value, void *name, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    SHADOWHOOK_CALL_PREV(proxy_rogue_server_code_ctor, self, value, name, method);
    if (value == 0 && g_rogue_server_code_success.load(std::memory_order_acquire) == nullptr) {
        g_rogue_server_code_success.store(self, std::memory_order_release);
        std::string label = managed_string_to_ascii(name);
        ALOGI("RogueServerCode.Success captured @ %p name=%s",
              self, label.empty() ? "<empty>" : label.c_str());
    }
}

// Replace the RogueServerCode field on a closure with the Success singleton.
// Returns the original value's integer code for diagnostics, or -1 if the singleton is not
// captured yet / the closure is null.
int forge_closure_set_success(void *closure, ptrdiff_t error_code_offset) {
    if (closure == nullptr) return -1;
    void *code = read_object_field(closure, error_code_offset);
    int code_value = code != nullptr ? read_int_field(code, g_fields.rogue_code_value, -1) : -1;
    if (code_value == 0) return 0;
    void *success = g_rogue_server_code_success.load(std::memory_order_acquire);
    if (success == nullptr) return -1;
    write_object_field(closure, error_code_offset, success);
    return code_value;
}

// Forge IssueBackupKey OnSuccess/OnError: replace errorCode with Success and msg with a freshly
// generated 8-character backup key so the UI shows credentials instead of an error toast. The
// forged key only exists on this device - it does not round-trip through PlayFab.
bool forge_issue_backup_closure(void *closure, const char *site) {
    if (closure == nullptr) return false;
    if (!g_forge_backup_success.load(std::memory_order_relaxed)) return false;
    if (g_il2cpp_string_new == nullptr) {
        ALOGW("%s forge skipped: il2cpp_string_new unresolved", site);
        return false;
    }

    int prev = forge_closure_set_success(closure, g_fields.backup_issue_error_code);
    if (prev == 0) return false;
    if (prev < 0) {
        ALOGW("%s forge skipped: Success singleton not captured", site);
        return false;
    }

    std::string forged = make_forged_backup_key();
    void *forged_str = g_il2cpp_string_new(forged.c_str());
    if (forged_str == nullptr) {
        ALOGW("%s forge skipped: il2cpp_string_new returned null", site);
        return false;
    }
    write_object_field(closure, g_fields.backup_issue_msg, forged_str);
    g_backup_success_forged.fetch_add(1, std::memory_order_relaxed);
    ALOGI("%s FORGED success: code=Success(0) msg=%s (replaced original code=%d)",
          site, forged.c_str(), prev);
    return true;
}

// Forge VerifyBackupKey OnSuccess/OnError: replace errorCode with Success and echo the user-typed
// PlayFab id back as the msg (the caller of VerifyBackupKeyAsync uses the second tuple element as
// the verified userid in subsequent restore stages).
bool forge_verify_backup_closure(void *closure, const char *site) {
    if (closure == nullptr) return false;
    if (!g_forge_backup_success.load(std::memory_order_relaxed)) return false;

    int prev = forge_closure_set_success(closure, g_fields.backup_verify_error_code);
    if (prev == 0) return false;
    if (prev < 0) {
        ALOGW("%s forge skipped: Success singleton not captured", site);
        return false;
    }

    void *captured_userid = g_last_verify_playfab_id.load(std::memory_order_acquire);
    if (captured_userid != nullptr) {
        write_object_field(closure, g_fields.backup_verify_msg, captured_userid);
        std::string label = managed_string_to_ascii(captured_userid);
        ALOGI("%s FORGED success: code=Success(0) msg=%s (echoed user-typed playFabId, replaced code=%d)",
              site, label.empty() ? "<empty>" : label.c_str(), prev);
    } else {
        ALOGI("%s FORGED success: code=Success(0) (no user-typed playFabId captured, msg untouched, replaced code=%d)",
              site, prev);
    }
    g_verify_backup_success_forged.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void proxy_issue_backup_key_on_success(void *self, void *result, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    SHADOWHOOK_CALL_PREV(proxy_issue_backup_key_on_success, self, result, method);
    [[maybe_unused]] void *code = read_object_field(self, g_fields.backup_issue_error_code);
    std::string message = managed_string_to_ascii(read_object_field(self, g_fields.backup_issue_msg));
    ALOGI("IssueBackupKey result %s msg=%s",
          rogue_server_code_label(code).c_str(),
          message.empty() ? "<empty>" : message.c_str());
    forge_issue_backup_closure(self, "IssueBackupKey.OnSuccess");
}

void proxy_issue_backup_key_on_error(void *self, void *error, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    SHADOWHOOK_CALL_PREV(proxy_issue_backup_key_on_error, self, error, method);
    [[maybe_unused]] void *code = read_object_field(self, g_fields.backup_issue_error_code);
    std::string message = managed_string_to_ascii(read_object_field(self, g_fields.backup_issue_msg));
    ALOGW("IssueBackupKey error %s msg=%s",
          rogue_server_code_label(code).c_str(),
          message.empty() ? "<empty>" : message.c_str());
    // The OnError path also funnels into the awaitable result. Forge it too so a PlayFab error
    // (network drop, HTTP 4xx, etc.) does not unwind back to the UI as a failure.
    forge_issue_backup_closure(self, "IssueBackupKey.OnError");
}

void proxy_verify_backup_key_on_success(void *self, void *result, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    SHADOWHOOK_CALL_PREV(proxy_verify_backup_key_on_success, self, result, method);
    [[maybe_unused]] void *code = read_object_field(self, g_fields.backup_verify_error_code);
    std::string message = managed_string_to_ascii(read_object_field(self, g_fields.backup_verify_msg));
    ALOGI("VerifyBackupKey result %s msg=%s",
          rogue_server_code_label(code).c_str(),
          message.empty() ? "<empty>" : message.c_str());
    forge_verify_backup_closure(self, "VerifyBackupKey.OnSuccess");
}

void proxy_verify_backup_key_on_error(void *self, void *error, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    SHADOWHOOK_CALL_PREV(proxy_verify_backup_key_on_error, self, error, method);
    [[maybe_unused]] void *code = read_object_field(self, g_fields.backup_verify_error_code);
    std::string message = managed_string_to_ascii(read_object_field(self, g_fields.backup_verify_msg));
    ALOGW("VerifyBackupKey error %s msg=%s",
          rogue_server_code_label(code).c_str(),
          message.empty() ? "<empty>" : message.c_str());
    forge_verify_backup_closure(self, "VerifyBackupKey.OnError");
}

// UnityEngine.Time.set_timeScale(float value) — the real engine setter. ReflectSpeedUp ends up
// here after computing its per-GameSpeedType multiplier. We intercept and rewrite the incoming
// value when:
//   * the slider multiplier is not 1.0, AND
//   * the original value is >= 1.0 (so we don't fight cutscene slow-mo, Stop=0, or other
//     deliberate decelerations).
// This keeps the in-game fast button working as the trigger; our hook just promotes its choice
// to whatever the slider currently says.
void proxy_unity_time_set_time_scale(float value, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    float multiplier = g_game_speed_multiplier.load(std::memory_order_relaxed);
    float effective = value;
    if (has_game_speed_override(multiplier) && value >= 1.0f) {
        effective = multiplier;
        g_launch_game_speed_applied.store(true, std::memory_order_release);
    }
    static std::atomic<int> hits{0};
    int previous = hits.fetch_add(1, std::memory_order_relaxed);
    if (previous < 20) {
        __android_log_print(ANDROID_LOG_INFO, "AppRuntime",
                            "Time.set_timeScale hit #%d incoming=%.3f effective=%.3f",
                            previous + 1, value, effective);
    }
    SHADOWHOOK_CALL_PREV(proxy_unity_time_set_time_scale, effective, method);
}

// Call the saved trampoline directly so dragging the slider applies the new tick rate on the
// same frame. Going through the trampoline (rather than the proxy) avoids hooking ourselves
// recursively and skips the >=1.0 guard so we can apply any value the user picks.
bool apply_unity_time_scale(float value) {
    if (g_orig_unity_time_set_time_scale == nullptr) {
        __android_log_print(ANDROID_LOG_WARN, "AppRuntime",
                            "apply_unity_time_scale skipped: orig not yet hooked (value=%.3f)",
                            value);
        return false;
    }
    using SetTimeScaleFn = void (*)(float, const void *);
    auto fn = reinterpret_cast<SetTimeScaleFn>(g_orig_unity_time_set_time_scale);
    __android_log_print(ANDROID_LOG_INFO, "AppRuntime",
                        "apply_unity_time_scale calling trampoline @ %p with value=%.3f",
                        fn, value);
    fn(value, nullptr);
    return true;
}

void write_switch_game_speed_cache(float multiplier) {
    void *instance = g_switch_game_speed_instance.load(std::memory_order_acquire);
    if (instance != nullptr && g_fields.switch_game_speed_value >= 0) {
        *reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(instance)
                                   + g_fields.switch_game_speed_value) = multiplier;
    }
}

void apply_launch_game_speed_if_needed(const char *reason) {
    float multiplier = g_game_speed_multiplier.load(std::memory_order_relaxed);
    if (!has_game_speed_override(multiplier)) return;
    bool expected = false;
    if (!g_launch_game_speed_applied.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel, std::memory_order_relaxed)) {
        return;
    }
    write_switch_game_speed_cache(multiplier);
    if (!apply_unity_time_scale(multiplier)) {
        g_launch_game_speed_applied.store(false, std::memory_order_release);
        return;
    }
    __android_log_print(ANDROID_LOG_INFO, "AppRuntime",
                        "launch game speed applied from %s value=%.3f",
                        reason != nullptr ? reason : "unknown", multiplier);
}

// UseCase_SwitchGameSpeed.ReflectSpeedUp(GameSpeedType speedType).
//
// `speedType` values follow `enum GameSpeedType { Normal=0, Fast=1, SuperFast=2, Stop=3 }`.
// When the live multiplier is >1 we promote the type to SuperFast(2) so the UI badge and any
// gating logic stay consistent, then overwrite `speedUpValue` with the slider value so the
// actual game tick runs at exactly that multiplier. The override is left untouched when
// the player picks Stop(3) so the in-menu pause UX still works.
void proxy_reflect_speed_up(void *self, int speed_type, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    float multiplier = g_game_speed_multiplier.load(std::memory_order_relaxed);
    int effective = speed_type;
    if (multiplier > 1.0f && speed_type != 2 /* SuperFast */ && speed_type != 3 /* Stop */) {
        effective = 2;
    }
    SHADOWHOOK_CALL_PREV(proxy_reflect_speed_up, self, effective, method);
    if (self != nullptr && has_game_speed_override(multiplier) && speed_type != 3 /* Stop */
            && g_fields.switch_game_speed_value >= 0) {
        *reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(self)
                                   + g_fields.switch_game_speed_value) = multiplier;
    }
}

// UseCase_SwitchGameSpeed.SetupNewMode(World world). Captures the live instance pointer for the
// realtime-slider JNI bridge, re-applies the persisted multiplier so it survives a fresh launch
// (a saved x10 from the previous session re-applies the very first frame this fires), and
// promotes the in-game speed-type badge to SuperFast so the on-screen indicator matches the
// actual tick rate.
void proxy_switch_game_speed_setup_new_mode(void *self, void *world, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    SHADOWHOOK_CALL_PREV(proxy_switch_game_speed_setup_new_mode, self, world, method);
    if (self != nullptr) {
        g_switch_game_speed_instance.store(self, std::memory_order_release);
    }
    float multiplier = g_game_speed_multiplier.load(std::memory_order_relaxed);
    if (has_game_speed_override(multiplier)) {
        write_switch_game_speed_cache(multiplier);
        if (g_orig_reflect_speed_up != nullptr) {
            using ReflectFn = void (*)(void *, int, const void *);
            auto fn = reinterpret_cast<ReflectFn>(g_orig_reflect_speed_up);
            fn(self, 2, nullptr);  // SuperFast - so the speed-type badge says "fast"
        }
        if (apply_unity_time_scale(multiplier)) {
            g_launch_game_speed_applied.store(true, std::memory_order_release);
        }
    }
}

// ServerManager.<VerifyBackupKeyAsync>d__119.MoveNext - read-only capture of the user-typed
// PlayFab id and backup key.
void proxy_server_manager_verify_backup_key_movenext(void *self, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    if (self != nullptr) {
        void *backup_key = read_object_field(self, g_fields.verify_state_backup_key);
        void *playfab_id = read_object_field(self, g_fields.verify_state_playfab_id);
        if (playfab_id != nullptr) {
            g_last_verify_playfab_id.store(playfab_id, std::memory_order_release);
        }
        if (backup_key != nullptr) {
            g_last_verify_backup_key.store(backup_key, std::memory_order_release);
        }
        static std::atomic<int> hits{0};
        int previous = hits.fetch_add(1, std::memory_order_relaxed);
        if (previous < 4) {
            ALOGI("VerifyBackupKey input captured #%d playFabId=%s backupKey=%s",
                  previous + 1,
                  managed_string_to_ascii(playfab_id).c_str(),
                  managed_string_to_ascii(backup_key).c_str());
        }
    }
    SHADOWHOOK_CALL_PREV(proxy_server_manager_verify_backup_key_movenext, self, method);
}

void proxy_playfab_execute_function(void *request, void *result_callback, void *error_callback,
                                    void *custom_data, void *extra_headers, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    log_execute_function_request(request, "static");
    SHADOWHOOK_CALL_PREV(proxy_playfab_execute_function,
                         request, result_callback, error_callback, custom_data, extra_headers,
                         method);
}

void proxy_playfab_instance_execute_function(void *self, void *request, void *result_callback,
                                             void *error_callback, void *custom_data,
                                             void *extra_headers, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    log_execute_function_request(request, "instance");
    SHADOWHOOK_CALL_PREV(proxy_playfab_instance_execute_function,
                         self, request, result_callback, error_callback, custom_data,
                         extra_headers, method);
}

void proxy_playfab_http_make_api_call_object(void *api_endpoint, void *full_url, void *request,
                                             int auth_type, void *result_callback,
                                             void *error_callback, void *custom_data,
                                             void *extra_headers, bool allow_queueing,
                                             void *auth_context, void *api_settings,
                                             void *instance_api, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    static std::atomic<int> hits{0};
    int previous = hits.fetch_add(1, std::memory_order_relaxed);
    if (previous < 24) ALOGI("PlayFabHttp._MakeApiCall<object> hit #%d", previous + 1);
    log_playfab_make_api_call(api_endpoint, full_url, request, auth_type, allow_queueing,
                              "PlayFabHttp._MakeApiCall<object>");
    SHADOWHOOK_CALL_PREV(proxy_playfab_http_make_api_call_object,
                         api_endpoint, full_url, request, auth_type, result_callback,
                         error_callback, custom_data, extra_headers, allow_queueing,
                         auth_context, api_settings, instance_api, method);
}

void proxy_playfab_unity_make_api_call(void *self, void *req_container_obj, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    static std::atomic<int> hits{0};
    int previous = hits.fetch_add(1, std::memory_order_relaxed);
    if (previous < 24) ALOGI("UnityHttp.MakeApiCall hit #%d req=%p", previous + 1,
                             req_container_obj);
    log_playfab_request_container(req_container_obj, "UnityHttp.MakeApiCall");
    SHADOWHOOK_CALL_PREV(proxy_playfab_unity_make_api_call, self, req_container_obj, method);
}

void *proxy_playfab_unity_post(void *self, void *req_container, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    static std::atomic<int> hits{0};
    int previous = hits.fetch_add(1, std::memory_order_relaxed);
    if (previous < 24) ALOGI("UnityHttp.Post hit #%d req=%p", previous + 1, req_container);
    log_playfab_request_container(req_container, "UnityHttp.Post");
    return SHADOWHOOK_CALL_PREV(proxy_playfab_unity_post, self, req_container, method);
}

void proxy_playfab_unity_on_response(void *self, void *response, void *req_container,
                                     const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    static std::atomic<int> hits{0};
    int previous = hits.fetch_add(1, std::memory_order_relaxed);
    if (previous < 24) ALOGI("UnityHttp.OnResponse hit #%d responseLen=%d req=%p",
                             previous + 1, managed_string_length(response), req_container);
    std::string body = managed_string_prefix(response, 768);
    if (is_interesting_playfab_text(body)) {
        ALOGI("UnityHttp.OnResponse response=%s", body.c_str());
    }
    log_playfab_request_container(req_container, "UnityHttp.OnResponse");
    SHADOWHOOK_CALL_PREV(proxy_playfab_unity_on_response, self, response, req_container, method);
    log_playfab_request_container(req_container, "UnityHttp.OnResponse.after");
}

void proxy_playfab_unity_on_error(void *self, void *error, void *req_container,
                                  const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    static std::atomic<int> hits{0};
    int previous = hits.fetch_add(1, std::memory_order_relaxed);
    if (previous < 24) ALOGW("UnityHttp.OnError hit #%d errorLen=%d req=%p",
                             previous + 1, managed_string_length(error), req_container);
    std::string message = managed_string_prefix(error, 512);
    ALOGW("UnityHttp.OnError error=%s", message.empty() ? "<empty>" : message.c_str());
    log_playfab_request_container(req_container, "UnityHttp.OnError");
    SHADOWHOOK_CALL_PREV(proxy_playfab_unity_on_error, self, error, req_container, method);
    log_playfab_request_container(req_container, "UnityHttp.OnError.after");
}

void proxy_playfab_http_on_api_result(void *self, void *req_container, const void *method) {
    SHADOWHOOK_STACK_SCOPE();
    static std::atomic<int> hits{0};
    int previous = hits.fetch_add(1, std::memory_order_relaxed);
    if (previous < 24) ALOGI("PlayFabHttp.OnPlayFabApiResult hit #%d req=%p",
                             previous + 1, req_container);
    log_playfab_request_container(req_container, "PlayFabHttp.Result");
    SHADOWHOOK_CALL_PREV(proxy_playfab_http_on_api_result, self, req_container, method);
    log_playfab_request_container(req_container, "PlayFabHttp.Result.after");
    apply_launch_game_speed_if_needed("PlayFabHttp.OnPlayFabApiResult");
}

struct HookSpec {
    const char *name;
    const char *image;
    const char *namespaze;
    const char *klass;
    const char *declaring_klass;
    const char *method;
    int param_count;
    void *proxy;
    void **original;
    bool required;
};

void *resolve_hook_target(const HookSpec &spec) {
    void *klass = find_il2cpp_class(spec.image, spec.namespaze, spec.klass, spec.declaring_klass);
    if (klass == nullptr) {
        ALOGW("hook class unresolved %s: %s/%s", spec.name, spec.image, spec.klass);
        return nullptr;
    }
    void *method = find_il2cpp_method(klass, spec.method, spec.param_count);
    if (method == nullptr) {
        ALOGW("hook method unresolved %s: %s/%s.%s/%d", spec.name, spec.image, spec.klass,
              spec.method, spec.param_count);
        return nullptr;
    }
    void *target = method_pointer(method);
    if (target == nullptr) {
        ALOGW("hook method has no executable pointer %s: %s/%s.%s/%d", spec.name,
              spec.image, spec.klass, spec.method, spec.param_count);
    }
    return target;
}

bool install_hook(const HookSpec &spec) {
    void *target = resolve_hook_target(spec);
    if (target == nullptr) {
        return false;
    }
    void *stub = shadowhook_hook_func_addr_2(
            target,
            spec.proxy,
            spec.original,
            SHADOWHOOK_HOOK_WITH_SHARED_MODE | SHADOWHOOK_HOOK_RECORD,
            kTargetLibrary,
            spec.name);
    if (stub == nullptr) {
        [[maybe_unused]] int err = shadowhook_get_errno();
        ALOGW("hook failed %s target=%p err=%d %s", spec.name, target, err,
              shadowhook_to_errmsg(err));
        return false;
    }
    ALOGI("hooked %s target=%p stub=%p", spec.name, target, stub);
    return true;
}

void log_recovered_state() {
    ALOGI("recovered feature state: damage=%d defense=%d god=%d free_shop=%d "
          "server_integrity_bypass=%d actk_bypass=%d forge_backup_success=%d game_speed=%.2f "
          "integrity_check_obs=%d token_obs=%d project_number_injected=%d "
          "backup_success_forged=%d",
          g_damage_multiplier.load(std::memory_order_relaxed),
          g_defense_multiplier.load(std::memory_order_relaxed),
          g_god_mode.load(std::memory_order_relaxed) ? 1 : 0,
          g_free_shop.load(std::memory_order_relaxed) ? 1 : 0,
          g_server_integrity_bypass.load(std::memory_order_relaxed) ? 1 : 0,
          g_actk_bypass.load(std::memory_order_relaxed) ? 1 : 0,
          g_forge_backup_success.load(std::memory_order_relaxed) ? 1 : 0,
          g_game_speed_multiplier.load(std::memory_order_relaxed),
          g_integrity_check_observed.load(std::memory_order_relaxed),
          g_integrity_token_observed.load(std::memory_order_relaxed),
          g_integrity_project_number_injected.load(std::memory_order_relaxed),
          g_backup_success_forged.load(std::memory_order_relaxed));
    ALOGI("old module feature numbers: 0=damage, 1=defense, 2=god mode, 3=free shop");
}

[[maybe_unused]] int install_recovered_hooks() {
    void *big_integer_class = find_il2cpp_class("Assembly-CSharp", "", "BigInteger");
    void *big_integer_ctor = find_il2cpp_method(big_integer_class, ".ctor", 1);
    g_big_integer_ctor_double = reinterpret_cast<BigIntegerCtorDouble>(
            method_pointer(big_integer_ctor));
    if (g_big_integer_ctor_double == nullptr) {
        ALOGW("BigInteger(double) constructor unresolved");
        g_big_integer_ctor_double = nullptr;
        return -1;
    }

    HookSpec hooks[] = {
            {"UseCase_SwitchGameSpeed.ReflectSpeedUp",
             "Assembly-CSharp", "", "UseCase_SwitchGameSpeed", nullptr, "ReflectSpeedUp", 1,
             reinterpret_cast<void *>(proxy_reflect_speed_up),
             &g_orig_reflect_speed_up, true},
            {"UnityEngine.Time.set_timeScale",
             "UnityEngine.CoreModule", "UnityEngine", "Time", nullptr, "set_timeScale", 1,
             reinterpret_cast<void *>(proxy_unity_time_set_time_scale),
             &g_orig_unity_time_set_time_scale, true},
            {"UseCase_SwitchGameSpeed.SetupNewMode",
             "Assembly-CSharp", "", "UseCase_SwitchGameSpeed", nullptr, "SetupNewMode", 1,
             reinterpret_cast<void *>(proxy_switch_game_speed_setup_new_mode),
             &g_orig_switch_game_speed_setup, true},
            {"MonsterParams.get_damageTakenRate",
             "Assembly-CSharp", "", "MonsterParams", nullptr, "get_damageTakenRate", 0,
             reinterpret_cast<void *>(proxy_monster_damage_taken),
             &g_orig_monster_damage_taken, true},
            {"Params.get_damageTakenRate",
             "Assembly-CSharp", "", "Params", nullptr, "get_damageTakenRate", 0,
             reinterpret_cast<void *>(proxy_params_damage_taken),
             &g_orig_params_damage_taken, true},
            {"PlayerParams.get_damageTakenRate",
             "Assembly-CSharp", "", "PlayerParams", nullptr, "get_damageTakenRate", 0,
             reinterpret_cast<void *>(proxy_player_damage_taken),
             &g_orig_player_damage_taken, true},
            {"PlayerScaledParams.get_damageTakenRate",
             "Assembly-CSharp", "", "PlayerScaledParams", nullptr, "get_damageTakenRate", 0,
             reinterpret_cast<void *>(proxy_player_scaled_damage_taken),
             &g_orig_player_scaled_damage_taken, true},
            {"UniquePlayerParams.get_damageTakenRate",
             "Assembly-CSharp", "", "UniquePlayerParams", nullptr, "get_damageTakenRate", 0,
             reinterpret_cast<void *>(proxy_unique_player_damage_taken),
             &g_orig_unique_player_damage_taken, true},
            {"Fighter.DecreaseHp",
             "Assembly-CSharp", "", "Fighter", nullptr, "DecreaseHp", 1,
             reinterpret_cast<void *>(proxy_fighter_decrease_hp),
             &g_orig_fighter_decrease_hp, true},
            {"Fighter.DecreaseHpWithoutSpGuard",
             "Assembly-CSharp", "", "Fighter", nullptr, "DecreaseHpWithoutSpGuard", 1,
             reinterpret_cast<void *>(proxy_fighter_decrease_hp_without_sp_guard),
             &g_orig_fighter_decrease_hp_without_sp_guard, true},
            {"ShopMaster.CalcPrice",
             "Assembly-CSharp", "", "ShopMaster", nullptr, "CalcPrice", 0,
             reinterpret_cast<void *>(proxy_shop_master_calc_price),
             &g_orig_shop_master_calc_price, true},
            {"ShopMaster.GetPriceType",
             "Assembly-CSharp", "", "ShopMaster", nullptr, "GetPriceType", 0,
             reinterpret_cast<void *>(proxy_shop_master_get_price_type),
             &g_orig_shop_master_get_price_type, true},
            {"ShopMaster.get_IsIAP",
             "Assembly-CSharp", "", "ShopMaster", nullptr, "get_IsIAP", 0,
             reinterpret_cast<void *>(proxy_shop_master_get_is_iap),
             &g_orig_shop_master_get_is_iap, true},
            {"UseCase_Purchase.CanPurchase",
             "Assembly-CSharp", "", "UseCase_Purchase", nullptr, "CanPurchase", 1,
             reinterpret_cast<void *>(proxy_purchase_can_purchase),
             &g_orig_purchase_can_purchase, true},
            {"UseCase_ViewSeasonalShopMenu.CanPurchase",
             "Assembly-CSharp", "", "UseCase_ViewSeasonalShopMenu", nullptr, "CanPurchase", 1,
             reinterpret_cast<void *>(proxy_seasonal_shop_can_purchase),
             &g_orig_seasonal_shop_can_purchase, true},
            {"UseCase_GameEvent.CanPurchase",
             "Assembly-CSharp", "", "UseCase_GameEvent", nullptr, "CanPurchase", 1,
             reinterpret_cast<void *>(proxy_game_event_can_purchase),
             &g_orig_game_event_can_purchase, true},
            {"Utils.CheckIfIsEnough",
             "Assembly-CSharp", "", "Utils", nullptr, "CheckIfIsEnough", 3,
             reinterpret_cast<void *>(proxy_utils_check_if_is_enough),
             &g_orig_utils_check_if_is_enough, true},
            {"Utils.Consume",
             "Assembly-CSharp", "", "Utils", nullptr, "Consume", 3,
             reinterpret_cast<void *>(proxy_utils_consume),
             &g_orig_utils_consume, true},
            {"SoldierData.CheckIfApIsEnough",
             "Assembly-CSharp", "", "SoldierData", nullptr, "CheckIfApIsEnough", 1,
             reinterpret_cast<void *>(proxy_soldier_check_ap),
             &g_orig_soldier_check_ap, true},
            {"SoldierData.Consume",
             "Assembly-CSharp", "", "SoldierData", nullptr, "Consume", 1,
             reinterpret_cast<void *>(proxy_soldier_consume_ap),
             &g_orig_soldier_consume_ap, true},
            {"RogueServerCode.get_IsIntegrityError",
             "Assembly-CSharp", "", "RogueServerCode", nullptr, "get_IsIntegrityError", 0,
             reinterpret_cast<void *>(proxy_rogue_server_code_is_integrity_error),
             &g_orig_rogue_server_code_is_integrity_error, true},
            {"RogueServerCode.get_IsSuccess",
             "Assembly-CSharp", "", "RogueServerCode", nullptr, "get_IsSuccess", 0,
             reinterpret_cast<void *>(proxy_rogue_server_code_is_success),
             &g_orig_rogue_server_code_is_success, true},
            {"ServerManager.<PrepareIntegrityCheck>.MoveNext",
             "Assembly-CSharp", "", "<PrepareIntegrityCheck>d__113", "ServerManager",
             "MoveNext", 0,
             reinterpret_cast<void *>(proxy_server_manager_prepare_integrity_movenext),
             &g_orig_server_manager_prepare_integrity_movenext, true},
            {"ServerManager.<RequestIntegrityTokenAsync>.MoveNext",
             "Assembly-CSharp", "", "<RequestIntegrityTokenAsync>d__114", "ServerManager",
             "MoveNext", 0,
             reinterpret_cast<void *>(proxy_server_manager_request_integrity_movenext),
             &g_orig_server_manager_request_integrity_movenext, true},
            {"ServerManager.<PrepareIntegrityCheck>.OnSuccess",
             "Assembly-CSharp", "", "<>c__DisplayClass113_0", "ServerManager",
             "<PrepareIntegrityCheck>g__OnSuccess|0", 1,
             reinterpret_cast<void *>(proxy_prepare_integrity_on_success),
             &g_orig_prepare_integrity_on_success, true},
            {"PlayFabCloudScriptAPI.ExecuteFunction",
             "PlayFab", "PlayFab", "PlayFabCloudScriptAPI", nullptr, "ExecuteFunction", 5,
             reinterpret_cast<void *>(proxy_playfab_execute_function),
             &g_orig_playfab_execute_function, true},
            {"PlayFabCloudScriptInstanceAPI.ExecuteFunction",
             "PlayFab", "PlayFab", "PlayFabCloudScriptInstanceAPI", nullptr,
             "ExecuteFunction", 5,
             reinterpret_cast<void *>(proxy_playfab_instance_execute_function),
             &g_orig_playfab_instance_execute_function, true},
            {"PlayFabHttp._MakeApiCall<object>",
             "PlayFab", "PlayFab.Internal", "PlayFabHttp", nullptr, "_MakeApiCall", 12,
             reinterpret_cast<void *>(proxy_playfab_http_make_api_call_object),
             &g_orig_playfab_http_make_api_call_object, true},
            {"PlayFabUnityHttp.MakeApiCall",
             "PlayFab", "PlayFab.Internal", "PlayFabUnityHttp", nullptr, "MakeApiCall", 1,
             reinterpret_cast<void *>(proxy_playfab_unity_make_api_call),
             &g_orig_playfab_unity_make_api_call, true},
            {"PlayFabUnityHttp.Post",
             "PlayFab", "PlayFab.Internal", "PlayFabUnityHttp", nullptr, "Post", 1,
             reinterpret_cast<void *>(proxy_playfab_unity_post),
             &g_orig_playfab_unity_post, true},
            {"PlayFabUnityHttp.OnResponse",
             "PlayFab", "PlayFab.Internal", "PlayFabUnityHttp", nullptr, "OnResponse", 2,
             reinterpret_cast<void *>(proxy_playfab_unity_on_response),
             &g_orig_playfab_unity_on_response, true},
            {"PlayFabUnityHttp.OnError",
             "PlayFab", "PlayFab.Internal", "PlayFabUnityHttp", nullptr, "OnError", 2,
             reinterpret_cast<void *>(proxy_playfab_unity_on_error),
             &g_orig_playfab_unity_on_error, true},
            {"PlayFabHttp.OnPlayFabApiResult",
             "PlayFab", "PlayFab.Internal", "PlayFabHttp", nullptr, "OnPlayFabApiResult", 1,
             reinterpret_cast<void *>(proxy_playfab_http_on_api_result),
             &g_orig_playfab_http_on_api_result, true},
            {"IntegrityManager.RequestIntegrityToken",
             "Google.Play.Integrity", "Google.Play.Integrity", "IntegrityManager", nullptr,
             "RequestIntegrityToken", 1,
             reinterpret_cast<void *>(proxy_integrity_manager_request_token),
             &g_orig_integrity_manager_request_token, true},
            {"IntegrityTokenRequest..ctor",
             "Google.Play.Integrity", "Google.Play.Integrity", "IntegrityTokenRequest",
             nullptr, ".ctor", 2,
             reinterpret_cast<void *>(proxy_integrity_token_request_ctor),
             &g_orig_integrity_token_request_ctor, true},
            {"IntegrityTokenResponse..ctor",
             "Google.Play.Integrity", "Google.Play.Integrity", "IntegrityTokenResponse",
             nullptr, ".ctor", 1,
             reinterpret_cast<void *>(proxy_integrity_token_response_ctor),
             &g_orig_integrity_token_response_ctor, true},
            {"ServerManager.<IssueBackupKeyAsync>.OnSuccess",
             "Assembly-CSharp", "", "<>c__DisplayClass118_0", "ServerManager",
             "<IssueBackupKeyAsync>g__OnSuccess|0", 1,
             reinterpret_cast<void *>(proxy_issue_backup_key_on_success),
             &g_orig_issue_backup_key_on_success, true},
            {"ServerManager.<IssueBackupKeyAsync>.OnError",
             "Assembly-CSharp", "", "<>c__DisplayClass118_0", "ServerManager",
             "<IssueBackupKeyAsync>g__OnError|1", 1,
             reinterpret_cast<void *>(proxy_issue_backup_key_on_error),
             &g_orig_issue_backup_key_on_error, true},
            {"ServerManager.<VerifyBackupKeyAsync>.MoveNext",
             "Assembly-CSharp", "", "<VerifyBackupKeyAsync>d__119", "ServerManager",
             "MoveNext", 0,
             reinterpret_cast<void *>(proxy_server_manager_verify_backup_key_movenext),
             &g_orig_server_manager_verify_backup_key_movenext, true},
            {"ServerManager.<VerifyBackupKeyAsync>.OnSuccess",
             "Assembly-CSharp", "", "<>c__DisplayClass119_0", "ServerManager",
             "<VerifyBackupKeyAsync>g__OnSuccess|0", 1,
             reinterpret_cast<void *>(proxy_verify_backup_key_on_success),
             &g_orig_verify_backup_key_on_success, true},
            {"ServerManager.<VerifyBackupKeyAsync>.OnError",
             "Assembly-CSharp", "", "<>c__DisplayClass119_0", "ServerManager",
             "<VerifyBackupKeyAsync>g__OnError|1", 1,
             reinterpret_cast<void *>(proxy_verify_backup_key_on_error),
             &g_orig_verify_backup_key_on_error, true},
            {"RogueServerCode..ctor",
             "Assembly-CSharp", "", "RogueServerCode", nullptr, ".ctor", 2,
             reinterpret_cast<void *>(proxy_rogue_server_code_ctor),
             &g_orig_rogue_server_code_ctor, true},
    };

    int installed = 0;
    int required_failed = 0;
    for (const HookSpec &hook : hooks) {
        if (install_hook(hook)) {
            ++installed;
        } else if (hook.required) {
            ++required_failed;
        }
    }
    if (required_failed > 0) {
        ALOGW("required IL2CPP hook resolution failures=%d installed=%d", required_failed,
              installed);
        return -required_failed;
    }
    return installed;
}

int proxy_il2cpp_init(const char *domain_name) {
    SHADOWHOOK_STACK_SCOPE();
    int result = SHADOWHOOK_CALL_PREV(proxy_il2cpp_init, domain_name);
    install_after_il2cpp_init("il2cpp_init");
    return result;
}

int proxy_il2cpp_init_utf16(const uint16_t *domain_name) {
    SHADOWHOOK_STACK_SCOPE();
    int result = SHADOWHOOK_CALL_PREV(proxy_il2cpp_init_utf16, domain_name);
    install_after_il2cpp_init("il2cpp_init_utf16");
    return result;
}

void *proxy_il2cpp_runtime_invoke(void *method, void *obj, void **params, void **exc) {
    SHADOWHOOK_STACK_SCOPE();
    void *result = SHADOWHOOK_CALL_PREV(proxy_il2cpp_runtime_invoke, method, obj, params, exc);
    install_after_il2cpp_init("il2cpp_runtime_invoke");
    return result;
}

bool install_bootstrap_hook(const char *symbol, void *proxy, void **original) {
    uintptr_t address = resolve_loaded_symbol(g_il2cpp_base, g_il2cpp_path, symbol);
    if (address == 0) {
        ALOGW("bootstrap export unresolved: %s", symbol);
        return false;
    }
    void *stub = shadowhook_hook_func_addr_2(
            reinterpret_cast<void *>(address),
            proxy,
            original,
            SHADOWHOOK_HOOK_WITH_SHARED_MODE | SHADOWHOOK_HOOK_RECORD,
            kTargetLibrary,
            symbol);
    if (stub == nullptr) {
        [[maybe_unused]] int err = shadowhook_get_errno();
        ALOGW("bootstrap hook failed %s target=%p err=%d %s", symbol,
              reinterpret_cast<void *>(address), err, shadowhook_to_errmsg(err));
        return false;
    }
    ALOGI("bootstrap hooked %s target=%p stub=%p", symbol,
          reinterpret_cast<void *>(address), stub);
    return true;
}

[[maybe_unused]] bool install_il2cpp_bootstrap_hooks() {
    bool any = false;
    any |= install_bootstrap_hook("il2cpp_init",
                                  reinterpret_cast<void *>(proxy_il2cpp_init),
                                  &g_orig_il2cpp_init);
    any |= install_bootstrap_hook("il2cpp_init_utf16",
                                  reinterpret_cast<void *>(proxy_il2cpp_init_utf16),
                                  &g_orig_il2cpp_init_utf16);
    any |= install_bootstrap_hook("il2cpp_runtime_invoke",
                                  reinterpret_cast<void *>(proxy_il2cpp_runtime_invoke),
                                  &g_orig_il2cpp_runtime_invoke);
    return any;
}

void install_after_il2cpp_init(const char *reason) {
    std::call_once(g_il2cpp_metadata_install_once, [reason]() {
        [[maybe_unused]] const char *install_reason =
                reason != nullptr ? reason : "runtime-ready";
        ALOGI("IL2CPP metadata install starting after %s", install_reason);
        if (!resolve_il2cpp_api(g_il2cpp_base, g_il2cpp_path)) {
            g_install_result.store(-23, std::memory_order_relaxed);
            return;
        }
        if (!wait_for_il2cpp_metadata(600, 10 * 1000)) {
            ALOGW("IL2CPP metadata was not ready before timeout");
            g_install_result.store(-24, std::memory_order_relaxed);
            return;
        }
        if (!resolve_managed_field_offsets()) {
            ALOGW("required managed field offsets did not resolve from IL2CPP metadata");
            g_install_result.store(-25, std::memory_order_relaxed);
            return;
        }

        int installed = install_recovered_hooks();
        if (installed <= 0) {
            g_install_result.store(-22, std::memory_order_relaxed);
            return;
        }

        ALOGI("installed %d recovered hooks", installed);
        log_recovered_state();
        g_install_result.store(0, std::memory_order_relaxed);
    });
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

    g_il2cpp_base = wait_for_loaded_library_base(kTargetLibrary, 1200, 10 * 1000);
    if (g_il2cpp_base == 0) {
        ALOGW("%s was not loaded before timeout", kTargetLibrary);
        g_install_result.store(-20, std::memory_order_relaxed);
        return;
    }

    g_il2cpp_path = find_loaded_library_path(kTargetLibrary, &g_il2cpp_base);
    ALOGI("%s base=%p path=%s", kTargetLibrary, reinterpret_cast<void *>(g_il2cpp_base),
          g_il2cpp_path.empty() ? "<unknown>" : g_il2cpp_path.c_str());

    if (!install_il2cpp_bootstrap_hooks()) {
        ALOGW("no IL2CPP bootstrap hook installed");
        g_install_result.store(-26, std::memory_order_relaxed);
        return;
    }
    ALOGI("IL2CPP bootstrap hooks installed; metadata hooks will install after runtime init");
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

jint native_get_install_status(JNIEnv *, jclass) {
    return g_install_result.load(std::memory_order_relaxed);
}

void native_sync_feature_state(JNIEnv *, jclass, jint damage, jint defense,
                               jboolean god_mode, jboolean free_shop,
                               jboolean server_integrity_bypass, jboolean actk_bypass,
                               jboolean forge_backup_success,
                               jfloat game_speed_multiplier) {
    g_damage_multiplier.store(damage < 1 ? 1 : damage, std::memory_order_relaxed);
    g_defense_multiplier.store(defense < 1 ? 1 : defense, std::memory_order_relaxed);
    g_god_mode.store(god_mode == JNI_TRUE, std::memory_order_relaxed);
    g_free_shop.store(free_shop == JNI_TRUE, std::memory_order_relaxed);
    g_server_integrity_bypass.store(server_integrity_bypass == JNI_TRUE, std::memory_order_relaxed);
    g_actk_bypass.store(actk_bypass == JNI_TRUE, std::memory_order_relaxed);
    g_forge_backup_success.store(forge_backup_success == JNI_TRUE, std::memory_order_relaxed);
    float previous_speed = g_game_speed_multiplier.load(std::memory_order_relaxed);
    float next_speed = sanitize_game_speed_multiplier(static_cast<float>(game_speed_multiplier));
    g_game_speed_multiplier.store(next_speed, std::memory_order_relaxed);
    if (std::fabs(previous_speed - next_speed) > 0.001f) {
        g_launch_game_speed_applied.store(false, std::memory_order_release);
    }
    log_recovered_state();
    // Do NOT call apply_unity_time_scale() here. nativeSyncFeatureState fires from the Java
    // Application onCreate path before Unity's IL2CPP type metadata is fully wired up, so
    // dispatching into UnityEngine.Time.set_timeScale crashes with a SIGSEGV inside the
    // method-info dereference. Persisted launch speed is applied later from existing IL2CPP
    // callbacks that prove managed game code is already running.
}

// Fast-path setter used by the slider. Writes the atomic, refreshes the IL2CPP cache so the
// in-game speed badge stays consistent with what we actually applied, then calls
// UnityEngine.Time.set_timeScale through the saved trampoline so Unity's engine tick rate
// changes on the next frame.
void native_set_game_speed_multiplier(JNIEnv *, jclass, jfloat value) {
    float multiplier = sanitize_game_speed_multiplier(static_cast<float>(value));
    g_game_speed_multiplier.store(multiplier, std::memory_order_relaxed);
    g_launch_game_speed_applied.store(false, std::memory_order_release);
    write_switch_game_speed_cache(multiplier);
    if (apply_unity_time_scale(multiplier)) {
        g_launch_game_speed_applied.store(true, std::memory_order_release);
    }
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
    out += "\nforge_backup_success="
            + std::string(g_forge_backup_success.load(std::memory_order_relaxed) ? "1" : "0");
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.3f",
                      g_game_speed_multiplier.load(std::memory_order_relaxed));
        out += "\ngame_speed_multiplier=";
        out += buf;
    }
    out += "\nswitch_game_speed_instance_captured="
            + std::string(g_switch_game_speed_instance.load(std::memory_order_relaxed)
                                  != nullptr ? "1" : "0");
    out += "\nlaunch_game_speed_applied="
            + std::string(g_launch_game_speed_applied.load(std::memory_order_relaxed)
                                  ? "1" : "0");
    out += "\nintegrity_check_observed="
            + std::to_string(g_integrity_check_observed.load(std::memory_order_relaxed));
    out += "\nintegrity_token_observed="
            + std::to_string(g_integrity_token_observed.load(std::memory_order_relaxed));
    out += "\nproject_number_injected="
            + std::to_string(g_integrity_project_number_injected.load(std::memory_order_relaxed));
    out += "\nbackup_success_forged="
            + std::to_string(g_backup_success_forged.load(std::memory_order_relaxed));
    out += "\nsuccess_singleton_captured="
            + std::string(g_rogue_server_code_success.load(std::memory_order_relaxed)
                                  != nullptr ? "1" : "0");
    out += "\nil2cpp_string_new_resolved="
            + std::string(g_il2cpp_string_new != nullptr ? "1" : "0");
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
            {"nativeGetInstallStatus", "()I",
             reinterpret_cast<void *>(native_get_install_status)},
            {"nativeSyncFeatureState", "(IIZZZZZF)V",
             reinterpret_cast<void *>(native_sync_feature_state)},
            {"nativeSetGameSpeedMultiplier", "(F)V",
             reinterpret_cast<void *>(native_set_game_speed_multiplier)},
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
