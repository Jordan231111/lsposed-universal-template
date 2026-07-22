package com.devin.lsposed.rogue.hooks;

import android.os.SystemClock;
import android.util.Log;

import com.devin.lsposed.rogue.FeatureRegistry;
import com.devin.lsposed.rogue.FeatureState;
import com.devin.lsposed.rogue.TemplateConfig;

import java.lang.reflect.Method;
import java.util.concurrent.atomic.AtomicLong;

import io.github.libxposed.api.XposedInterface;
import io.github.libxposed.api.XposedModule;

/**
 * Time-warp: presents the target with a time stream that runs faster than wall-clock
 * by the configured multiplier ({@link FeatureRegistry#KEY_MULTIPLIER}).
 *
 * <p>The implementation anchors at install time: each clock query returns
 * {@code anchor + (real_now - anchor) * multiplier}. That keeps the stream monotonic across
 * multiplier changes and avoids the negative jumps that {@code now * multiplier} would
 * produce when the multiplier is lowered.</p>
 *
 * <p>The hook covers four Java time sources:</p>
 * <ul>
 *   <li>{@code java.lang.System.currentTimeMillis()} (UTC wall clock)</li>
 *   <li>{@code android.os.SystemClock.uptimeMillis()} (time since boot, excluding deep sleep)</li>
 *   <li>{@code android.os.SystemClock.elapsedRealtime()} / {@code elapsedRealtimeNanos()}
 *       (time since boot, including deep sleep)</li>
 * </ul>
 *
 * <p>It also hooks the {@code net.codestage.actk.androidnative.ACTkAndroidRoutines} JNI bridge
 * methods that the CodeStage Anti-Cheat Toolkit uses to fetch a "tamper-proof" time. ACTk routes
 * its time queries through that bridge specifically to bypass native syscall hooks - so a Java
 * LSPosed hook on the bridge methods both speeds up the game AND avoids ACTk's time-tamper
 * detection in one shot.</p>
 */
public final class TimeWarpHook {

    private static final AtomicLong ANCHOR_WALL_MS = new AtomicLong(0);
    private static final AtomicLong ANCHOR_UPTIME_MS = new AtomicLong(0);
    private static final AtomicLong ANCHOR_ELAPSED_MS = new AtomicLong(0);
    private static final AtomicLong ANCHOR_ELAPSED_NS = new AtomicLong(0);
    private static volatile boolean anchored = false;

    private TimeWarpHook() {}

    /** Returns the user-configured time multiplier, clamped to a positive value. */
    public static float currentMultiplier() {
        if (!FeatureRegistry.getBool(FeatureRegistry.KEY_ENABLED)) return 1f;
        if (!FeatureRegistry.getBool(FeatureRegistry.KEY_TIME_WARP)) return 1f;
        float m = FeatureState.getMultiplier();
        if (Float.isNaN(m) || Float.isInfinite(m) || m <= 0f) return 1f;
        return m;
    }

    private static synchronized void anchorIfNeeded() {
        if (anchored) return;
        ANCHOR_WALL_MS.set(System.currentTimeMillis());
        ANCHOR_UPTIME_MS.set(SystemClock.uptimeMillis());
        ANCHOR_ELAPSED_MS.set(SystemClock.elapsedRealtime());
        ANCHOR_ELAPSED_NS.set(SystemClock.elapsedRealtimeNanos());
        anchored = true;
    }

    private static long warp(long realNow, long anchor, float multiplier) {
        long delta = realNow - anchor;
        if (delta < 0L) return realNow;
        return anchor + (long) (delta * multiplier);
    }

    public static long warpedWallMs() {
        anchorIfNeeded();
        return warp(System.currentTimeMillis(), ANCHOR_WALL_MS.get(), currentMultiplier());
    }

    public static long warpedUptimeMs() {
        anchorIfNeeded();
        return warp(SystemClock.uptimeMillis(), ANCHOR_UPTIME_MS.get(), currentMultiplier());
    }

    public static long warpedElapsedMs() {
        anchorIfNeeded();
        return warp(SystemClock.elapsedRealtime(), ANCHOR_ELAPSED_MS.get(), currentMultiplier());
    }

    public static long warpedElapsedNs() {
        anchorIfNeeded();
        long realNow = SystemClock.elapsedRealtimeNanos();
        long anchor = ANCHOR_ELAPSED_NS.get();
        float m = currentMultiplier();
        long delta = realNow - anchor;
        if (delta < 0L) return realNow;
        return anchor + (long) (delta * (double) m);
    }

