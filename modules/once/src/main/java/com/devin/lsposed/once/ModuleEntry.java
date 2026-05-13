package com.devin.lsposed.once;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.devin.lsposed.once.engine.EngineDetector;
import com.devin.lsposed.once.hooks.AntiIdleHook;
import com.devin.lsposed.once.hooks.PairipBypassHook;
import com.devin.lsposed.once.hooks.TelemetryHook;
import com.devin.lsposed.once.hooks.TimeWarpHook;
import com.devin.lsposed.once.ui.OverlayController;

import java.lang.reflect.Method;
import java.util.Locale;

import io.github.libxposed.api.XposedInterface;
import io.github.libxposed.api.XposedModule;

/**
 * Modern libxposed API 101 entry point for the OnceWorld module.
 *
 * <p>Hooks installed (each gated by a {@link FeatureRegistry} key so the overlay can flip them
 * live without re-launching the target):</p>
 * <ul>
 *   <li>{@link TimeWarpHook} - {@code System.currentTimeMillis}, {@code SystemClock.uptimeMillis},
 *       {@code SystemClock.elapsedRealtime[Nanos]}, plus the
 *       {@code net.codestage.actk.androidnative.ACTkAndroidRoutines} JNI bridge that ACTk uses
 *       for tamper-resistant time queries. Multiplier comes from
 *       {@link FeatureRegistry#KEY_MULTIPLIER}.</li>
 *   <li>{@link PairipBypassHook} - no-ops Google PAIRIP's
 *       {@code com.pairip.SignatureCheck.verifyIntegrity} and the license-check callbacks so the
 *       hooked process can keep running on environments where Play's integrity attestation would
 *       otherwise kill it.</li>
 *   <li>{@link AntiIdleHook} - {@code Activity.onResume} adds {@code FLAG_KEEP_SCREEN_ON};
 *       {@code Activity.onPause} acquires a short partial wake-lock so the Unity render thread
 *       continues scheduling while the activity is backgrounded.</li>
 *   <li>{@link TelemetryHook} (off by default) - silences Firebase Analytics and Crashlytics in
 *       the target process.</li>
 *   <li>Sample {@code Activity.onResume} log hook - useful while developing further hooks.</li>
 * </ul>
 *
 * <p>Process-level filtering is enforced via {@link TemplateConfig#shouldHookProcess(String, String)}
 * so we never load into satellite processes ({@code :push}, {@code :crashpad_handler}, etc.).</p>
 */
public final class ModuleEntry extends XposedModule {
    private volatile String packageName;
    private volatile String processName;
    private volatile ClassLoader targetClassLoader;
    private volatile boolean applicationAttachHookInstalled;
    private volatile boolean applicationContextReady;
    private volatile boolean activityHookInstalled;
    private volatile boolean targetHooksInstalled;

