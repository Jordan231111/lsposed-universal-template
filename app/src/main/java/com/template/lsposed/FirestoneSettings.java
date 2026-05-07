package com.template.lsposed;

import android.content.ContentResolver;
import android.content.Context;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Process;
import android.util.Log;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

public final class FirestoneSettings {
    public static final String AUTHORITY = BuildConfig.APPLICATION_ID + ".settings";
    public static final Uri URI = Uri.parse("content://" + AUTHORITY + "/config");
    public static final String METHOD_READ = "read";
    public static final String METHOD_WRITE = "write";
    public static final String EXTRA_JSON = "json";
    private static volatile boolean providerFailureLogged;

    private FirestoneSettings() {}

    public static String defaultJson() {
        try {
            JSONObject o = new JSONObject();
            o.put(FeatureRegistry.KEY_ENABLED, true);
            o.put(FeatureRegistry.KEY_NATIVE_HOOKS, true);
            o.put(FeatureRegistry.KEY_FREE_CURRENCY, true);
            o.put(FeatureRegistry.KEY_GOD_MODE, true);
            o.put(FeatureRegistry.KEY_GAME_SPEED, false);
            o.put(FeatureRegistry.KEY_WAVE_SPEED, false);
            o.put(FeatureRegistry.KEY_OHK, false);
            o.put(FeatureRegistry.KEY_ATTACK_SPEED, false);
            o.put(FeatureRegistry.KEY_ATTACK_SPEED_BATTLE_STAT, true);
            o.put(FeatureRegistry.KEY_ATTACK_SPEED_IDLE_TIMER, true);
            o.put(FeatureRegistry.KEY_ATTACK_SPEED_ATTACK_TIMER, true);
            o.put(FeatureRegistry.KEY_ATTACK_SPEED_ROSTER_STAT, true);
            o.put(FeatureRegistry.KEY_SLOW_ENEMIES, false);
            o.put(FeatureRegistry.KEY_MULTIPLIER, 2.0);
            o.put(FeatureRegistry.KEY_WAVE_SPEED_MULTIPLIER, 2.0);
            o.put(FeatureRegistry.KEY_DAMAGE_MULTIPLIER, 1000.0);
            o.put(FeatureRegistry.KEY_ATTACK_SPEED_MULTIPLIER, 2.0);
            o.put(FeatureRegistry.KEY_ENEMY_ATTACK_SPEED_MULTIPLIER, 2.0);
            return o.toString();
        } catch (Throwable t) {
            return "{\"enabled\":true,\"native_hooks\":true,\"free_currency\":true,\"god_mode\":true}";
        }
    }

    /**
     * Keep the historical master flag from leaving the target half-disabled. Runtime control now
     * happens through per-feature toggles; LSPosed Manager remains the authoritative way to disable
     * the module itself.
     */
    public static String normalizeJson(String json) {
        try {
            JSONObject fallback = new JSONObject(defaultJson());
            JSONObject out = json != null && !json.trim().isEmpty()
                    ? new JSONObject(json)
                    : new JSONObject();
            for (String key : new String[] {
                    FeatureRegistry.KEY_ENABLED,
                    FeatureRegistry.KEY_NATIVE_HOOKS,
                    FeatureRegistry.KEY_FREE_CURRENCY,
                    FeatureRegistry.KEY_GOD_MODE,
                    FeatureRegistry.KEY_GAME_SPEED,
                    FeatureRegistry.KEY_WAVE_SPEED,
                    FeatureRegistry.KEY_OHK,
                    FeatureRegistry.KEY_ATTACK_SPEED,
                    FeatureRegistry.KEY_ATTACK_SPEED_BATTLE_STAT,
                    FeatureRegistry.KEY_ATTACK_SPEED_IDLE_TIMER,
                    FeatureRegistry.KEY_ATTACK_SPEED_ATTACK_TIMER,
                    FeatureRegistry.KEY_ATTACK_SPEED_ROSTER_STAT,
                    FeatureRegistry.KEY_SLOW_ENEMIES,
                    FeatureRegistry.KEY_MULTIPLIER,
                    FeatureRegistry.KEY_WAVE_SPEED_MULTIPLIER,
                    FeatureRegistry.KEY_DAMAGE_MULTIPLIER,
                    FeatureRegistry.KEY_ATTACK_SPEED_MULTIPLIER,
                    FeatureRegistry.KEY_ENEMY_ATTACK_SPEED_MULTIPLIER
            }) {
                if (!out.has(key)) out.put(key, fallback.get(key));
            }
            out.put(FeatureRegistry.KEY_ENABLED, true);
            return out.toString();
        } catch (Throwable t) {
            return defaultJson();
        }
    }

