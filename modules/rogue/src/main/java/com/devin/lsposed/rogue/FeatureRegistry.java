package com.devin.lsposed.rogue;

import android.content.Context;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;

/**
 * Runtime feature registry. Each feature declares a key, a human label, a type, and a default
 * value. The overlay renders features as toggle rows; feature consumers read the live value at
 * every relevant hook invocation.
 *
 * <p>Persistence is best-effort file IO under the target app's {@code getFilesDir()} with an
 * innocuous filename. That keeps toggle state across the target app's process restarts but does
 * leave a file inside the target's sandbox.</p>
 */
public final class FeatureRegistry {
    private static final String FILE_NAME = TemplateConfig.FEATURE_STATE_FILE_NAME;

    public static final String KEY_ENABLED = "enabled";
    public static final String KEY_MULTIPLIER = "multiplier";
    public static final String KEY_SAMPLE_ACTIVITY_HOOK = "sample_activity_hook";
    public static final String KEY_TIME_WARP = "time_warp";
    public static final String KEY_ACTK_BYPASS = "actk_bypass";
    public static final String KEY_PAIRIP_BYPASS = "pairip_bypass";
    public static final String KEY_ANTI_IDLE = "anti_idle";
    public static final String KEY_DISABLE_TELEMETRY = "disable_telemetry";

    public enum Type { BOOL, FLOAT }

    public static final class Feature {
        public final String key;
        public final String label;
        public final Type type;
        public final float defaultFloat;
        public final float min;
        public final float max;
        public final boolean defaultBool;

        private Feature(String key, String label, Type type,
                        boolean defaultBool, float defaultFloat, float min, float max) {
            this.key = key;
            this.label = label;
            this.type = type;
            this.defaultBool = defaultBool;
            this.defaultFloat = defaultFloat;
            this.min = min;
            this.max = max;
        }

        public static Feature bool(String key, String label, boolean def) {
            return new Feature(key, label, Type.BOOL, def, 0f, 0f, 0f);
        }

        public static Feature number(String key, String label, float def, float min, float max) {
            return new Feature(key, label, Type.FLOAT, false, def, min, max);
        }
    }

    public interface Listener {
        void onChanged(String key);
    }

    private static final Map<String, Feature> FEATURES = new ConcurrentHashMap<>();
    private static final CopyOnWriteArrayList<String> ORDER = new CopyOnWriteArrayList<>();
    private static final Map<String, Boolean> BOOLS = new ConcurrentHashMap<>();
    private static final Map<String, Float> FLOATS = new ConcurrentHashMap<>();
    private static final List<Listener> LISTENERS = new CopyOnWriteArrayList<>();
    private static volatile File storage;
    private static volatile boolean initialized;

    private FeatureRegistry() {}

    /**
     * Registers built-in features with their defaults. Idempotent and context-free so it can
     * run as early as {@code onPackageLoaded} - before the target app's {@code Application}
     * is even constructed - so hooks installed at that point already see valid defaults.
     */
    public static synchronized void registerDefaults() {
        if (initialized) return;
        initialized = true;

        register(Feature.bool(KEY_ENABLED, "Module enabled", true));
        register(Feature.bool(KEY_TIME_WARP, "Time multiplier (idle/cooldowns)", true));
        register(Feature.bool(KEY_ACTK_BYPASS, "ACTk time-tamper bypass", true));
        register(Feature.bool(KEY_PAIRIP_BYPASS, "PAIRIP signature/license no-op", true));
        register(Feature.bool(KEY_ANTI_IDLE, "Anti-idle (keep screen on, no pause)", true));
        register(Feature.bool(KEY_DISABLE_TELEMETRY, "Suppress telemetry/analytics", false));
        register(Feature.bool(KEY_SAMPLE_ACTIVITY_HOOK, "Log Activity.onResume",
                TemplateConfig.ENABLE_SAMPLE_ACTIVITY_LOG_HOOK));
        register(Feature.number(KEY_MULTIPLIER, "Time multiplier", 4f, 1f, 30f));
    }

