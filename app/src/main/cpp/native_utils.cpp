#include "native_utils.h"

#include <android/log.h>
#include <shadowhook.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>
#include <link.h>

#ifndef TEMPLATE_VERBOSE_LOGS
#define TEMPLATE_VERBOSE_LOGS 0
#endif

#if TEMPLATE_VERBOSE_LOGS
#define NU_LOG_TAG "AppRuntime"
#define NU_LOGI(...) __android_log_print(ANDROID_LOG_INFO, NU_LOG_TAG, __VA_ARGS__)
#define NU_LOGW(...) __android_log_print(ANDROID_LOG_WARN, NU_LOG_TAG, __VA_ARGS__)
#else
#define NU_LOGI(...) ((void)0)
#define NU_LOGW(...) ((void)0)
#endif

namespace native_utils {

namespace {

// Allocation/resolver failures must not unwind a C++ exception across the JVM's JNI boundary.
template<auto Function> struct JniBoundary;
template<class Return, class... Args, Return (*Function)(JNIEnv*, jclass, Args...)>
struct JniBoundary<Function> {
    static Return call(JNIEnv* env, jclass cls, Args... args) noexcept {
        try { return Function(env, cls, args...); }
        catch (...) { return Return{}; }
    }
};

struct Mapping {
    uintptr_t start{0};
    uintptr_t end{0};
    uintptr_t offset{0};
    char perms[5]{};
    std::string path;
};

struct AdrpDecode {
    bool valid{false};
    int rd{-1};
    uintptr_t target_page{0};
};

AdrpDecode decode_adrp(uint32_t insn, uintptr_t pc) {
    AdrpDecode out;
    if ((insn & 0x9F000000) != 0x90000000) return out;
    int immlo = (insn >> 29) & 0x3;
    int immhi = (insn >> 5) & 0x7FFFF;
    int64_t imm = (int64_t)((immhi << 2) | immlo);
    if (imm & (1LL << 20)) imm |= ~((1LL << 21) - 1);
    out.target_page = (pc & ~0xFFFULL) + (imm << 12);
    out.rd = insn & 0x1F;
    out.valid = true;
    return out;
}

struct AddImmDecode {
    bool valid{false};
    int rn{-1};
    int rd{-1};
    uint32_t imm12{0};
    int shift{0};
};

AddImmDecode decode_add_imm(uint32_t insn) {
    AddImmDecode out;
    if ((insn & 0xFF800000) != 0x91000000) return out;
    out.shift = (insn >> 22) & 0x1;
    out.imm12 = (insn >> 10) & 0xFFF;
    out.rn = (insn >> 5) & 0x1F;
    out.rd = insn & 0x1F;
    out.valid = true;
    return out;
}

bool is_adrp(uint32_t insn) {
    return (insn & 0x9F000000) == 0x90000000;
}

bool completes_page_ref(uint32_t insn, const AdrpDecode &adrp, uint32_t target_offset) {
    AddImmDecode add = decode_add_imm(insn);
    if (add.valid && add.rn == adrp.rd && add.shift == 0 && add.imm12 == target_offset) {
        return true;
    }

    // LDR Wt, [Xn, #imm12*4]
    if ((insn & 0xFFC00000) == 0xB9400000) {
        uint32_t ldr_imm12 = (insn >> 10) & 0xFFF;
        int ldr_rn = (insn >> 5) & 0x1F;
        if (ldr_rn == adrp.rd && ldr_imm12 * 4 == target_offset) return true;
    }

    // LDR Xt, [Xn, #imm12*8]
    if ((insn & 0xFFC00000) == 0xF9400000) {
        uint32_t ldr_imm12 = (insn >> 10) & 0xFFF;
        int ldr_rn = (insn >> 5) & 0x1F;
        if (ldr_rn == adrp.rd && ldr_imm12 * 8 == target_offset) return true;
    }

    // LDRB Wt, [Xn, #imm12]
    if ((insn & 0xFFC00000) == 0x39400000) {
        uint32_t ldr_imm12 = (insn >> 10) & 0xFFF;
        int ldr_rn = (insn >> 5) & 0x1F;
        if (ldr_rn == adrp.rd && ldr_imm12 == target_offset) return true;
    }

    return false;
}

bool is_function_prologue(uint32_t insn) {
    if ((insn & 0xFFC003E0) == 0xA9800000) return true;
    if ((insn & 0xFF0003E0) == 0xA9000000) return true;
    if (insn == 0xD503233F) return true;
    if ((insn & 0xFFC003FF) == 0xA98003FD) return true;
    if ((insn & 0xFF0003FF) == 0xA90003FD) return true;
    if ((insn & 0x7F000000) == 0x51000000) {
        int rd = insn & 0x1F;
        if (rd == 31) return true;
    }
    return false;
}

std::vector<Mapping> read_maps() {
    std::vector<Mapping> out;
    FILE *f = std::fopen("/proc/self/maps", "r");
    if (f == nullptr) return out;
    char line[1024];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        unsigned long start = 0;
        unsigned long end = 0;
        unsigned long fileoff = 0;
        char perms[5] = {0};
        char path_buf[512] = {0};
        int scanned = std::sscanf(line, "%lx-%lx %4s %lx %*s %*lu %511[^\n]",
                                  &start, &end, perms, &fileoff, path_buf);
        if (scanned < 3) continue;
        Mapping m;
        m.start = static_cast<uintptr_t>(start);
        m.end = static_cast<uintptr_t>(end);
        m.offset = static_cast<uintptr_t>(fileoff);
        std::strncpy(m.perms, perms, 4);
        m.perms[4] = '\0';
        if (scanned >= 4) {
            const char *trim = path_buf;
            while (*trim == ' ' || *trim == '\t') ++trim;
            m.path.assign(trim);
        }
        out.push_back(std::move(m));
    }
    std::fclose(f);
    return out;
}

std::string jstring_to_string(JNIEnv *env, jstring value) {
    if (value == nullptr) return {};
    const char *chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) return {};
    std::string out(chars);
    env->ReleaseStringUTFChars(value, chars);
    return out;
}

