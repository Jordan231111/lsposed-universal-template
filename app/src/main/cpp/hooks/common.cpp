#include "common.h"

#include <shadowhook.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace firestone {
namespace {

struct Mapping {
    uintptr_t start;
    uintptr_t end;
    uintptr_t offset;
    char perms[5];
    char path[512];
};

bool read_mapping(FILE *f, Mapping *m) {
    char line[1024];
    if (std::fgets(line, sizeof(line), f) == nullptr) return false;
    unsigned long start = 0;
    unsigned long end = 0;
    unsigned long offset = 0;
    char perms[5] = {};
    char path[512] = {};
    int n = std::sscanf(line, "%lx-%lx %4s %lx %*s %*lu %511[^\n]",
                        &start, &end, perms, &offset, path);
    if (n < 4) {
        m->start = 0;
        return true;
    }
    m->start = static_cast<uintptr_t>(start);
    m->end = static_cast<uintptr_t>(end);
    m->offset = static_cast<uintptr_t>(offset);
    std::strncpy(m->perms, perms, sizeof(m->perms) - 1);
    const char *trim = path;
    while (*trim == ' ' || *trim == '\t') ++trim;
    std::strncpy(m->path, trim, sizeof(m->path) - 1);
    return true;
}

bool path_matches(const char *path, const char *soname) {
    if (path == nullptr || soname == nullptr) return false;
    const char *base = std::strrchr(path, '/');
    base = base == nullptr ? path : base + 1;
    return std::strcmp(base, soname) == 0;
}

}  // namespace

uintptr_t find_module_base(const char *soname) {
    FILE *f = std::fopen("/proc/self/maps", "r");
    if (f == nullptr) return 0;
    uintptr_t base = 0;
    Mapping m{};
    while (read_mapping(f, &m)) {
        if (m.start == 0) continue;
        if (!path_matches(m.path, soname)) continue;
        if (m.offset == 0) {
            base = m.start;
            break;
        }
        uintptr_t candidate = m.start - m.offset;
        if (base == 0 || candidate < base) base = candidate;
    }
    std::fclose(f);
    return base;
}

void *rva_addr(uintptr_t base, uintptr_t rva) {
    if (base == 0 || rva == 0) return nullptr;
    return reinterpret_cast<void *>(base + rva);
}

bool install_rva_hook(uintptr_t base,
                      uintptr_t rva,
                      void *replacement,
                      void **original,
                      void **stub,
                      const char *name) {
    void *target = rva_addr(base, rva);
    if (target == nullptr) {
        ALOGE("%s hook target missing rva=0x%lx", name, static_cast<unsigned long>(rva));
        return false;
    }
    *stub = shadowhook_hook_func_addr_2(
            target,
            replacement,
            original,
            SHADOWHOOK_HOOK_WITH_SHARED_MODE | SHADOWHOOK_HOOK_RECORD,
            "libil2cpp.so",
            name);
    int err = shadowhook_get_errno();
    if (*stub == nullptr) {
        ALOGE("%s hook failed rva=0x%lx err=%d %s", name, static_cast<unsigned long>(rva),
              err, shadowhook_to_errmsg(err));
        return false;
    }
    ALOGI("%s hook installed rva=0x%lx target=%p stub=%p err=%d %s", name,
          static_cast<unsigned long>(rva), target, *stub, err, shadowhook_to_errmsg(err));
    return true;
}

bool install_rva_intercept(uintptr_t base,
                           uintptr_t rva,
                           void *interceptor,
                           void **stub,
                           const char *name) {
    void *target = rva_addr(base, rva);
    if (target == nullptr) {
        ALOGE("%s intercept target missing rva=0x%lx", name, static_cast<unsigned long>(rva));
        return false;
    }
    *stub = shadowhook_intercept_func_addr(
            target,
            reinterpret_cast<shadowhook_interceptor_t>(interceptor),
            nullptr,
            SHADOWHOOK_INTERCEPT_RECORD,
            "libil2cpp.so",
            name);
    int err = shadowhook_get_errno();
    if (*stub == nullptr) {
        ALOGE("%s intercept failed rva=0x%lx err=%d %s", name, static_cast<unsigned long>(rva),
              err, shadowhook_to_errmsg(err));
        return false;
    }
    ALOGI("%s intercept installed rva=0x%lx target=%p stub=%p err=%d %s", name,
          static_cast<unsigned long>(rva), target, *stub, err, shadowhook_to_errmsg(err));
    return true;
}

float read_obscured_float(const ObscuredFloat *value) {
    if (value == nullptr) return 0.0f;
    uint32_t raw = static_cast<uint32_t>(value->hidden_value ^ value->current_crypto_key);
    float out = 0.0f;
    std::memcpy(&out, &raw, sizeof(out));
    return out;
}

ObscuredFloat make_obscured_float(float value, uintptr_t il2cpp_base) {
    using MakeFn = ObscuredFloat (*)(float, const void *);
    auto fn = reinterpret_cast<MakeFn>(rva_addr(il2cpp_base, 0x211761C));
    if (fn != nullptr) return fn(value, nullptr);
    ObscuredFloat out{};
    out.current_crypto_key = 0x3F1A4C9B;
    uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    out.hidden_value = static_cast<int32_t>(raw ^ static_cast<uint32_t>(out.current_crypto_key));
    out.inited = true;
    out.fake_value = value;
    out.fake_value_active = false;
    return out;
}

void write_obscured_float(void *address, float value, uintptr_t il2cpp_base) {
    if (address == nullptr) return;
    ObscuredFloat out = make_obscured_float(value, il2cpp_base);
    std::memcpy(address, &out, sizeof(out));
}

float clamp_multiplier(float value, float fallback, float min_value, float max_value) {
    if (!std::isfinite(value)) value = fallback;
    return std::max(min_value, std::min(max_value, value));
}

}  // namespace firestone
