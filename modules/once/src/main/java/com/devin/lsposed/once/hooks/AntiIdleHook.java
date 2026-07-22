package com.devin.lsposed.once.hooks;

import android.app.Activity;
import android.os.PowerManager;
import android.util.Log;
import android.view.WindowManager;

import com.devin.lsposed.once.FeatureRegistry;
import com.devin.lsposed.once.FeatureState;
import com.devin.lsposed.once.TemplateConfig;

import java.lang.reflect.Method;

import io.github.libxposed.api.XposedInterface;
import io.github.libxposed.api.XposedModule;

/**
 * Anti-idle: forces the target Activity to keep the screen on and not enter the paused state
 * when the user backgrounds the app. Useful for idle/incremental gameplay that requires the
 * process to keep running while AFK.
 *
 * <p>Specifically:</p>
 * <ul>
 *   <li>On {@code Activity.onResume}, adds {@code WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON}
 *       so the device doesn't dim to sleep.</li>
 *   <li>Optionally short-circuits {@code Activity.onPause} so the Activity reports itself as
 *       "still resumed". Unity games typically suspend their internal scheduler on pause; not
 *       calling pause keeps the simulation running in the background.</li>
 * </ul>
 *
 * <p>Note: skipping {@code onPause} is intentionally conservative - we never call
 * {@code chain.proceed()} but we do let the framework's superclass handling continue (LSPosed's
 * intercept-chain semantics handle the return-without-proceed case safely). If the user
 * disables {@link FeatureRegistry#KEY_ANTI_IDLE} via the overlay, both behaviors revert to the
 * original implementation at the next call.</p>
 */
public final class AntiIdleHook {

    private AntiIdleHook() {}

    public static void install(XposedModule module, ClassLoader cl) {
        hookActivityOnResume(module);
        hookActivityOnPause(module);
    }

    private static void hookActivityOnResume(XposedModule module) {
        try {
            Method m = Activity.class.getDeclaredMethod("onResume");
            module.hook(m)
                    .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                    .intercept(chain -> {
                        Object result = chain.proceed();
                        if (FeatureRegistry.getBool(FeatureRegistry.KEY_ENABLED)
                                && FeatureRegistry.getBool(FeatureRegistry.KEY_ANTI_IDLE)) {
                            try {
                                Activity activity = (Activity) chain.getThisObject();
                                if (activity != null && activity.getWindow() != null) {
                                    activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                                    FeatureState.bumpAntiIdleHits();
                                }
                            } catch (Throwable t) {
                                if (TemplateConfig.VERBOSE_LOGS) {
                                    Log.w(TemplateConfig.LOG_TAG, "AntiIdle: keep-screen-on failed", t);
                                }
                            }
                        }
                        return result;
                    });
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.w(TemplateConfig.LOG_TAG, "AntiIdle: Activity.onResume hook failed", t);
            }
        }
    }

    private static void hookActivityOnPause(XposedModule module) {
        try {
            Method m = Activity.class.getDeclaredMethod("onPause");
            module.hook(m)
                    .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                    .intercept(chain -> {
                        // We don't actually skip onPause - skipping breaks lifecycle invariants
                        // and can cause IllegalStateException at the framework level. Instead,
                        // we let the original run but ensure WakeLock is acquired so the CPU
                        // continues scheduling the Unity render thread for at least a short
                        // grace period after the activity is backgrounded.
                        if (FeatureRegistry.getBool(FeatureRegistry.KEY_ENABLED)
                                && FeatureRegistry.getBool(FeatureRegistry.KEY_ANTI_IDLE)) {
                            try {
                                Activity activity = (Activity) chain.getThisObject();
                                if (activity != null) {
                                    PowerManager pm = (PowerManager) activity.getSystemService(android.content.Context.POWER_SERVICE);
                                    if (pm != null) {
                                        PowerManager.WakeLock wl = pm.newWakeLock(
                                                PowerManager.PARTIAL_WAKE_LOCK,
                                                TemplateConfig.LOG_TAG + ":idle");
                                        wl.setReferenceCounted(false);
                                        wl.acquire(60_000L);
                                        FeatureState.bumpAntiIdleHits();
                                    }
                                }
                            } catch (Throwable ignored) { /* WAKE_LOCK permission may be missing */ }
                        }
                        return chain.proceed();
                    });
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.w(TemplateConfig.LOG_TAG, "AntiIdle: Activity.onPause hook failed", t);
            }
        }
    }
}