bool library_matches(const std::string &path, const std::string &name) {
    if (path.empty() || name.empty()) return false;
    if (name.find('/') != std::string::npos) return path == name;
    std::size_t pos = path.rfind('/');
    std::string base = pos == std::string::npos ? path : path.substr(pos + 1);
    return base == name;
}

bool checked_range_end(uintptr_t start, std::size_t len, uintptr_t *end) {
    if (end == nullptr || len > UINTPTR_MAX - start) return false;
    *end = start + len;
    return true;
}

const Mapping *containing_mapping(const std::vector<Mapping> &maps, uintptr_t addr, std::size_t len) {
    uintptr_t end = 0;
    if (!checked_range_end(addr, len, &end)) return nullptr;
    for (const auto &m : maps) {
        if (addr >= m.start && end <= m.end) return &m;
    }
    return nullptr;
}

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;
};

bool is_hex_char(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool parse_pattern(const std::string &pattern, Pattern &out) {
    out.bytes.clear();
    out.mask.clear();
    const char *p = pattern.c_str();
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') break;
        if (*p == '?') {
            out.bytes.push_back(0);
            out.mask.push_back(false);
            ++p;
            if (*p == '?') ++p;
            continue;
        }
        if (!is_hex_char(p[0]) || !is_hex_char(p[1])) {
            out.bytes.clear();
            out.mask.clear();
            return false;
        }
        char hex[3] = {p[0], p[1], '\0'};
        char *end = nullptr;
        long byte = std::strtol(hex, &end, 16);
        if (end == hex || byte < 0 || byte > 0xFF) return false;
        out.bytes.push_back(static_cast<uint8_t>(byte));
        out.mask.push_back(true);
        p += 2;
    }
    return !out.bytes.empty();
}

// Shared matcher for the checked JNI mapping scan and the caller-bounded C++ scan.
uintptr_t scan_pattern(uintptr_t start, uintptr_t end, const Pattern &pattern) {
    if (start == 0 || end <= start || pattern.bytes.empty() || end - start < pattern.bytes.size()) return 0;
    const auto *data = reinterpret_cast<const uint8_t *>(start);
    const std::size_t last = end - start - pattern.bytes.size();
    for (std::size_t offset = 0; offset <= last; ++offset) {
        bool matches = true;
        for (std::size_t i = 0; i < pattern.bytes.size(); ++i) {
            if (pattern.mask[i] && data[offset + i] != pattern.bytes[i]) {
                matches = false;
                break;
            }
        }
        if (matches) return start + offset;
    }
    return 0;
}