    /**
     * Attaches the on-disk persistence file once the target's app context is available.
     * Safe to call multiple times (only the first wires up storage).
     */
    public static synchronized void attachPersistence(Context ctx) {
        if (storage != null || ctx == null) return;
        try {
            storage = new File(ctx.getFilesDir(), FILE_NAME);
            load();
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.w(TemplateConfig.LOG_TAG, "FeatureRegistry load failed", t);
            }
        }
    }

    /** Legacy convenience wrapper used by older callers. Equivalent to the two-step API above. */
    public static synchronized void initialize(Context ctx) {
        registerDefaults();
        if (ctx != null) attachPersistence(ctx);
    }

    public static synchronized void register(Feature feature) {
        if (feature == null || feature.key == null) return;
        FEATURES.put(feature.key, feature);
        ORDER.addIfAbsent(feature.key);
        if (feature.type == Type.BOOL && !BOOLS.containsKey(feature.key)) {
            BOOLS.put(feature.key, feature.defaultBool);
        }
        if (feature.type == Type.FLOAT && !FLOATS.containsKey(feature.key)) {
            FLOATS.put(feature.key, feature.defaultFloat);
        }
    }

    public static List<Feature> features() {
        List<Feature> out = new ArrayList<>(ORDER.size());
        for (String key : ORDER) {
            Feature f = FEATURES.get(key);
            if (f != null) out.add(f);
        }
        return out;
    }

    public static boolean getBool(String key) {
        Boolean value = BOOLS.get(key);
        if (value != null) return value;
        Feature f = FEATURES.get(key);
        return f != null && f.defaultBool;
    }

    public static void setBool(String key, boolean value) {
        Feature f = FEATURES.get(key);
        if (f == null || f.type != Type.BOOL) return;
        Boolean prev = BOOLS.put(key, value);
        if (prev == null || prev != value) {
            save();
            notifyChanged(key);
        }
    }

    public static float getFloat(String key) {
        Float value = FLOATS.get(key);
        if (value != null) return value;
        Feature f = FEATURES.get(key);
        return f != null ? f.defaultFloat : 0f;
    }

    public static void setFloat(String key, float value) {
        Feature f = FEATURES.get(key);
        if (f == null || f.type != Type.FLOAT) return;
        float clamped = Math.max(f.min, Math.min(f.max, value));
        Float prev = FLOATS.put(key, clamped);
        if (prev == null || prev != clamped) {
            save();
            notifyChanged(key);
        }
    }

    public static void resetToDefaults() {
        for (Feature f : features()) {
            if (f.type == Type.BOOL) BOOLS.put(f.key, f.defaultBool);
            else FLOATS.put(f.key, f.defaultFloat);
            notifyChanged(f.key);
        }
        save();
    }

    public static void addListener(Listener listener) {
        if (listener != null && !LISTENERS.contains(listener)) LISTENERS.add(listener);
    }

    public static void removeListener(Listener listener) {
        if (listener != null) LISTENERS.remove(listener);
    }

    private static void notifyChanged(String key) {
        for (Listener l : LISTENERS) {
            try {
                l.onChanged(key);
            } catch (Throwable t) {
                if (TemplateConfig.VERBOSE_LOGS) {
                    Log.w(TemplateConfig.LOG_TAG, "Listener failed for " + key, t);
                }
            }
        }
    }

    private static synchronized void save() {
        File file = storage;
        if (file == null) return;
        StringBuilder sb = new StringBuilder(256);
        sb.append("{");
        boolean first = true;
        for (Map.Entry<String, Boolean> e : BOOLS.entrySet()) {
            if (!first) sb.append(',');
            first = false;
            sb.append('"').append(escape(e.getKey())).append("\":")
                    .append(Boolean.TRUE.equals(e.getValue()) ? "true" : "false");
        }
        for (Map.Entry<String, Float> e : FLOATS.entrySet()) {
            if (!first) sb.append(',');
            first = false;
            Float value = e.getValue();
            sb.append('"').append(escape(e.getKey())).append("\":")
                    .append(String.format(Locale.US, "%.4f", value == null ? 0f : value));
        }
        sb.append('}');

        File tmp = new File(file.getParentFile(), file.getName() + ".tmp");
        byte[] bytes = sb.toString().getBytes(StandardCharsets.UTF_8);
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(bytes);
            out.getFD().sync();
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.w(TemplateConfig.LOG_TAG, "FeatureRegistry save failed", t);
            }
            return;
        }
        if (!tmp.renameTo(file)) {
            // best-effort; fall back to an in-place write
            try (FileOutputStream out = new FileOutputStream(file)) {
                out.write(bytes);
            } catch (Throwable ignored) {
            }
        }
    }

    private static synchronized void load() {
        File file = storage;
        if (file == null || !file.exists()) return;
        ByteArrayOutputStream buf = new ByteArrayOutputStream((int) Math.min(64 * 1024, file.length()));
        try (FileInputStream in = new FileInputStream(file)) {
            byte[] chunk = new byte[512];
            int n;
            while ((n = in.read(chunk)) > 0) buf.write(chunk, 0, n);
        } catch (Throwable t) {
            return;
        }
        parseAndApply(new String(buf.toByteArray(), StandardCharsets.UTF_8));
    }

    private static void parseAndApply(String json) {
        if (json == null) return;
        String body = json.trim();
        if (body.length() < 2 || body.charAt(0) != '{' || body.charAt(body.length() - 1) != '}') return;
        body = body.substring(1, body.length() - 1).trim();
        if (body.isEmpty()) return;

        int i = 0;
        while (i < body.length()) {
            while (i < body.length() && Character.isWhitespace(body.charAt(i))) i++;
            if (i >= body.length() || body.charAt(i) != '"') return;
            int keyStart = ++i;
            while (i < body.length() && body.charAt(i) != '"') i++;
            if (i >= body.length()) return;
            String key = body.substring(keyStart, i);
            i++;
            while (i < body.length() && body.charAt(i) != ':') i++;
            if (i >= body.length()) return;
            i++;
            while (i < body.length() && Character.isWhitespace(body.charAt(i))) i++;
            if (i >= body.length()) return;
            int valStart = i;
            while (i < body.length() && body.charAt(i) != ',') i++;
            String raw = body.substring(valStart, i).trim();
            applyRaw(key, raw);
            if (i < body.length() && body.charAt(i) == ',') i++;
        }
    }

    private static void applyRaw(String key, String raw) {
        Feature f = FEATURES.get(key);
        if (f == null) return;
        try {
            if (f.type == Type.BOOL) {
                BOOLS.put(key, Boolean.parseBoolean(raw));
            } else {
                float v = Float.parseFloat(raw);
                FLOATS.put(key, Math.max(f.min, Math.min(f.max, v)));
            }
        } catch (Throwable ignored) {
        }
    }

    private static String escape(String value) {
        return value.replace("\\", "\\\\").replace("\"", "\\\"");
    }
}
