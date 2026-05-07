package com.template.lsposed;

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

    private FeatureState() {}

    public static boolean isEnabled() {
        return FeatureRegistry.getBool(FeatureRegistry.KEY_ENABLED);
    }

    public static void setEnabled(boolean value) {
        FeatureRegistry.setBool(FeatureRegistry.KEY_ENABLED, true);
        lastMessage = "Enabled";
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

    public static void setLastMessage(String message) {
        if (message != null && !message.isEmpty()) lastMessage = message;
    }

    public static void setEngineLabel(String label) {
        if (label != null && !label.isEmpty()) engineLabel = label;
    }

    public static String summary() {
        return String.format(Locale.US,
                "Enabled: %s\nFree currency: %s\nGod-mode: %s\nGame speed: %s \u00d7%.1f\nOHK: %s\nAttack speed: %s \u00d7%.1f\nAtk paths: stat=%s idle=%s attack=%s roster=%s\nEngine: %s\nNative install attempts: %d\nStatus: %s",
                isEnabled() ? "yes" : "no",
                FeatureRegistry.getBool(FeatureRegistry.KEY_FREE_CURRENCY) ? "yes" : "no",
                FeatureRegistry.getBool(FeatureRegistry.KEY_GOD_MODE) ? "yes" : "no",
                FeatureRegistry.getBool(FeatureRegistry.KEY_GAME_SPEED) ? "yes" : "no",
                getMultiplier(),
                FeatureRegistry.getBool(FeatureRegistry.KEY_OHK) ? "yes" : "no",
                FeatureRegistry.getBool(FeatureRegistry.KEY_ATTACK_SPEED) ? "yes" : "no",
                FeatureRegistry.getFloat(FeatureRegistry.KEY_ATTACK_SPEED_MULTIPLIER),
                FeatureRegistry.getBool(FeatureRegistry.KEY_ATTACK_SPEED_BATTLE_STAT) ? "on" : "off",
                FeatureRegistry.getBool(FeatureRegistry.KEY_ATTACK_SPEED_IDLE_TIMER) ? "on" : "off",
                FeatureRegistry.getBool(FeatureRegistry.KEY_ATTACK_SPEED_ATTACK_TIMER) ? "on" : "off",
                FeatureRegistry.getBool(FeatureRegistry.KEY_ATTACK_SPEED_ROSTER_STAT) ? "on" : "off",
                engineLabel,
                nativeInstallAttempts.get(),
                lastMessage);
    }
}
