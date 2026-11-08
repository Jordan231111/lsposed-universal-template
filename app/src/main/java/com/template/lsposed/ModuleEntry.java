package com.template.lsposed;

import android.annotation.SuppressLint;
import android.app.Application;
import android.content.Context;
import android.util.Log;

import java.lang.reflect.Method;

import io.github.libxposed.api.XposedInterface;
import io.github.libxposed.api.XposedModule;

/**
 * Modern libxposed API 102 entry point.
 *
 * <p>Registered in {@code META-INF/xposed/java_init.list}. Vector and LSPatch 1.2 call this class in
 * each scoped target process. Process-level filtering is enforced here (see
 * {@link TemplateConfig}) so the module does not bloat satellite processes such as {@code :push},
 * {@code :pushservice}, {@code :gameservice}, or dedicated anti-cheat processes.</p>
 */
public final class ModuleEntry extends XposedModule {
    private volatile String processName;
    private volatile boolean applicationAttachHookInstalled;
    private volatile boolean applicationContextReady;

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
        String proc = processName;
        if (proc == null || proc.isEmpty()) return;
        if (!TemplateConfig.shouldHookProcess(pkg, proc)) return;
        if (TemplateConfig.VERBOSE_LOGS) {
            log(Log.INFO, TemplateConfig.LOG_TAG, phase + " " + pkg
                    + " process=" + proc
                    + " firstPackage=" + param.isFirstPackage()
                    + " dataDir=" + param.getApplicationInfo().dataDir);
        }

        installApplicationAttachHook();
    }

    @SuppressLint("DiscouragedPrivateApi") // Xposed needs this stable pre-Activity context handoff.
    private synchronized void installApplicationAttachHook() {
        if (applicationAttachHookInstalled) return;
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
            // A failure in onPackageLoaded must leave onPackageReady able to retry.
            applicationAttachHookInstalled = true;
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
        if (!TemplateConfig.shouldHook(context.getPackageName())) return;
        applicationContextReady = true;
        Context appContext = context.getApplicationContext() != null ? context.getApplicationContext() : context;
        // Idempotent via Bootstrap's static guard.
        Bootstrap.start(appContext);
    }

}
