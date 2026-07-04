#include "native_utils.h"

#include <android/log.h>
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
#include <dlfcn.h>

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
    std::size_t pos = path.rfind('/');
    std::string base = pos == std::string::npos ? path : path.substr(pos + 1);
    if (base == name) return true;
    if (base.size() > name.size() && base.compare(0, name.size(), name) == 0) {
        char next = base[name.size()];
        return next == '.';
    }
    return false;
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

jlongArray native_find_module(JNIEnv *env, jclass, jstring name_j) {
    std::string name = jstring_to_string(env, name_j);
    jlongArray out = env->NewLongArray(2);
    if (out == nullptr) return nullptr;
    jlong values[2] = {0, 0};
    if (name.empty()) {
        env->SetLongArrayRegion(out, 0, 2, values);
        return out;
    }
    auto maps = read_maps();
    uintptr_t start = 0;
    uintptr_t end = 0;
    for (const auto &m : maps) {
        if (!library_matches(m.path, name)) continue;
        if (m.perms[0] != 'r') continue;
        if (start == 0 || m.start < start) start = m.start;
        if (m.end > end) end = m.end;
    }
    values[0] = static_cast<jlong>(start);
    values[1] = static_cast<jlong>(end > start ? end - start : 0);
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
        std::size_t capped = static_cast<std::size_t>(scan_limit - scan_start);
        if (capped < p.bytes.size()) continue;

        const uint8_t *data = reinterpret_cast<const uint8_t *>(scan_start);
        std::size_t scan_end = capped - p.bytes.size();
        for (std::size_t i = 0; i <= scan_end; ++i) {
            bool ok = true;
            for (std::size_t j = 0; j < p.bytes.size(); ++j) {
                if (p.mask[j] && data[i + j] != p.bytes[j]) {
                    ok = false;
                    break;
                }
            }
            if (ok) return static_cast<jlong>(scan_start + i);
        }
    }
    return 0;
}

