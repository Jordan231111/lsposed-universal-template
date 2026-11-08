package com.template.lsposed;

/**
 * Java-side facade for the native helper toolkit implemented in {@code native_utils.cpp}.
 *
 * <p>Java/JNI lookup failures return method-specific fallbacks (0, null, false, or empty arrays).
 * Java catch blocks do not catch native signals. Mapping checks are snapshots: callers must keep
 * the library/address range alive while native code uses it. Loading a library, reading maps and
 * scanning memory can block, so cache resolved addresses and avoid these operations in hot hooks.
 * The native library is loaded through {@link NativeBridge#loadTemplateNative()}.</p>
 */
public final class NativeUtils {
    private NativeUtils() {}

    /**
     * @return {@code [loadBias, imageSpan]} from ELF program headers, or {@code [0, 0]} when absent.
     * The span may contain unmapped gaps; patternScan checks readable mappings individually.
     */
    public static long[] findModule(String libraryName) {
        if (!NativeBridge.loadTemplateNative()) return new long[]{0L, 0L};
        try {
            long[] out = nativeFindModule(libraryName == null ? "" : libraryName);
            return out != null ? out : new long[]{0L, 0L};
        } catch (Throwable t) {
            return new long[]{0L, 0L};
        }
    }

    /**
     * IDA-style pattern scan. The pattern accepts hex bytes and {@code "??"} wildcards, e.g.
     * {@code "48 8B ?? ?? 00 00 00"}. Returns the absolute address of the first match or 0.
     */
    public static long patternScan(long base, long size, String pattern) {
        if (base == 0 || size <= 0 || pattern == null || pattern.isEmpty()) return 0L;
        if (!NativeBridge.loadTemplateNative()) return 0L;
        try {
            return nativePatternScan(base, size, pattern);
        } catch (Throwable t) {
            return 0L;
        }
    }

    /** Queries the loaded image's dynamic exports through ShadowHook, without loading an ELF. */
    public static long resolveSymbol(String libraryName, String symbolName) {
        if (libraryName == null || symbolName == null || libraryName.isEmpty() || symbolName.isEmpty()) return 0L;
        if (!NativeBridge.loadTemplateNative()) return 0L;
        try {
            return nativeResolveSymbol(libraryName, symbolName);
        } catch (Throwable t) {
            return 0L;
        }
    }

    /**
     * Reads {@code length} bytes from {@code address} after checking the address range against
     * readable {@code /proc/self/maps} entries.
     */
    public static byte[] readMemory(long address, int length) {
        if (address == 0 || length <= 0 || length > (8 * 1024 * 1024)) return null;
        if (!NativeBridge.loadTemplateNative()) return null;
        try {
            return nativeReadMemory(address, length);
        } catch (Throwable t) {
            return null;
        }
    }

    /**
     * Writes within one mapped range, temporarily adding read/write permissions while preserving
     * the original execute bit. Restores the original permissions and flushes the instruction
     * cache for executable memory. Returns false on failure; a failed permission restore can occur
     * after bytes were written, so false does not imply that memory is unchanged.
     * This is not an atomic patcher: callers must own the range and quiesce concurrent access.
     */
    public static boolean writeMemory(long address, byte[] data) {
        if (address == 0 || data == null || data.length == 0 || data.length > (8 * 1024 * 1024)) {
            return false;
        }
        if (!NativeBridge.loadTemplateNative()) return false;
        try {
            return nativeWriteMemory(address, data);
        } catch (Throwable t) {
            return false;
        }
    }

    /**
     * Finds ARM64 ADRP+ADD/LDR references to an absolute in-memory string address.
     * Returns an empty array on non-ARM64 processes or when the module/address is unavailable.
     */
    public static long[] findStringXRefs(String libraryName, long stringAddress, int maxResults) {
        if (libraryName == null || libraryName.isEmpty() || stringAddress <= 0) return new long[0];
        if (!NativeBridge.loadTemplateNative()) return new long[0];
        try {
            long[] out = nativeFindStringXRefs(libraryName, stringAddress, maxResults);
            return out != null ? out : new long[0];
        } catch (Throwable t) {
            return new long[0];
        }
    }

    // Registered through RegisterNatives in template_native.cpp; do not rename.
    private static native long[] nativeFindModule(String name);
    private static native long nativePatternScan(long base, long size, String pattern);
    private static native long nativeResolveSymbol(String lib, String sym);
    private static native byte[] nativeReadMemory(long address, int length);
    private static native boolean nativeWriteMemory(long address, byte[] data);
    private static native long[] nativeFindStringXRefs(String lib, long stringVa, int maxResults);
}
