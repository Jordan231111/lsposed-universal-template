package com.template.lsposed;

import android.content.Context;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicLong;
import java.util.regex.Pattern;

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
    private static final int MAX_STATE_BYTES = 64 * 1024;

    public static final String KEY_ENABLED = "enabled";
    public static final String KEY_MULTIPLIER = "multiplier";
    public static final String KEY_NATIVE_HOOKS = "native_hooks";

    public enum Type { BOOL, FLOAT }

    public static final class Feature {
        public final String key;
        public final String label;
        public final Type type;
        public final float defaultFloat;
        public final float min;
        public final float max;
        public final boolean defaultBool;
        public final float step;

        private Feature(String key, String label, Type type,
                        boolean defaultBool, float defaultFloat, float min, float max, float step) {
            if (key == null || !key.matches("[A-Za-z0-9_.-]{1,96}") || label == null || label.isBlank())
                throw new IllegalArgumentException("Feature key/label is invalid");
            if (type == Type.FLOAT && (!Float.isFinite(defaultFloat) || !Float.isFinite(min)
                    || !Float.isFinite(max) || !Float.isFinite(max - min) || min >= max
                    || defaultFloat < min || defaultFloat > max || !Float.isFinite(step) || step < 0
                    || (step > 0 && Math.ceil(((double) max - min) / step) > 10000)))
                throw new IllegalArgumentException("Invalid numeric bounds, default or step");
            this.key = key;
            this.label = label;
            this.type = type;
            this.defaultBool = defaultBool;
            this.defaultFloat = defaultFloat;
            this.min = min;
            this.max = max;
            this.step = step;
        }

        public static Feature bool(String key, String label, boolean def) {
            return new Feature(key, label, Type.BOOL, def, 0f, 0f, 0f, 0f);
        }

        public static Feature number(String key, String label, float def, float min, float max) {
            return number(key, label, def, min, max, 0f);
        }

        public static Feature number(String key, String label, float def, float min, float max, float step) {
            return new Feature(key, label, Type.FLOAT, false, def, min, max, step);
        }

        public static Feature integer(String key, String label, int def, int min, int max) {
            if (min < -16_777_216 || max > 16_777_216)
                throw new IllegalArgumentException("Integer controls must fit the exact float integer range");
            return number(key, label, def, min, max, 1f);
        }

        private float normalize(float value) {
            float bounded = Math.max(min, Math.min(max, value));
            if (step == 0 || bounded == min || bounded == max) return bounded;
            double snapped = min + Math.round(((double) bounded - min) / step) * (double) step;
            return Math.max(min, Math.min(max, (float) snapped));
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
    private static final Map<String, String> LOADED_VALUES = new ConcurrentHashMap<>();
    private static final AtomicLong revision = new AtomicLong();
    private static long savedRevision;
    private static volatile File storage;
    private static volatile boolean initialized;

    private FeatureRegistry() {}

    /** Called once per process before the overlay is attached or any hook reads a value. */
    public static synchronized void initialize(Context ctx) {
        if (!initialized) {
            register(Feature.bool(KEY_ENABLED, "Module enabled", true));
            register(Feature.bool(KEY_NATIVE_HOOKS, "Native helpers at startup",
                    TemplateConfig.ENABLE_NATIVE_SHADOWHOOK));
            register(Feature.number(KEY_MULTIPLIER, "Example multiplier", 1f, 1f, 30f, 0.1f));
            initialized = true;
        }
        // An early context-free lookup must not permanently prevent persistence initialization.
        if (ctx != null && storage == null) {
            try {
                storage = new File(ctx.getFilesDir(), FILE_NAME);
                load();
            } catch (Throwable t) {
                if (TemplateConfig.VERBOSE_LOGS) {
                    Log.w(TemplateConfig.LOG_TAG, "FeatureRegistry load failed", t);
                }
            }
        }
    }

    public static synchronized void register(Feature feature) {
        if (feature == null || feature.key == null) return;
        Feature previous = FEATURES.get(feature.key);
        if (previous != null && previous.type != feature.type)
            throw new IllegalArgumentException("A registered feature cannot change its value type");
        FEATURES.put(feature.key, feature);
        ORDER.addIfAbsent(feature.key);
        if (feature.type == Type.BOOL && !BOOLS.containsKey(feature.key)) {
            BOOLS.put(feature.key, feature.defaultBool);
        }
        if (feature.type == Type.FLOAT && !FLOATS.containsKey(feature.key)) {
            FLOATS.put(feature.key, feature.normalize(feature.defaultFloat));
        }
        String loaded = LOADED_VALUES.get(feature.key);
        if (previous == null && loaded != null) applyRaw(feature.key, loaded);
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
            revision.incrementAndGet();
            notifyChanged(key);
        }
        commit();
    }

    public static float getFloat(String key) {
        Float value = FLOATS.get(key);
        if (value != null) return value;
        Feature f = FEATURES.get(key);
        return f != null ? f.defaultFloat : 0f;
    }

    public static void setFloat(String key, float value) {
        updateFloat(key, value, true);
    }

    /** Live slider preview; commit once when dragging ends or the panel closes. */
    public static void previewFloat(String key, float value) {
        updateFloat(key, value, false);
    }

    private static void updateFloat(String key, float value, boolean persist) {
        Feature f = FEATURES.get(key);
        if (f == null || f.type != Type.FLOAT || !Float.isFinite(value)) return;
        float clamped = f.normalize(value);
        Float prev = FLOATS.put(key, clamped);
        if (prev == null || prev != clamped) {
            revision.incrementAndGet();
            notifyChanged(key);
        }
        if (persist) commit();
    }

    public static void resetToDefaults() {
        for (Feature f : features()) {
            if (f.type == Type.BOOL) BOOLS.put(f.key, f.defaultBool);
            else FLOATS.put(f.key, f.normalize(f.defaultFloat));
        }
        revision.incrementAndGet();
        commit();
        for (Feature f : features()) notifyChanged(f.key);
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

    public static synchronized void commit() {
        long current = revision.get();
        if (current != savedRevision && save()) savedRevision = current;
    }

    private static boolean save() {
        File file = storage;
        if (file == null) return false;
        StringBuilder sb = new StringBuilder(256);
        sb.append("{");
        boolean first = true;
        Map<String, String> values = new HashMap<>(LOADED_VALUES);
        for (Map.Entry<String, Boolean> e : BOOLS.entrySet()) values.put(e.getKey(), Boolean.toString(e.getValue()));
        for (Map.Entry<String, Float> e : FLOATS.entrySet()) values.put(e.getKey(), Float.toString(e.getValue()));
        for (Map.Entry<String, String> e : values.entrySet()) {
            if (!first) sb.append(',');
            first = false;
            sb.append('"').append(escape(e.getKey())).append("\":")
                    .append(e.getValue());
        }
        sb.append('}');

        File tmp = new File(file.getParentFile(), file.getName() + ".tmp");
        byte[] bytes = sb.toString().getBytes(StandardCharsets.UTF_8);
        if (bytes.length > MAX_STATE_BYTES) return false;
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(bytes);
            out.getFD().sync();
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.w(TemplateConfig.LOG_TAG, "FeatureRegistry save failed", t);
            }
            return false;
        }
        // Preserve the last complete settings file if atomic replacement fails.
        return tmp.renameTo(file);
    }

    private static synchronized void load() {
        File file = storage;
        if (file == null || !file.exists()) return;
        if (file.length() > MAX_STATE_BYTES) return;
        ByteArrayOutputStream buf = new ByteArrayOutputStream((int) file.length());
        try (FileInputStream in = new FileInputStream(file)) {
            byte[] chunk = new byte[512];
            int n;
            while ((n = in.read(chunk)) > 0) {
                // Enforce the bound during reading too, in case another process grows the file.
                if (buf.size() + n > MAX_STATE_BYTES) return;
                buf.write(chunk, 0, n);
            }
        } catch (Throwable t) {
            return;
        }
        parseAndApply(new String(buf.toByteArray(), StandardCharsets.UTF_8));
    }

    private static final Pattern ENTRY = Pattern.compile(
            "\\s*\"([A-Za-z0-9_.-]{1,96})\"\\s*:\\s*"
            + "(true|false|-?(?:0|[1-9][0-9]*)(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)\\s*(,|$)");

    private static void parseAndApply(String json) {
        if (json == null) return;
        String text = json.trim();
        if (text.length() < 2 || text.charAt(0) != '{' || text.charAt(text.length() - 1) != '}') return;
        String body = text.substring(1, text.length() - 1).trim();
        Map<String, String> values = new HashMap<>();
        var matcher = ENTRY.matcher(body);
        int position = 0;
        while (position < body.length()) {
            matcher.region(position, body.length());
            if (!matcher.lookingAt() || values.putIfAbsent(matcher.group(1), matcher.group(2)) != null) return;
            position = matcher.end();
            if (",".equals(matcher.group(3)) && position == body.length()) return;
        }
        // Validate the entire flat settings document before applying any values.
        LOADED_VALUES.putAll(values);
        for (Map.Entry<String, String> entry : values.entrySet()) applyRaw(entry.getKey(), entry.getValue());
    }

    private static void applyRaw(String key, String raw) {
        Feature f = FEATURES.get(key);
        if (f == null) return;
        try {
            if (f.type == Type.BOOL) {
                if ("true".equals(raw)) BOOLS.put(key, true);
                else if ("false".equals(raw)) BOOLS.put(key, false);
            } else {
                float v = Float.parseFloat(raw);
                if (Float.isFinite(v)) FLOATS.put(key, f.normalize(v));
            }
        } catch (Throwable ignored) {
        }
    }

    private static String escape(String value) {
        return value.replace("\\", "\\\\").replace("\"", "\\\"");
    }
}