jlongArray native_find_module(JNIEnv *env, jclass, jstring name_j) {
    std::string name = jstring_to_string(env, name_j);
    jlongArray out = env->NewLongArray(2);
    if (out == nullptr) return nullptr;
    jlong values[2] = {0, 0};
    if (name.empty()) {
        env->SetLongArrayRegion(out, 0, 2, values);
        return out;
    }
    const auto info = find_module_info(name.c_str());
    values[0] = static_cast<jlong>(info.valid ? info.base : 0);
    values[1] = static_cast<jlong>(info.valid && info.end > info.base ? info.end - info.base : 0);
    env->SetLongArrayRegion(out, 0, 2, values);
    return out;
}

jlong native_pattern_scan(JNIEnv *env, jclass, jlong base_j, jlong size_j, jstring pattern_j) {
    if (base_j == 0 || size_j <= 0 || pattern_j == nullptr) return 0;
    std::string pat = jstring_to_string(env, pattern_j);
    Pattern p;
    if (!parse_pattern(pat, p)) return 0;

    auto maps = read_maps();
    uintptr_t base = static_cast<uintptr_t>(base_j);
    std::size_t size = static_cast<std::size_t>(size_j);
    uintptr_t limit = 0;
    if (!checked_range_end(base, size, &limit)) return 0;

    for (const auto &m : maps) {
        if (m.perms[0] != 'r') continue;
        uintptr_t scan_start = std::max(base, m.start);
        uintptr_t scan_limit = std::min(limit, m.end);
        if (scan_limit <= scan_start) continue;
        const uintptr_t match = scan_pattern(scan_start, scan_limit, p);
        if (match != 0) return static_cast<jlong>(match);
    }
    return 0;
}

jlong native_resolve_symbol(JNIEnv *env, jclass, jstring lib_j, jstring sym_j) {
    std::string lib = jstring_to_string(env, lib_j);
    std::string sym = jstring_to_string(env, sym_j);
    if (lib.empty() || sym.empty()) return 0;
    const auto info = find_module_info(lib.c_str());
    return static_cast<jlong>(info.valid ? resolve_export(info.base, sym.c_str()) : 0);
}

jbyteArray native_read_memory(JNIEnv *env, jclass, jlong address_j, jint length) {
    if (address_j == 0 || length <= 0) return nullptr;
    auto maps = read_maps();
    uintptr_t address = static_cast<uintptr_t>(address_j);
    const Mapping *m = containing_mapping(maps, address, static_cast<std::size_t>(length));
    if (m == nullptr || m->perms[0] != 'r') return nullptr;
    jbyteArray out = env->NewByteArray(length);
    if (out == nullptr) return nullptr;
    env->SetByteArrayRegion(out, 0, length, reinterpret_cast<const jbyte *>(address));
    return out;
}

