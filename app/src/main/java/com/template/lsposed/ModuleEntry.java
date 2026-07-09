package com.template.lsposed;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.os.Build;
import android.util.Log;

import java.lang.reflect.Method;

import io.github.libxposed.api.XposedInterface;
import io.github.libxposed.api.XposedModule;

/**
 * Modern libxposed API 101 entry point.
 *
 * <p>Registered in {@code META-INF/xposed/java_init.list}. The framework calls this class in each
 * scoped target process. Process-level filtering is enforced here (see {@link TemplateConfig}) so
 * the module does not bloat satellite processes such as {@code :push}, {@code :pushservice},
 * {@code :gameservice} or dedicated anti-cheat processes that many games ship.</p>
 */
public final class ModuleEntry extends XposedModule {
    private volatile String packageName;
    private volatile String processName;
    private volatile boolean applicationAttachHookInstalled;
    private volatile boolean applicationContextReady;
    private volatile boolean activityHookInstalled;

    @Override
    public void onModuleLoaded(ModuleLoadedParam param) {
        try {
            processName = param.getProcessName();
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

        if (TemplateConfig.VERBOSE_LOGS) {
            log(Log.INFO, TemplateConfig.LOG_TAG, phase + " " + packageName
                    + " process=" + proc
                    + " firstPackage=" + param.isFirstPackage()
                    + " dataDir=" + param.getApplicationInfo().dataDir);
        }

        installApplicationAttachHook();
        // Install always; the hook itself checks the registry so users can flip it live.
        installActivityResumeHook();
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
        // Shared with the classic LSPatch entry; idempotent via a static guard.
        Bootstrap.start(appContext);
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
