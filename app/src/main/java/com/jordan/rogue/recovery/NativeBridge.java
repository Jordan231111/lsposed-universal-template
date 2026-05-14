package com.jordan.rogue.recovery;

import android.content.Context;
import android.os.Build;
import android.util.Log;

import com.bytedance.shadowhook.ShadowHook;

/**
 * Safe Java wrapper around the optional native ShadowHook scaffold.
 *
 * <p>All public methods catch failures and return status text/codes instead of crashing the target app.
 * Keep that pattern in your own module: hooks should fail closed and leave the original app behavior intact.</p>
 */
public final class NativeBridge {
    private static boolean libraryLoadTried;
    private static boolean libraryLoaded;
    private static boolean shadowHookInitTried;
    private static int shadowHookErrno = Integer.MIN_VALUE;

    private NativeBridge() {}

    public static synchronized boolean isArmProcess() {
        for (String abi : Build.SUPPORTED_ABIS) {
            if ("arm64-v8a".equals(abi) || "armeabi-v7a".equals(abi)) return true;
        }
        return false;
    }

    public static synchronized int initShadowHook() {
        if (shadowHookInitTried) return shadowHookErrno;
        shadowHookInitTried = true;
        try {
            shadowHookErrno = ShadowHook.init(new ShadowHook.ConfigBuilder()
                    .setMode(ShadowHook.Mode.SHARED)
                    .setDebuggable(TemplateConfig.SHADOWHOOK_DEBUG_LOGS)
                    .setRecordable(TemplateConfig.SHADOWHOOK_RECORDS)
                    .setDisable(false)
                    .build());
            if (shadowHookErrno == 0) {
                if (TemplateConfig.VERBOSE_LOGS) {
                    Log.i(TemplateConfig.LOG_TAG, "ShadowHook ready: version=" + ShadowHook.getVersion()
                            + ", arch=" + ShadowHook.getArch());
                }
            } else if (TemplateConfig.VERBOSE_LOGS) {
                Log.w(TemplateConfig.LOG_TAG, "ShadowHook init failed: " + shadowHookErrno
                        + " / " + ShadowHook.toErrmsg(shadowHookErrno));
            }
        } catch (Throwable t) {
            shadowHookErrno = -1;
            if (TemplateConfig.VERBOSE_LOGS) Log.e(TemplateConfig.LOG_TAG, "ShadowHook init exception", t);
        }
        return shadowHookErrno;
    }

    public static synchronized boolean loadTemplateNative() {
        if (libraryLoadTried) return libraryLoaded;
        libraryLoadTried = true;
        try {
            System.loadLibrary(TemplateConfig.NATIVE_LIBRARY_NAME);
            libraryLoaded = true;
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.i(TemplateConfig.LOG_TAG, "Loaded lib" + TemplateConfig.NATIVE_LIBRARY_NAME + ".so");
            }
        } catch (Throwable t) {
            libraryLoaded = false;
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.e(TemplateConfig.LOG_TAG, "Could not load lib" + TemplateConfig.NATIVE_LIBRARY_NAME + ".so", t);
            }
        }
        return libraryLoaded;
    }

    public static synchronized int installNativeHooks(Context context, String packageName) {
        FeatureState.bumpNativeInstallAttempts();
        if (!TemplateConfig.ENABLE_NATIVE_SHADOWHOOK) return -10;
        if (!isArmProcess()) {
            FeatureState.setLastMessage("Native hooks skipped: ShadowHook supports arm/arm64 only");
            return -11;
        }
        int init = initShadowHook();
        if (init != 0) {
            FeatureState.setLastMessage("ShadowHook init failed: " + init);
            return init;
        }
        if (!loadTemplateNative()) {
            FeatureState.setLastMessage("Native library load failed");
            return -12;
        }
        try {
            String dataDir = context != null && context.getApplicationInfo() != null
                    ? context.getApplicationInfo().dataDir : "";
            int result = nativeInstallHooks(packageName != null ? packageName : "", dataDir);
            syncFeatureState();
            FeatureState.setLastMessage(result == 0 ? "Native scaffold installed" : "Native install returned " + result);
            return result;
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) Log.e(TemplateConfig.LOG_TAG, "nativeInstallHooks failed", t);
            FeatureState.setLastMessage("nativeInstallHooks exception: " + t.getClass().getSimpleName());
            return -13;
        }
    }

    public static synchronized void syncFeatureState() {
        try {
            if (!libraryLoaded && !loadTemplateNative()) return;
            nativeSyncFeatureState(
                    FeatureState.getDamageMultiplier(),
                    FeatureState.getDefenseMultiplier(),
                    FeatureState.isGodMode(),
                    FeatureState.isFreeShop());
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) Log.w(TemplateConfig.LOG_TAG, "nativeSyncFeatureState failed", t);
        }
    }

    public static synchronized String getNativeRecords() {
        try {
            if (!libraryLoaded && !loadTemplateNative()) return "Native library not loaded";
            String records = nativeGetShadowHookRecords();
            return records == null || records.isEmpty() ? "No ShadowHook records yet" : records;
        } catch (Throwable t) {
            return "Records unavailable: " + t.getMessage();
        }
    }

    // Registered in C++ via JNI_OnLoad / RegisterNatives. Do not rename without updating the
    // method table in app/src/main/cpp/template_native.cpp.
    private static native int nativeInstallHooks(String packageName, String dataDir);
    private static native void nativeSyncFeatureState(int damageMultiplier, int defenseMultiplier,
                                                     boolean godMode, boolean freeShop);
    private static native String nativeGetShadowHookRecords();
}