    /**
     * Installs all time-warp hooks. Safe to call once per package-load callback; individual
     * hook installation failures are logged and skipped without crashing the target.
     */
    public static void install(XposedModule module, ClassLoader cl) {
        anchorIfNeeded();
        hookSystemCurrentTimeMillis(module);
        hookSystemClockUptime(module);
        hookSystemClockElapsed(module);
        hookSystemClockElapsedNanos(module);
        hookActkBridge(module, cl);
    }

    private static void hookSystemCurrentTimeMillis(XposedModule module) {
        try {
            Method m = System.class.getDeclaredMethod("currentTimeMillis");
            module.hook(m)
                    .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                    .intercept(chain -> {
                        if (currentMultiplier() == 1f) return chain.proceed();
                        FeatureState.bumpTimeQueries();
                        return warpedWallMs();
                    });
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.w(TemplateConfig.LOG_TAG, "TimeWarp: System.currentTimeMillis hook failed", t);
            }
        }
    }

    private static void hookSystemClockUptime(XposedModule module) {
        try {
            Method m = SystemClock.class.getDeclaredMethod("uptimeMillis");
            module.hook(m)
                    .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                    .intercept(chain -> {
                        if (currentMultiplier() == 1f) return chain.proceed();
                        FeatureState.bumpTimeQueries();
                        return warpedUptimeMs();
                    });
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.w(TemplateConfig.LOG_TAG, "TimeWarp: SystemClock.uptimeMillis hook failed", t);
            }
        }
    }

    private static void hookSystemClockElapsed(XposedModule module) {
        try {
            Method m = SystemClock.class.getDeclaredMethod("elapsedRealtime");
            module.hook(m)
                    .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                    .intercept(chain -> {
                        if (currentMultiplier() == 1f) return chain.proceed();
                        FeatureState.bumpTimeQueries();
                        return warpedElapsedMs();
                    });
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.w(TemplateConfig.LOG_TAG, "TimeWarp: SystemClock.elapsedRealtime hook failed", t);
            }
        }
    }

    private static void hookSystemClockElapsedNanos(XposedModule module) {
        try {
            Method m = SystemClock.class.getDeclaredMethod("elapsedRealtimeNanos");
            module.hook(m)
                    .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                    .intercept(chain -> {
                        if (currentMultiplier() == 1f) return chain.proceed();
                        FeatureState.bumpTimeQueries();
                        return warpedElapsedNs();
                    });
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.w(TemplateConfig.LOG_TAG, "TimeWarp: SystemClock.elapsedRealtimeNanos hook failed", t);
            }
        }
    }

    /**
     * Hooks the CodeStage Anti-Cheat Toolkit Java bridge methods. ACTk explicitly routes time
     * queries through these static methods to defeat native-only time spoofing; hooking them at
     * the Java layer both speeds time up AND silences ACTk's time-tamper alarm.
     *
     * <p>If the target build does not ship ACTk (different game version, different obfuscator
     * configuration), the class lookup fails silently and we fall back to the System/SystemClock
     * hooks installed above.</p>
     */
    private static void hookActkBridge(XposedModule module, ClassLoader cl) {
        if (!FeatureRegistry.getBool(FeatureRegistry.KEY_ACTK_BYPASS)) return;
        Class<?> actk;
        try {
            actk = Class.forName("net.codestage.actk.androidnative.ACTkAndroidRoutines", false, cl);
        } catch (Throwable ignored) {
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.i(TemplateConfig.LOG_TAG, "TimeWarp: ACTk bridge class not present, skipping");
            }
            return;
        }
        hookActkMethod(module, actk, "GetSystemCurrentTimeMs", TimeWarpHook::warpedWallMs);
        hookActkMethod(module, actk, "GetSystemNanoTime", TimeWarpHook::warpedElapsedNs);
        hookActkMethod(module, actk, "GetSystemNanoTimeMs", TimeWarpHook::warpedElapsedMs);
    }

    private interface LongSupplier { long getAsLong(); }

    private static void hookActkMethod(XposedModule module, Class<?> klass, String name,
                                       LongSupplier supplier) {
        try {
            Method m = klass.getDeclaredMethod(name);
            module.hook(m)
                    .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                    .intercept(chain -> {
                        if (currentMultiplier() == 1f) return chain.proceed();
                        FeatureState.bumpActkBypassHits();
                        return supplier.getAsLong();
                    });
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.i(TemplateConfig.LOG_TAG, "TimeWarp: hooked ACTk " + name);
            }
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.w(TemplateConfig.LOG_TAG, "TimeWarp: ACTk " + name + " hook failed", t);
            }
        }
    }
}
