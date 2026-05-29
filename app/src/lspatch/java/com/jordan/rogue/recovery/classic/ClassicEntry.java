package com.jordan.rogue.recovery.classic;

import android.app.Application;
import android.content.Context;
import android.util.Log;

import com.jordan.rogue.recovery.FeatureRegistry;
import com.jordan.rogue.recovery.NativeBridge;
import com.jordan.rogue.recovery.TemplateConfig;

import java.lang.reflect.Method;

import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XC_MethodReplacement;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.XposedHelpers;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

/**
 * Classic Xposed entry point for LSPatch-patched builds.
 *
 * <p>LSPatch uses the legacy {@code de.robv.android.xposed} API rather than libxposed-api 101.
 * This entry point reproduces the minimum bootstrap needed for the native ShadowHook layer to do
 * the heavy lifting: bypass PAIRIP's signature/license checks at Java level, then hand off to
 * {@link NativeBridge}. The overlay UI is not wired up here because LSPatch builds run in the
 * target's own process without the helper Activity, and the floating overlay needs an Activity
     * context. Toggle state is loaded through {@link com.jordan.rogue.recovery.FeatureRegistry}
     * from the target app's persisted `.rt_state` file when present, otherwise it falls back to
     * {@link TemplateConfig} compile-time defaults.</p>
 *
 * <p>This class is only loaded in the {@code lspatch} build flavor. The default {@code lsposed}
 * flavor continues to use the libxposed API 101 module entry under
 * {@code com.jordan.rogue.recovery.ModuleEntry}.</p>
 */
public final class ClassicEntry implements IXposedHookLoadPackage {
    private static final String TAG = TemplateConfig.LOG_TAG;

    @Override
    public void handleLoadPackage(XC_LoadPackage.LoadPackageParam lpparam) {
        if (lpparam == null || lpparam.packageName == null) return;
        if (!TemplateConfig.shouldHook(lpparam.packageName)) return;

        XposedBridge.log(TAG + ": ClassicEntry loaded for " + lpparam.packageName);

        installPairipBypass(lpparam.classLoader);
        hookApplicationAttach(lpparam.classLoader, lpparam.packageName);
    }

    private void installPairipBypass(ClassLoader cl) {
        if (!TemplateConfig.ENABLE_INTEGRITY_BYPASS) return;
        hookNoOpVoid(cl, "com.pairip.SignatureCheck", "verifyIntegrity", Context.class);
        hookReturnTrue(cl, "com.pairip.SignatureCheck", "verifySignatureMatches", String.class);
        hookNoOpVoid(cl, "com.pairip.licensecheck.LicenseClient", "checkLicense", Context.class);
        hookNoOpVoid(cl, "com.pairip.licensecheck.LicenseClient", "initializeLicenseCheck");
        // LicenseResponseHelper.validateResponse has multiple overloads; iterate methods reflectively.
        try {
            Class<?> helper = XposedHelpers.findClass(
                    "com.pairip.licensecheck.LicenseResponseHelper", cl);
            for (Method m : helper.getDeclaredMethods()) {
                if (!"validateResponse".equals(m.getName())) continue;
                XposedBridge.hookMethod(m, XC_MethodReplacement.returnConstant(null));
            }
        } catch (Throwable t) {
            Log.w(TAG, "LicenseResponseHelper.validateResponse hook skipped: " + t.getMessage());
        }
    }

    private void hookApplicationAttach(ClassLoader cl, String packageName) {
        if (!TemplateConfig.ENABLE_NATIVE_SHADOWHOOK) return;
        XposedHelpers.findAndHookMethod(Application.class, "attach", Context.class,
                new XC_MethodHook() {
                    @Override
                    protected void afterHookedMethod(MethodHookParam param) {
                        Context ctx = (Context) param.args[0];
                        if (ctx == null) return;
                        FeatureRegistry.initialize(ctx.getApplicationContext() != null
                                ? ctx.getApplicationContext() : ctx);
                        Thread worker = new Thread(() -> {
                            try {
                                NativeBridge.installNativeHooks(ctx, packageName);
                            } catch (Throwable t) {
                                Log.w(TAG, "Native install (LSPatch flavor) failed", t);
                            }
                        }, TemplateConfig.WORKER_THREAD_NAME);
                        worker.setDaemon(true);
                        worker.start();
                    }
                });
    }

    private static void hookNoOpVoid(ClassLoader cl, String cls, String method, Class<?>... args) {
        try {
            Object[] hookArgs = new Object[args.length + 1];
            System.arraycopy(args, 0, hookArgs, 0, args.length);
            hookArgs[args.length] = XC_MethodReplacement.returnConstant(null);
            XposedHelpers.findAndHookMethod(cls, cl, method, hookArgs);
        } catch (Throwable t) {
            Log.w(TAG, "hook " + cls + "." + method + " skipped: " + t.getMessage());
        }
    }

    private static void hookReturnTrue(ClassLoader cl, String cls, String method, Class<?>... args) {
        try {
            Object[] hookArgs = new Object[args.length + 1];
            System.arraycopy(args, 0, hookArgs, 0, args.length);
            hookArgs[args.length] = XC_MethodReplacement.returnConstant(true);
            XposedHelpers.findAndHookMethod(cls, cl, method, hookArgs);
        } catch (Throwable t) {
            Log.w(TAG, "hook " + cls + "." + method + " skipped: " + t.getMessage());
        }
    }
}