jboolean native_write_memory(JNIEnv *env, jclass, jlong address_j, jbyteArray data_j) {
    if (address_j == 0 || data_j == nullptr) return JNI_FALSE;
    jsize length = env->GetArrayLength(data_j);
    constexpr jsize kMaxWriteBytes = 8 * 1024 * 1024;
    if (length <= 0 || length > kMaxWriteBytes) return JNI_FALSE;

    auto maps = read_maps();
    uintptr_t address = static_cast<uintptr_t>(address_j);
    const Mapping *m = containing_mapping(maps, address, static_cast<std::size_t>(length));
    if (m == nullptr) return JNI_FALSE;

    // Complete all allocation/JNI work before changing page permissions.
    std::vector<jbyte> buffer(static_cast<std::size_t>(length));
    env->GetByteArrayRegion(data_j, 0, length, buffer.data());
    if (env->ExceptionCheck()) return JNI_FALSE;

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return JNI_FALSE;
    uintptr_t mask = static_cast<uintptr_t>(page_size) - 1;
    uintptr_t aligned_start = address & ~mask;
    uintptr_t raw_end = 0;
    if (!checked_range_end(address, static_cast<std::size_t>(length), &raw_end)) return JNI_FALSE;
    if (raw_end > UINTPTR_MAX - mask) return JNI_FALSE;
    uintptr_t aligned_end = (raw_end + mask) & ~mask;
    std::size_t aligned_len = aligned_end - aligned_start;

    int new_prot = PROT_READ | PROT_WRITE;
    if (m->perms[2] == 'x') new_prot |= PROT_EXEC;
    int orig_prot = 0;
    if (m->perms[0] == 'r') orig_prot |= PROT_READ;
    if (m->perms[1] == 'w') orig_prot |= PROT_WRITE;
    if (m->perms[2] == 'x') orig_prot |= PROT_EXEC;

    if (mprotect(reinterpret_cast<void *>(aligned_start), aligned_len, new_prot) != 0) {
        NU_LOGW("mprotect(+W) failed at %p len=%zu", reinterpret_cast<void *>(aligned_start), aligned_len);
        return JNI_FALSE;
    }

    std::memcpy(reinterpret_cast<void *>(address), buffer.data(), static_cast<std::size_t>(length));

    if ((orig_prot & PROT_EXEC) != 0) {
        __builtin___clear_cache(reinterpret_cast<char *>(address),
                                 reinterpret_cast<char *>(address + length));
    }

    if (mprotect(reinterpret_cast<void *>(aligned_start), aligned_len, orig_prot) != 0) {
        NU_LOGW("mprotect(restore) failed at %p len=%zu",
                reinterpret_cast<void *>(aligned_start), aligned_len);
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

jlongArray native_find_string_xrefs(JNIEnv *env, jclass, jstring lib_j, jlong string_va_j, jint max_results_j) {
    std::string lib = jstring_to_string(env, lib_j);
    auto empty_result = [env]() { return env->NewLongArray(0); };
    if (lib.empty() || string_va_j <= 0) return empty_result();

#if !defined(__aarch64__)
    return empty_result();
#else

    ModuleInfo info = find_module_info(lib.c_str());
    if (!info.valid) return empty_result();

    uintptr_t target_va = static_cast<uintptr_t>(string_va_j);
    std::size_t max_results = max_results_j > 0
            ? std::min<std::size_t>(static_cast<std::size_t>(max_results_j), 4096)
            : 16;
    std::vector<uintptr_t> xrefs;
    for (const auto& range : info.ranges) {
        if (!range.readable || !range.executable || xrefs.size() >= max_results) continue;
        auto hits = find_adrp_add_xrefs(range.begin, range.end, target_va, max_results - xrefs.size());
        xrefs.insert(xrefs.end(), hits.begin(), hits.end());
    }

    jlongArray out = env->NewLongArray(static_cast<jsize>(xrefs.size()));
    if (out == nullptr) return nullptr;
    std::vector<jlong> vals(xrefs.size());
    for (std::size_t i = 0; i < xrefs.size(); ++i) {
        vals[i] = static_cast<jlong>(xrefs[i]);
    }
    env->SetLongArrayRegion(out, 0, static_cast<jsize>(xrefs.size()), vals.data());
    return out;
#endif
}

}  // namespace

std::vector<MemoryRange> process_ranges() {
    std::vector<MemoryRange> out;
    for (const auto& mapping : read_maps())
        out.push_back({mapping.start, mapping.end, mapping.perms[0] == 'r',
                       mapping.perms[1] == 'w', mapping.perms[2] == 'x'});
    return out;
}

ModuleInfo find_module_info(const char *library_name) {
    ModuleInfo info;
    if (library_name == nullptr || *library_name == '\0') return info;

    // Use dl_iterate_phdr — it gives the correct load bias for APK-embedded .so.
    // The Android linker handles the mapping and dlpi_addr is reliable.
    struct IterArg { const char *needle; ModuleInfo *out; bool found; };
    IterArg iarg{library_name, &info, false};

    dl_iterate_phdr([](struct dl_phdr_info *pinfo, size_t, void *data) -> int {
        auto *arg = static_cast<IterArg *>(data);
        const char *name = pinfo->dlpi_name;
        if (name == nullptr || *name == '\0') return 0;
        std::string path(name);
        if (!library_matches(path, arg->needle)) return 0;
        if (arg->found) {
            if (arg->out->base != pinfo->dlpi_addr) arg->out->ambiguous = true;
            return 0;
        }
        arg->found = true;
        arg->out->base = pinfo->dlpi_addr;
        arg->out->path = path;

        // Derive segment bounds from program headers instead of hardcoding section offsets.
        // Modern Android ELFs commonly put .rodata in a separate R-- LOAD rather than RX.
        uintptr_t base = pinfo->dlpi_addr;
        uintptr_t rx_lo = 0, rx_hi = 0, rw_lo = 0, rw_hi = 0;
        uintptr_t ro_lo = 0, ro_hi = 0;
        uintptr_t best_ro_size = 0;
        for (int i = 0; i < pinfo->dlpi_phnum; ++i) {
            const ElfW(Phdr) &ph = pinfo->dlpi_phdr[i];
            if (ph.p_type != PT_LOAD) continue;
            if (ph.p_vaddr > UINTPTR_MAX - base) continue;
            uintptr_t lo = base + ph.p_vaddr;
            if (ph.p_memsz > UINTPTR_MAX - lo) continue;
            uintptr_t hi = lo + ph.p_memsz;
            arg->out->ranges.push_back({lo, hi, bool(ph.p_flags & PF_R),
                    bool(ph.p_flags & PF_W), bool(ph.p_flags & PF_X)});
            arg->out->end = std::max(arg->out->end, hi);
            if (ph.p_flags & PF_X) {
                if (rx_lo == 0 || lo < rx_lo) rx_lo = lo;
                if (hi > rx_hi) rx_hi = hi;
            } else if (ph.p_flags & PF_W) {
                if (rw_lo == 0 || lo < rw_lo) rw_lo = lo;
                if (hi > rw_hi) rw_hi = hi;
            } else if (ph.p_flags & PF_R) {
                uintptr_t size = hi - lo;
                if (size > best_ro_size) {
                    ro_lo = lo;
                    ro_hi = hi;
                    best_ro_size = size;
                }
            }
        }
        arg->out->base = base;
        arg->out->path = path;
        arg->out->text_start = rx_lo;
        arg->out->text_end = rx_hi;
        arg->out->rodata_start = ro_lo != 0 ? ro_lo : rx_lo;
        arg->out->rodata_end = ro_hi != 0 ? ro_hi : rx_hi;
        arg->out->data_start = rw_lo;
        arg->out->data_end = rw_hi;
        arg->out->valid = !arg->out->ranges.empty();
        arg->found = true;
        return 0; // Continue to reject duplicate basenames in different loader namespaces.
    }, &iarg);

    if (info.ambiguous) {
        info.valid = false;
        info.ranges.clear();
        return info;
    }

    if (iarg.found) {
        NU_LOGI("module %s: base=%p text=[%p,%p] rodata=[%p,%p]",
                library_name,
                (void *)info.base,
                (void *)info.text_start, (void *)info.text_end,
                (void *)info.rodata_start, (void *)info.rodata_end);
    }

    return info;
}

uintptr_t resolve_export(uintptr_t module_base, const char *symbol_name) {
    if (!module_base || !symbol_name || !*symbol_name) return 0;
    struct Lookup { uintptr_t base; std::string path; } lookup{module_base, {}};
    dl_iterate_phdr([](dl_phdr_info* info, size_t, void* data) {
        auto* value = static_cast<Lookup*>(data);
        if (info->dlpi_addr != value->base || !info->dlpi_name || !*info->dlpi_name) return 0;
        value->path = info->dlpi_name;
        return 1;
    }, &lookup);
    if (lookup.path.empty()) return 0;
    // ShadowHook uses xDL's loaded-image lookup (not XDL_TRY_FORCE_LOAD). Its close
    // releases the symbol-query wrapper; it does not dlclose the target library.
    void* image = shadowhook_dlopen(lookup.path.c_str());
    if (!image) return 0;
    void* symbol = shadowhook_dlsym_dynsym(image, symbol_name);
    shadowhook_dlclose(image);
    return reinterpret_cast<uintptr_t>(symbol);
}

std::vector<uintptr_t> find_adrp_add_xrefs(uintptr_t text_start, uintptr_t text_end,
                                            uintptr_t target_va, std::size_t max_results) {
    std::vector<uintptr_t> results;
    if (text_start == 0 || text_end <= text_start || text_start % alignof(uint32_t) != 0
            || text_end - text_start < sizeof(uint32_t)) return results;

    uintptr_t target_page = target_va & ~0xFFFULL;
    uint32_t target_offset = target_va & 0xFFF;

    uintptr_t scan = text_start;
    uintptr_t limit = text_end - 4;

    while (scan <= limit && results.size() < max_results) {
        uint32_t insn = *reinterpret_cast<const uint32_t *>(scan);
        if (!is_adrp(insn)) {
            scan += 4;
            continue;
        }
        AdrpDecode adrp = decode_adrp(insn, scan);
        if (!adrp.valid || adrp.target_page != target_page) {
            scan += 4;
            continue;
        }

        // Optimized AArch64 often separates ADRP from the consuming ADD/LDR by register moves,
        // guards, or argument setup. Accept a short forward window instead of only ADRP+next.
        constexpr int kForwardInsnWindow = 8;
        for (int i = 1; i <= kForwardInsnWindow && static_cast<uintptr_t>(4 * i) <= limit - scan; ++i) {
            uint32_t candidate = *reinterpret_cast<const uint32_t *>(scan + 4 * i);
            if (completes_page_ref(candidate, adrp, target_offset)) {
                results.push_back(scan);
                break;
            }
        }
        // A second ADRP can sit between this ADRP and its consuming ADD/LDR.
        scan += 4;
    }
    return results;
}

uintptr_t find_function_start(uintptr_t addr_in_func, std::size_t scan_back_limit) {
    if (addr_in_func % alignof(uint32_t) != 0) return 0;
    uintptr_t scan = addr_in_func;
    uintptr_t limit = addr_in_func > scan_back_limit ? addr_in_func - scan_back_limit : 0;
    while (scan - limit >= sizeof(uint32_t)) {
        scan -= 4;
        uint32_t insn = *reinterpret_cast<const uint32_t *>(scan);
        if (is_function_prologue(insn)) return scan;
    }
    return 0;
}

uintptr_t find_string_va(uintptr_t rodata_start, uintptr_t rodata_end, const char *needle) {
    if (rodata_start == 0 || rodata_end <= rodata_start || needle == nullptr) return 0;
    std::size_t needle_len = std::strlen(needle);
    if (needle_len == 0) return 0;
    const char *base = reinterpret_cast<const char *>(rodata_start);
    std::size_t size = rodata_end - rodata_start;
    if (needle_len > size) return 0;
    for (std::size_t i = 0; i <= size - needle_len; ++i) {
        if (std::memcmp(base + i, needle, needle_len) == 0) {
            return rodata_start + i;
        }
    }
    return 0;
}

uintptr_t find_ptr_in_range(uintptr_t start, uintptr_t end, uintptr_t value) {
    if (start == 0 || end <= start) return 0;
    constexpr uintptr_t alignment = alignof(uintptr_t);
    if (start > UINTPTR_MAX - (alignment - 1)) return 0;
    start = (start + alignment - 1) & ~(alignment - 1);
    if (end < start || end - start < sizeof(uintptr_t)) return 0;
    for (uintptr_t p = start; p <= end - sizeof(uintptr_t); p += sizeof(uintptr_t)) {
        if (*reinterpret_cast<const uintptr_t *>(p) == value) return p;
    }
    return 0;
}

uintptr_t pattern_scan(uintptr_t start, uintptr_t end, const char *aob) {
    if (start == 0 || end <= start || aob == nullptr) return 0;
    Pattern pattern;
    if (!parse_pattern(aob, pattern)) return 0;
    return scan_pattern(start, end, pattern);
}

bool register_natives(JNIEnv *env) {
    jclass cls = env->FindClass("com/template/lsposed/NativeUtils");
    if (cls == nullptr) return false;
    static JNINativeMethod methods[] = {
            {"nativeFindModule", "(Ljava/lang/String;)[J",
             reinterpret_cast<void *>(JniBoundary<native_find_module>::call)},
            {"nativePatternScan", "(JJLjava/lang/String;)J",
             reinterpret_cast<void *>(JniBoundary<native_pattern_scan>::call)},
            {"nativeResolveSymbol", "(Ljava/lang/String;Ljava/lang/String;)J",
             reinterpret_cast<void *>(JniBoundary<native_resolve_symbol>::call)},
            {"nativeReadMemory", "(JI)[B",
             reinterpret_cast<void *>(JniBoundary<native_read_memory>::call)},
            {"nativeWriteMemory", "(J[B)Z",
             reinterpret_cast<void *>(JniBoundary<native_write_memory>::call)},
            {"nativeFindStringXRefs", "(Ljava/lang/String;JI)[J",
             reinterpret_cast<void *>(JniBoundary<native_find_string_xrefs>::call)},
    };
    return env->RegisterNatives(cls, methods, sizeof(methods) / sizeof(methods[0])) == JNI_OK;
}

}  // namespace native_utils
