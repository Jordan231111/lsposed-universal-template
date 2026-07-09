package com.template.lsposed;

import android.app.Application;
import android.content.Context;

import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedHelpers;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

/**
 * Classic Xposed entry point for LSPatch (non-root). LSPatch only implements the classic
 * {@code de.robv.android.xposed} API (API level 93), so this is registered via
 * {@code assets/xposed_init} instead of the modern {@code META-INF/xposed/java_init.list} used by
 * {@link ModuleEntry} on LSPosed. A module that only ships the modern entry is silently rejected by
 * LSPatch — see {@code docs/LSPATCH_NONROOT.md}.
 *
 * <p>It hooks {@code Application.attach(Context)} to obtain the app context, then hands off to the
 * shared {@link Bootstrap}. Both entries funnel through Bootstrap, which is idempotent.</p>
 */
public final class LSPatchEntry implements IXposedHookLoadPackage {

    @Override
    public void handleLoadPackage(XC_LoadPackage.LoadPackageParam lpparam) {
        if (lpparam == null || !TemplateConfig.shouldHook(lpparam.packageName)) return;
        try {
            XposedHelpers.findAndHookMethod(Application.class, "attach", Context.class,
                    new XC_MethodHook() {
                        @Override
                        protected void afterHookedMethod(MethodHookParam param) {
                            Context ctx = null;
                            if (param.thisObject instanceof Context) {
                                ctx = (Context) param.thisObject;
                            } else if (param.args != null && param.args.length > 0
                                    && param.args[0] instanceof Context) {
                                ctx = (Context) param.args[0];
                            }
                            if (ctx != null) {
                                Bootstrap.start(ctx);
                            }
                        }
                    });
        } catch (Throwable ignored) {
            // Fail closed: never crash the target.
        }
    }
}
