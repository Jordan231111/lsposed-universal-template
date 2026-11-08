#pragma once

#include <jni.h>
#include <cstdint>
#include <string>
#include <vector>

namespace native_utils {

/**
 * Registers the NativeUtils JNI surface on com.template.lsposed.NativeUtils.
 * Called from template_native.cpp's JNI_OnLoad. Returns true on success.
 */
bool register_natives(JNIEnv *env);

struct MemoryRange {
    uintptr_t begin{0}, end{0};
    bool readable{false}, writable{false}, executable{false};
    bool contains(uintptr_t address, std::size_t size) const {
        return address >= begin && address < end && size <= end - address;
    }
};

// A snapshot, not a lifetime lock. Keep the owning mapping alive while using its addresses.
std::vector<MemoryRange> process_ranges();

struct ModuleInfo {
    // Summary extents may span unmapped gaps. Iterate ranges for every memory read/scan.
    uintptr_t base{0};
    uintptr_t end{0};
    uintptr_t text_start{0};
    uintptr_t text_end{0};
    uintptr_t rodata_start{0};
    uintptr_t rodata_end{0};
    uintptr_t data_start{0};   // writable/relro LOAD segments — vtables & typeinfo live here
    uintptr_t data_end{0};
    bool valid{false};
    bool ambiguous{false};
    std::string path;
    std::vector<MemoryRange> ranges;
};

ModuleInfo find_module_info(const char *library_name);

/**
 * Resolve an exported symbol from an already-loaded ELF through ShadowHook's namespace-aware
 * symbol API. No target library is loaded or unloaded. The caller retains the mapping lifetime.
 * `module_base` is ModuleInfo::base, not the first APK mapping or a file offset.
 */
uintptr_t resolve_export(uintptr_t module_base, const char *symbol_name);

/**
 * Scan [start,end) for a naturally aligned pointer-sized word equal to `value`.
 * Returns the address of the first match, or 0.
 *
 * The caller supplies one readable live range. A matching word is analysis evidence, not proof
 * of a vtable, object type or uniquely identified symbol.
 */
uintptr_t find_ptr_in_range(uintptr_t start, uintptr_t end, uintptr_t value);

/**
 * ARM64 ADRP+ADD and ADRP+LDR cross-reference scanner.
 * Scans .text for instruction pairs that compute the address of target_va.
 * Handles both ADRP+ADD (direct address) and ADRP+LDR (load from address).
 * Returns a vector of ADRP instruction addresses.
 */
std::vector<uintptr_t> find_adrp_add_xrefs(uintptr_t text_start, uintptr_t text_end,
                                            uintptr_t target_va, std::size_t max_results);

/**
 * Diagnostic heuristic only: walk backwards to find a possible prologue.
 * Looks for STP X29, X30 / SUB SP, SP / PACIASP signatures.
 * Returns a candidate address or 0. Never use this alone as proof of a hook target.
 */
uintptr_t find_function_start(uintptr_t addr_in_func, std::size_t scan_back_limit);

/**
 * Searches .rodata of a loaded module for a string and returns its runtime VA.
 * Returns 0 if not found.
 */
uintptr_t find_string_va(uintptr_t rodata_start, uintptr_t rodata_end, const char *needle);

/**
 * AOB (array-of-bytes) pattern scan over [start,end). Pattern is space-separated hex with `??`
 * wildcards, e.g. "1F 20 03 D5 ?? ?? 40 F9". Returns the first match address, or 0. Supply one readable
 * live range; a first match alone does not establish uniqueness or a safe hook target.
 */
uintptr_t pattern_scan(uintptr_t start, uintptr_t end, const char *aob);

}  // namespace native_utils