    @Override
    public void onModuleLoaded(ModuleLoadedParam param) {
        try {
            processName = param.getProcessName();
            // Register feature defaults early so any hook installed on this code path can
            // safely query FeatureRegistry.getBool/getFloat even before Application.attach.
            FeatureRegistry.registerDefaults();
            if (TemplateConfig.VERBOSE_LOGS) {
                log(Log.INFO, TemplateConfig.LOG_TAG, "Loaded in process=" + processName
                        + ", framework=" + getFrameworkName()
                        + " " + getFrameworkVersion()
                        + ", api=" + getApiVersion());
            }
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) Log.e(TemplateConfig.LOG_TAG, "onModuleLoaded log failed", t);
        }
    }

    @Override
    public void onPackageLoaded(PackageLoadedParam param) {
        preparePackage(param, "onPackageLoaded");
    }

    @Override
    public void onPackageReady(PackageReadyParam param) {
        preparePackage(param, "onPackageReady");
    }

    private void preparePackage(PackageLoadedParam param, String phase) {
        String pkg = param.getPackageName();
        String proc = processName != null ? processName : pkg;
        if (!TemplateConfig.shouldHookProcess(pkg, proc)) return;
        packageName = pkg;
        try {
            targetClassLoader = param.getDefaultClassLoader();
        } catch (Throwable ignored) {
            targetClassLoader = null;
        }

        if (TemplateConfig.VERBOSE_LOGS) {
            log(Log.INFO, TemplateConfig.LOG_TAG, phase + " " + packageName
                    + " process=" + proc
                    + " firstPackage=" + param.isFirstPackage()
                    + " dataDir=" + param.getApplicationInfo().dataDir);
        }

        installApplicationAttachHook();
        installActivityResumeHook();
        installTargetHooks();
    }

    /**
     * Installs feature hooks the moment the target package is loaded. The hooks themselves check
     * FeatureRegistry on every invocation so users can toggle behavior from the overlay without
     * relaunching the target.
     *
     * <p>Installing here (rather than after {@code Application.attach}) means PAIRIP's
     * {@code SignatureCheck.verifyIntegrity} and {@code StartupLauncher} class loads happen
     * <em>after</em> our hooks are wired, so the no-op intercept takes effect on the very first
     * invocation.</p>
     */
    private synchronized void installTargetHooks() {
        if (targetHooksInstalled) return;
        targetHooksInstalled = true;
        ClassLoader cl = targetClassLoader;
        if (cl == null) {
            cl = getClass().getClassLoader();
        }
        try {
            TimeWarpHook.install(this, cl);
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                log(Log.ERROR, TemplateConfig.LOG_TAG, "TimeWarpHook.install failed", t);
            }
        }
        try {
            PairipBypassHook.install(this, cl);
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                log(Log.ERROR, TemplateConfig.LOG_TAG, "PairipBypassHook.install failed", t);
            }
        }
        try {
            AntiIdleHook.install(this, cl);
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                log(Log.ERROR, TemplateConfig.LOG_TAG, "AntiIdleHook.install failed", t);
            }
        }
        try {
            TelemetryHook.install(this, cl);
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                log(Log.ERROR, TemplateConfig.LOG_TAG, "TelemetryHook.install failed", t);
            }
        }
    }

    private synchronized void installApplicationAttachHook() {
        if (applicationAttachHookInstalled) return;
        applicationAttachHookInstalled = true;
        try {
            Method attach = Application.class.getDeclaredMethod("attach", Context.class);
            attach.setAccessible(true);
            hook(attach)
                    .setPriority(XposedInterface.PRIORITY_HIGHEST)
                    .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                    .intercept(chain -> {
                        Object result = chain.proceed();
                        try {
                            Object thiz = chain.getThisObject();
                            Context ctx = thiz instanceof Context ? (Context) thiz : null;
                            if (ctx == null && !chain.getArgs().isEmpty() && chain.getArg(0) instanceof Context) {
                                ctx = (Context) chain.getArg(0);
                            }
                            if (ctx != null) onApplicationContextReady(ctx);
                        } catch (Throwable t) {
                            if (TemplateConfig.VERBOSE_LOGS) {
                                log(Log.ERROR, TemplateConfig.LOG_TAG, "Application.attach post-hook failed", t);
                            }
                        }
                        return result;
                    });
            if (TemplateConfig.VERBOSE_LOGS) {
                log(Log.INFO, TemplateConfig.LOG_TAG, "Hooked Application.attach");
            }
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                log(Log.ERROR, TemplateConfig.LOG_TAG, "Could not hook Application.attach", t);
            }
        }
    }

    private synchronized void onApplicationContextReady(Context context) {
        if (applicationContextReady) return;
        applicationContextReady = true;
        Context appContext = context.getApplicationContext() != null ? context.getApplicationContext() : context;

        FeatureRegistry.registerDefaults();
        FeatureRegistry.attachPersistence(appContext);

        try {
            EngineDetector.Engine engine = EngineDetector.detect(appContext);
            FeatureState.setEngineLabel(engine.name().toLowerCase(Locale.US));
            if (TemplateConfig.VERBOSE_LOGS) {
                log(Log.INFO, TemplateConfig.LOG_TAG, "Detected engine=" + engine
                        + " evidence=" + EngineDetector.evidenceFromNativeLibraryDir(appContext));
            }
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                log(Log.WARN, TemplateConfig.LOG_TAG, "Engine detection failed", t);
            }
        }

        if (TemplateConfig.ENABLE_OVERLAY) {
            new Handler(Looper.getMainLooper()).post(() -> OverlayController.attach(appContext));
        }
    }

    private synchronized void installActivityResumeHook() {
        if (activityHookInstalled) return;
        activityHookInstalled = true;
        try {
            Method onResume = Activity.class.getDeclaredMethod("onResume");
            onResume.setAccessible(true);
            hook(onResume)
                    .setPriority(XposedInterface.PRIORITY_LOWEST)
                    .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                    .intercept(chain -> {
                        Object result = chain.proceed();
                        try {
                            if (!FeatureRegistry.getBool(FeatureRegistry.KEY_ENABLED)) return result;
                            if (!FeatureRegistry.getBool(FeatureRegistry.KEY_SAMPLE_ACTIVITY_HOOK)) return result;
                            int hits = FeatureState.bumpJavaHookHits();
                            Object thiz = chain.getThisObject();
                            String activityName = thiz != null ? thiz.getClass().getName() : "<null>";
                            FeatureState.setLastMessage("Activity.onResume #" + hits + ": " + activityName);
                            if (TemplateConfig.VERBOSE_LOGS) {
                                log(Log.INFO, TemplateConfig.LOG_TAG,
                                        "Activity.onResume #" + hits + " -> " + activityName);
                            }
                        } catch (Throwable t) {
                            if (TemplateConfig.VERBOSE_LOGS) {
                                log(Log.ERROR, TemplateConfig.LOG_TAG,
                                        "Activity.onResume post-hook failed", t);
                            }
                        }
                        return result;
                    });
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                deoptimize(onResume);
            }
            if (TemplateConfig.VERBOSE_LOGS) {
                log(Log.INFO, TemplateConfig.LOG_TAG, "Hooked Activity.onResume sample");
            }
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                log(Log.ERROR, TemplateConfig.LOG_TAG, "Could not hook Activity.onResume", t);
            }
        }
    }
}