jlong native_resolve_symbol(JNIEnv *env, jclass, jstring lib_j, jstring sym_j) {
    std::string lib = jstring_to_string(env, lib_j);
    std::string sym = jstring_to_string(env, sym_j);
    if (lib.empty() || sym.empty()) return 0;
    void *handle = dlopen(lib.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (handle == nullptr) return 0;
    void *addr = dlsym(handle, sym.c_str());
    dlclose(handle);
    return static_cast<jlong>(reinterpret_cast<uintptr_t>(addr));
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
    if (length <= 0) return JNI_FALSE;

    auto maps = read_maps();
    uintptr_t address = static_cast<uintptr_t>(address_j);
    const Mapping *m = containing_mapping(maps, address, static_cast<std::size_t>(length));
    if (m == nullptr) return JNI_FALSE;

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;
    uintptr_t mask = static_cast<uintptr_t>(page_size) - 1;
    uintptr_t aligned_start = address & ~mask;
    uintptr_t raw_end = 0;
    if (!checked_range_end(address, static_cast<std::size_t>(length), &raw_end)) return JNI_FALSE;
    if (raw_end > UINTPTR_MAX - mask) return JNI_FALSE;
    uintptr_t aligned_end = (raw_end + mask) & ~mask;
    std::size_t aligned_len = aligned_end - aligned_start;

    int new_prot = PROT_READ | PROT_WRITE;
    if (m->perms[2] == 'x') new_prot |= PROT_EXEC;

    if (mprotect(reinterpret_cast<void *>(aligned_start), aligned_len, new_prot) != 0) {
        NU_LOGW("mprotect(+W) failed at %p len=%zu", reinterpret_cast<void *>(aligned_start), aligned_len);
        return JNI_FALSE;
    }

    std::vector<jbyte> buffer(static_cast<std::size_t>(length));
    env->GetByteArrayRegion(data_j, 0, length, buffer.data());
    std::memcpy(reinterpret_cast<void *>(address), buffer.data(), static_cast<std::size_t>(length));

    int orig_prot = 0;
    if (m->perms[0] == 'r') orig_prot |= PROT_READ;
    if (m->perms[1] == 'w') orig_prot |= PROT_WRITE;
    if (m->perms[2] == 'x') orig_prot |= PROT_EXEC;
    mprotect(reinterpret_cast<void *>(aligned_start), aligned_len, orig_prot);

    if ((orig_prot & PROT_EXEC) != 0) {
        __builtin___clear_cache(reinterpret_cast<char *>(address),
                                 reinterpret_cast<char *>(address + length));
    }
    return JNI_TRUE;
}

jlongArray native_find_string_xrefs(JNIEnv *env, jclass, jstring lib_j, jlong string_va_j, jint max_results_j) {
    std::string lib = jstring_to_string(env, lib_j);
    jlongArray out = env->NewLongArray(0);
    if (out == nullptr) return nullptr;

    ModuleInfo info = find_module_info(lib.c_str());
    if (!info.valid) return out;

    uintptr_t target_va = static_cast<uintptr_t>(string_va_j);
    if (target_va == 0) {
        target_va = info.base + static_cast<uintptr_t>(string_va_j);
    }

    std::size_t max_results = max_results_j > 0 ? static_cast<std::size_t>(max_results_j) : 16;
    auto xrefs = find_adrp_add_xrefs(info.text_start, info.text_end, target_va, max_results);

    out = env->NewLongArray(static_cast<jsize>(xrefs.size()));
    if (out == nullptr) return nullptr;
    std::vector<jlong> vals(xrefs.size());
    for (std::size_t i = 0; i < xrefs.size(); ++i) {
        vals[i] = static_cast<jlong>(xrefs[i]);
    }
    env->SetLongArrayRegion(out, 0, static_cast<jsize>(xrefs.size()), vals.data());
    return out;
}

}  // namespace

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
        if (path.find(arg->needle) == std::string::npos) return 0;

        // Derive segment bounds from the program headers instead of hardcoding section offsets,
        // so xref/string/vtable resolution survives version & offset shifts. The RX LOAD holds
        // both code and .rodata strings; the RW LOAD(s) hold .data.rel.ro (vtables/typeinfo).
        uintptr_t base = pinfo->dlpi_addr;
        uintptr_t rx_lo = 0, rx_hi = 0, rw_lo = 0, rw_hi = 0;
        for (int i = 0; i < pinfo->dlpi_phnum; ++i) {
            const ElfW(Phdr) &ph = pinfo->dlpi_phdr[i];
            if (ph.p_type != PT_LOAD) continue;
            uintptr_t lo = base + ph.p_vaddr;
            uintptr_t hi = lo + ph.p_memsz;
            if (ph.p_flags & PF_X) {
                rx_lo = lo;
                rx_hi = hi;
            } else if (ph.p_flags & PF_W) {
                if (rw_lo == 0 || lo < rw_lo) rw_lo = lo;
                if (hi > rw_hi) rw_hi = hi;
            }
        }
        if (rx_hi == 0) return 0;  // no executable segment -> not the lib we want

        arg->out->base = base;
        arg->out->text_start = rx_lo;
        arg->out->text_end = rx_hi;
        arg->out->rodata_start = rx_lo;  // .rodata strings live inside the RX LOAD segment
        arg->out->rodata_end = rx_hi;
        arg->out->data_start = rw_lo;
        arg->out->data_end = rw_hi;
        arg->out->end = rw_hi > rx_hi ? rw_hi : rx_hi;
        arg->out->valid = true;
        arg->found = true;
        return 1;
    }, &iarg);

    if (iarg.found) {
        NU_LOGI("module %s: base=%p text=[%p,%p] rodata=[%p,%p]",
                library_name,
                (void *)info.base,
                (void *)info.text_start, (void *)info.text_end,
                (void *)info.rodata_start, (void *)info.rodata_end);
    }

    return info;
}

