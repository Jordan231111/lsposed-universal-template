package com.template.lsposed;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;

public final class SettingsProvider extends ContentProvider {
    private static final String PREFS = "firestone_settings";
    private static final String KEY_JSON = "json";

    @Override
    public boolean onCreate() {
        ensureDefaults();
        return true;
    }

    @Override
    public Bundle call(String method, String arg, Bundle extras) {
        ensureDefaults();
        Bundle out = new Bundle();
        if (FirestoneSettings.METHOD_READ.equals(method)) {
            String json = FirestoneSettings.normalizeJson(
                    prefs().getString(KEY_JSON, FirestoneSettings.defaultJson()));
            prefs().edit().putString(KEY_JSON, json).apply();
            FirestoneSettings.writePublicSettingsFile(json);
            out.putString(FirestoneSettings.EXTRA_JSON, json);
            out.putBoolean("ok", true);
            return out;
        }
        if (FirestoneSettings.METHOD_WRITE.equals(method)) {
            String json = extras != null ? extras.getString(FirestoneSettings.EXTRA_JSON) : null;
            if (json == null || json.isEmpty()) json = FirestoneSettings.defaultJson();
            json = FirestoneSettings.normalizeJson(json);
            prefs().edit().putString(KEY_JSON, json).apply();
            FirestoneSettings.writePublicSettingsFile(json);
            out.putBoolean("ok", true);
            return out;
        }
        out.putBoolean("ok", false);
        return out;
    }

    @Override public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) { return null; }
    @Override public String getType(Uri uri) { return "application/json"; }
    @Override public Uri insert(Uri uri, ContentValues values) { return null; }
    @Override public int delete(Uri uri, String selection, String[] selectionArgs) { return 0; }
    @Override public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) { return 0; }

    private void ensureDefaults() {
        String current = prefs().getString(KEY_JSON, null);
        String normalized = FirestoneSettings.normalizeJson(current);
        if (current == null || !normalized.equals(current)) {
            prefs().edit().putString(KEY_JSON, normalized).apply();
        }
        FirestoneSettings.writePublicSettingsFile(normalized);
    }

    private SharedPreferences prefs() {
        return getContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }
}
