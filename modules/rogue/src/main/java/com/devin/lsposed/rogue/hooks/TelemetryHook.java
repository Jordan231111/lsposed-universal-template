package com.devin.lsposed.rogue.hooks;

import android.util.Log;

import com.devin.lsposed.rogue.FeatureRegistry;
import com.devin.lsposed.rogue.FeatureState;
import com.devin.lsposed.rogue.TemplateConfig;

import java.lang.reflect.Method;

import io.github.libxposed.api.XposedInterface;
import io.github.libxposed.api.XposedModule;

/**
 * Best-effort telemetry suppression. Disabled by default because nuking analytics inside an app
 * that uses analytics signals for anti-cheat heuristics can be more visible than just letting
 * them run. Enable from the overlay if you want to silence Firebase Analytics, Crashlytics, and
 * AppLovin in the target app's process.
 */
public final class TelemetryHook {

    private TelemetryHook() {}

    public static void install(XposedModule module, ClassLoader cl) {
        if (!FeatureRegistry.getBool(FeatureRegistry.KEY_DISABLE_TELEMETRY)) return;
        hookFirebaseAnalytics(module, cl);
        hookCrashlytics(module, cl);
    }

    private static void hookFirebaseAnalytics(XposedModule module, ClassLoader cl) {
        // FirebaseAnalytics.logEvent(String, Bundle)
        Class<?> fa;
        try {
            fa = Class.forName("com.google.firebase.analytics.FirebaseAnalytics", false, cl);
        } catch (Throwable ignored) { return; }
        for (Method m : fa.getDeclaredMethods()) {
            if (!"logEvent".equals(m.getName())) continue;
            try {
                module.hook(m)
                        .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                        .intercept(chain -> {
                            FeatureState.bumpTelemetrySuppressed();
                            return null;
                        });
            } catch (Throwable t) {
                if (TemplateConfig.VERBOSE_LOGS) {
                    Log.w(TemplateConfig.LOG_TAG, "Telemetry: FirebaseAnalytics.logEvent hook failed", t);
                }
            }
        }
    }

    private static void hookCrashlytics(XposedModule module, ClassLoader cl) {
        // FirebaseCrashlytics.recordException / log
        Class<?> fc;
        try {
            fc = Class.forName("com.google.firebase.crashlytics.FirebaseCrashlytics", false, cl);
        } catch (Throwable ignored) { return; }
        for (Method m : fc.getDeclaredMethods()) {
            String n = m.getName();
            if (!("recordException".equals(n) || "log".equals(n) || "setCustomKey".equals(n))) continue;
            try {
                module.hook(m)
                        .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                        .intercept(chain -> {
                            FeatureState.bumpTelemetrySuppressed();
                            return null;
                        });
            } catch (Throwable t) {
                if (TemplateConfig.VERBOSE_LOGS) {
                    Log.w(TemplateConfig.LOG_TAG, "Telemetry: Crashlytics." + n + " hook failed", t);
                }
            }
        }
    }
}
