package com.template.lsposed;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.template.lsposed.engine.EngineDetector;
import com.template.lsposed.ui.OverlayController;

import java.util.Locale;

/**
 * Shared, framework-agnostic init. Both entry points call this exactly once per process:
 *   - {@link ModuleEntry}  (modern libxposed API 101, used by LSPosed / root)
 *   - {@link LSPatchEntry} (classic {@code de.robv.android.xposed} API, used by LSPatch / non-root)
 *
 * <p>A static guard makes it idempotent, so it is safe even if a framework happens to invoke both
 * entries. It initialises the feature registry, detects the engine, wires the native-hook toggle
 * listener, starts native hooks if enabled, and attaches the overlay. This is the single init path
 * both entries hand off to once they have the target's {@link Context}.</p>
 */
public final class Bootstrap {
    private static volatile boolean started;
    private static volatile boolean nativeStarted;

    private Bootstrap() {}

    public static synchronized void start(Context context) {
        if (started || context == null) return;
        started = true;
        final Context app = context.getApplicationContext() != null
                ? context.getApplicationContext() : context;

        FeatureRegistry.initialize(app);

        try {
            EngineDetector.Engine engine = EngineDetector.detect(app);
            FeatureState.setEngineLabel(engine.name().toLowerCase(Locale.US));
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.i(TemplateConfig.LOG_TAG, "Detected engine=" + engine
                        + " evidence=" + EngineDetector.evidenceFromNativeLibraryDir(app));
            }
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.w(TemplateConfig.LOG_TAG, "Engine detection failed", t);
            }
        }

        // Mirror the enable/native toggles into the native worker so users can flip them live.
        FeatureRegistry.addListener(key -> {
            if (FeatureRegistry.KEY_ENABLED.equals(key) || FeatureRegistry.KEY_NATIVE_HOOKS.equals(key)) {
                maybeStartNative(app);
            }
        });

        maybeStartNative(app);

        if (TemplateConfig.ENABLE_OVERLAY) {
            new Handler(Looper.getMainLooper()).post(() -> OverlayController.attach(app));
        }
    }

    private static synchronized void maybeStartNative(Context app) {
        if (nativeStarted) return;
        if (!FeatureRegistry.getBool(FeatureRegistry.KEY_ENABLED)) return;
        if (!FeatureRegistry.getBool(FeatureRegistry.KEY_NATIVE_HOOKS)) return;
        nativeStarted = true;
        Thread worker = new Thread(
                () -> NativeBridge.installNativeHooks(app, app.getPackageName()),
                TemplateConfig.WORKER_THREAD_NAME);
        worker.setDaemon(true);
        worker.start();
    }
}
