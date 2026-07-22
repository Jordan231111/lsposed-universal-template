package com.devin.lsposed.rogue;

import java.util.Locale;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Status/statistics accessor. Actual feature toggles live in {@link FeatureRegistry} so the
 * overlay, hooks, and persistence share a single source of truth. Keep this class for counters
 * and the last status line shown in the overlay.
 */
public final class FeatureState {
    private static volatile String lastMessage = "Ready";
    private static volatile String engineLabel = "unknown";
    private static final AtomicInteger javaHookHits = new AtomicInteger();
    private static final AtomicInteger nativeInstallAttempts = new AtomicInteger();
    private static final AtomicInteger timeQueries = new AtomicInteger();
    private static final AtomicInteger actkBypassHits = new AtomicInteger();
    private static final AtomicInteger pairipBypassHits = new AtomicInteger();
    private static final AtomicInteger antiIdleHits = new AtomicInteger();
    private static final AtomicInteger telemetrySuppressed = new AtomicInteger();

    private FeatureState() {}

    public static boolean isEnabled() {
        return FeatureRegistry.getBool(FeatureRegistry.KEY_ENABLED);
    }

    public static void setEnabled(boolean value) {
        FeatureRegistry.setBool(FeatureRegistry.KEY_ENABLED, value);
        lastMessage = value ? "Enabled" : "Disabled";
    }

    public static float getMultiplier() {
        return FeatureRegistry.getFloat(FeatureRegistry.KEY_MULTIPLIER);
    }

    public static void setMultiplier(float value) {
        if (Float.isNaN(value) || Float.isInfinite(value)) value = 1.0f;
        FeatureRegistry.setFloat(FeatureRegistry.KEY_MULTIPLIER, value);
        lastMessage = String.format(Locale.US, "Multiplier ×%.1f", getMultiplier());
    }

    public static int bumpJavaHookHits() {
        return javaHookHits.incrementAndGet();
    }

    public static int bumpNativeInstallAttempts() {
        return nativeInstallAttempts.incrementAndGet();
    }

    public static int bumpTimeQueries() {
        return timeQueries.incrementAndGet();
    }

    public static int bumpActkBypassHits() {
        return actkBypassHits.incrementAndGet();
    }

    public static int bumpPairipBypassHits() {
        return pairipBypassHits.incrementAndGet();
    }

    public static int bumpAntiIdleHits() {
        return antiIdleHits.incrementAndGet();
    }

    public static int bumpTelemetrySuppressed() {
        return telemetrySuppressed.incrementAndGet();
    }

    public static void setLastMessage(String message) {
        if (message != null && !message.isEmpty()) lastMessage = message;
    }

    public static void setEngineLabel(String label) {
        if (label != null && !label.isEmpty()) engineLabel = label;
    }

    public static String summary() {
        return String.format(Locale.US,
                "Enabled: %s\nMultiplier: \u00d7%.1f\nEngine: %s\nTime queries: %d\nACTk bypass hits: %d\nPAIRIP bypass: %d\nAnti-idle: %d\nTelemetry suppressed: %d\nJava hook hits: %d\nStatus: %s",
                isEnabled() ? "yes" : "no",
                getMultiplier(),
                engineLabel,
                timeQueries.get(),
                actkBypassHits.get(),
                pairipBypassHits.get(),
                antiIdleHits.get(),
                telemetrySuppressed.get(),
                javaHookHits.get(),
                lastMessage);
    }
}
