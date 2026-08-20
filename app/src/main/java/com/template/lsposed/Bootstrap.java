package com.template.lsposed;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.template.lsposed.engine.EngineDetector;
import com.template.lsposed.ui.OverlayController;

import java.util.Locale;

/**
 * Framework-neutral initialization called by the API-102 {@link ModuleEntry} once per process.
 *
 * <p>A static guard makes it idempotent. It initializes the feature registry, detects the engine,
 * wires the native-hook toggle listener, starts native hooks if enabled, and attaches the overlay
 * once the target's {@link Context} is available.</p>
 */
public final class Bootstrap {
    private static final int ENGINE_DETECTION_RETRIES = 3;
    private static final long ENGINE_DETECTION_RETRY_MS = 1_000L;
    private static volatile boolean started;
    private static volatile boolean nativeStarted;

    private Bootstrap() {}

    public static synchronized void start(Context context) {
        if (started || context == null) return;
        started = true;
        final Context app = context.getApplicationContext() != null
                ? context.getApplicationContext() : context;

        FeatureRegistry.initialize(app);

        detectEngine(app, ENGINE_DETECTION_RETRIES);

        // Mirror the enable/native toggles into the native worker so users can flip them live.
        FeatureRegistry.addListener(key -> {
            if (FeatureRegistry.KEY_ENABLED.equals(key) || FeatureRegistry.KEY_NATIVE_HOOKS.equals(key)) {
                maybeStartNative(app);
            }
        });

        maybeStartNative(app);

        if (TemplateConfig.ENABLE_OVERLAY) {
            // Application.attach normally runs on the main thread. Register lifecycle callbacks
            // synchronously there so the first Activity.onResume cannot race ahead of us.
            if (Looper.myLooper() == Looper.getMainLooper()) {
                OverlayController.attach(app);
            } else {
                new Handler(Looper.getMainLooper()).post(() -> OverlayController.attach(app));
            }
        }
    }

    private static void detectEngine(Context app, int retriesRemaining) {
        try {
            EngineDetector.Engine engine = EngineDetector.detect(app);
            FeatureState.setEngineLabel(engine.name().toLowerCase(Locale.US));
            if (engine == EngineDetector.Engine.NATIVE && retriesRemaining > 0) {
                new Handler(Looper.getMainLooper()).postDelayed(
                        () -> detectEngine(app, retriesRemaining - 1),
                        ENGINE_DETECTION_RETRY_MS);
                return;
            }
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.i(TemplateConfig.LOG_TAG, "Detected engine=" + engine
                        + " evidence=" + EngineDetector.evidence(app));
            }
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.w(TemplateConfig.LOG_TAG, "Engine detection failed", t);
            }
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
