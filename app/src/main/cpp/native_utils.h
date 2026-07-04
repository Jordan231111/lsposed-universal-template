#pragma once

#include <jni.h>
#include <cstdint>
#include <vector>

namespace native_utils {

/**
 * Registers the NativeUtils JNI surface on com.template.lsposed.NativeUtils.
 * Called from candytest_native.cpp's JNI_OnLoad. Returns true on success.
 */
bool register_natives(JNIEnv *env);

struct ModuleInfo {
    uintptr_t base{0};
    uintptr_t end{0};
    uintptr_t text_start{0};
    uintptr_t text_end{0};
    uintptr_t rodata_start{0};
    uintptr_t rodata_end{0};
    uintptr_t data_start{0};   // writable/relro LOAD segments — vtables & typeinfo live here
    uintptr_t data_end{0};
    bool valid{false};
};

ModuleInfo find_module_info(const char *library_name);

/**
 * Scan [start,end) for an 8-byte-aligned pointer-sized word equal to `value`.
 * Returns the address of the first match, or 0.
 *
 * RELATIVE relocations are already applied in the mapped image, so a vtable slot that points to
 * function F literally holds F's runtime address, and a typeinfo's name pointer holds the string's
 * runtime address. That lets us walk vtables/RTTI purely by value — no ELF reloc parsing needed,
 * which is what makes symbol resolution survive version/offset shifts.
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
 * Walks backwards from addr_in_func to find a function prologue.
 * Looks for STP X29, X30 / SUB SP, SP / PACIASP signatures.
 * Returns the function start address, or addr_in_func if not found within limit.
 */
uintptr_t find_function_start(uintptr_t addr_in_func, std::size_t scan_back_limit);

/**
 * Searches .rodata of a loaded module for a string and returns its runtime VA.
 * Returns 0 if not found.
 */
uintptr_t find_string_va(uintptr_t rodata_start, uintptr_t rodata_end, const char *needle);

}  // namespace native_utils