    public static String readFromProvider(Context context) {
        if (context == null) return defaultJson();
        try {
            ContentResolver resolver = providerContext(context).getContentResolver();
            Bundle result = resolver.call(URI, METHOD_READ, null, null);
            String json = result != null ? result.getString(EXTRA_JSON) : null;
            json = normalizeJson(json);
            if (canWriteModulePublicFile(context)) writePublicSettingsFile(json);
            providerFailureLogged = false;
            return json;
        } catch (Throwable t) {
            String publicJson = readPublicSettingsFile();
            if (publicJson != null) return normalizeJson(publicJson);
            if (TemplateConfig.VERBOSE_LOGS && !providerFailureLogged) {
                providerFailureLogged = true;
                Log.w(TemplateConfig.LOG_TAG,
                        "Settings provider and public fallback read failed; using defaults", t);
            }
            return defaultJson();
        }
    }

    public static boolean writeToProvider(Context context, String json) {
        if (context == null || json == null || json.isEmpty()) return false;
        String normalized = normalizeJson(json);
        if (canWriteModulePublicFile(context)) writePublicSettingsFile(normalized);
        try {
            Bundle extras = new Bundle();
            extras.putString(EXTRA_JSON, normalized);
            Bundle result = providerContext(context).getContentResolver().call(URI, METHOD_WRITE, null, extras);
            return result == null || result.getBoolean("ok", true);
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) Log.w(TemplateConfig.LOG_TAG, "Settings provider write failed", t);
            return false;
        }
    }

    public static File targetSettingsFile(Context targetContext) {
        return new File(targetContext.getFilesDir(), TemplateConfig.FEATURE_STATE_FILE_NAME);
    }

    public static String syncProviderToTargetFile(Context targetContext) {
        String json = normalizeJson(readFromProvider(targetContext));
        writeTargetFile(targetContext, json);
        return json;
    }

    public static boolean writeTargetFile(Context targetContext, String json) {
        if (targetContext == null || json == null) return false;
        json = normalizeJson(json);
        File out = targetSettingsFile(targetContext);
        File tmp = new File(out.getParentFile(), out.getName() + ".tmp");
        byte[] bytes = json.getBytes(StandardCharsets.UTF_8);
        try (FileOutputStream fos = new FileOutputStream(tmp)) {
            fos.write(bytes);
            fos.getFD().sync();
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) Log.w(TemplateConfig.LOG_TAG, "Settings temp write failed", t);
            return false;
        }
        if (!tmp.renameTo(out)) {
            try (FileOutputStream fos = new FileOutputStream(out)) {
                fos.write(bytes);
            } catch (Throwable t) {
                return false;
            }
        }
        return true;
    }

    public static boolean writePublicSettingsFile(String json) {
        if (json == null) return false;
        json = normalizeJson(json);
        File out = publicSettingsFile();
        File parent = out.getParentFile();
        if (parent == null || (!parent.exists() && !parent.mkdirs())) return false;
        File tmp = new File(parent, out.getName() + ".tmp");
        byte[] bytes = json.getBytes(StandardCharsets.UTF_8);
        try (FileOutputStream fos = new FileOutputStream(tmp)) {
            fos.write(bytes);
            fos.getFD().sync();
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) Log.w(TemplateConfig.LOG_TAG, "Public settings write failed", t);
            return false;
        }
        tmp.setReadable(true, false);
        out.setReadable(true, false);
        if (!tmp.renameTo(out)) {
            try (FileOutputStream fos = new FileOutputStream(out)) {
                fos.write(bytes);
            } catch (Throwable t) {
                return false;
            }
        }
        out.setReadable(true, false);
        return true;
    }

    public static float floatValue(JSONObject json, String key, float fallback, float min, float max) {
        double value = json.optDouble(key, fallback);
        if (Double.isNaN(value) || Double.isInfinite(value)) value = fallback;
        return Math.max(min, Math.min(max, (float) value));
    }

    public static String format(float value) {
        return String.format(Locale.US, "%.2f", value);
    }

    public static File publicSettingsFile() {
        return new File(new File(Environment.getExternalStorageDirectory(),
                "Android/media/" + BuildConfig.APPLICATION_ID), TemplateConfig.FEATURE_STATE_FILE_NAME);
    }

    private static Context providerContext(Context context) {
        if (context == null || BuildConfig.APPLICATION_ID.equals(context.getPackageName())) {
            return context;
        }
        try {
            return context.createPackageContext(BuildConfig.APPLICATION_ID, Context.CONTEXT_IGNORE_SECURITY);
        } catch (Throwable ignored) {
            return context;
        }
    }

    private static boolean canWriteModulePublicFile(Context context) {
        if (context == null) return false;
        try {
            return BuildConfig.APPLICATION_ID.equals(context.getPackageName())
                    && context.getApplicationInfo() != null
                    && Process.myUid() == context.getApplicationInfo().uid;
        } catch (Throwable ignored) {
            return false;
        }
    }

    private static String readPublicSettingsFile() {
        File file = publicSettingsFile();
        if (!file.exists()) return null;
        byte[] buf = new byte[(int) Math.min(file.length(), 64 * 1024)];
        int pos = 0;
        try (FileInputStream in = new FileInputStream(file)) {
            int n;
            while (pos < buf.length && (n = in.read(buf, pos, buf.length - pos)) > 0) {
                pos += n;
            }
        } catch (Throwable ignored) {
            return null;
        }
        return pos > 0 ? new String(buf, 0, pos, StandardCharsets.UTF_8) : null;
    }
}