std::vector<uintptr_t> find_adrp_add_xrefs(uintptr_t text_start, uintptr_t text_end,
                                            uintptr_t target_va, std::size_t max_results) {
    std::vector<uintptr_t> results;
    if (text_start == 0 || text_end <= text_start) return results;

    uintptr_t target_page = target_va & ~0xFFFULL;
    uint32_t target_offset = target_va & 0xFFF;

    uintptr_t scan = text_start;
    uintptr_t limit = text_end - 4;

    while (scan < limit && results.size() < max_results) {
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
        bool matched = false;
        constexpr int kForwardInsnWindow = 8;
        for (int i = 1; i <= kForwardInsnWindow && scan + 4 * i < text_end; ++i) {
            uint32_t candidate = *reinterpret_cast<const uint32_t *>(scan + 4 * i);
            if (completes_page_ref(candidate, adrp, target_offset)) {
                results.push_back(scan);
                matched = true;
                break;
            }
        }
        if (matched) {
            scan += 8;
            continue;
        }

        scan += 4;
    }
    return results;
}

uintptr_t find_function_start(uintptr_t addr_in_func, std::size_t scan_back_limit) {
    uintptr_t scan = addr_in_func;
    uintptr_t limit = addr_in_func > scan_back_limit ? addr_in_func - scan_back_limit : 0;
    while (scan > limit) {
        scan -= 4;
        uint32_t insn = *reinterpret_cast<const uint32_t *>(scan);
        if (is_function_prologue(insn)) return scan;
    }
    return addr_in_func;
}

uintptr_t find_string_va(uintptr_t rodata_start, uintptr_t rodata_end, const char *needle) {
    if (rodata_start == 0 || rodata_end <= rodata_start || needle == nullptr) return 0;
    std::size_t needle_len = std::strlen(needle);
    if (needle_len == 0) return 0;
    const char *base = reinterpret_cast<const char *>(rodata_start);
    std::size_t size = rodata_end - rodata_start;
    for (std::size_t i = 0; i + needle_len < size; ++i) {
        if (std::memcmp(base + i, needle, needle_len) == 0) {
            return rodata_start + i;
        }
    }
    return 0;
}

uintptr_t find_ptr_in_range(uintptr_t start, uintptr_t end, uintptr_t value) {
    if (start == 0 || end <= start) return 0;
    start = (start + 7) & ~uintptr_t(7);  // 8-byte align
    for (uintptr_t p = start; p + sizeof(uintptr_t) <= end; p += sizeof(uintptr_t)) {
        if (*reinterpret_cast<const uintptr_t *>(p) == value) return p;
    }
    return 0;
}

bool register_natives(JNIEnv *env) {
    jclass cls = env->FindClass("com/template/lsposed/NativeUtils");
    if (cls == nullptr) return false;
    static JNINativeMethod methods[] = {
            {"nativeFindModule", "(Ljava/lang/String;)[J",
             reinterpret_cast<void *>(native_find_module)},
            {"nativePatternScan", "(JJLjava/lang/String;)J",
             reinterpret_cast<void *>(native_pattern_scan)},
            {"nativeResolveSymbol", "(Ljava/lang/String;Ljava/lang/String;)J",
             reinterpret_cast<void *>(native_resolve_symbol)},
            {"nativeReadMemory", "(JI)[B",
             reinterpret_cast<void *>(native_read_memory)},
            {"nativeWriteMemory", "(J[B)Z",
             reinterpret_cast<void *>(native_write_memory)},
            {"nativeFindStringXRefs", "(Ljava/lang/String;JI)[J",
             reinterpret_cast<void *>(native_find_string_xrefs)},
    };
    return env->RegisterNatives(cls, methods, sizeof(methods) / sizeof(methods[0])) == JNI_OK;
}

}  // namespace native_utils
