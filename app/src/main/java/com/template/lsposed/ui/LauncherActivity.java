package com.template.lsposed.ui;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import com.template.lsposed.FeatureRegistry;
import com.template.lsposed.FirestoneSettings;
import com.template.lsposed.TemplateConfig;

import org.json.JSONObject;

import java.util.Locale;

public final class LauncherActivity extends Activity {
    private JSONObject settings;
    private TextView status;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        settings = loadSettings();

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.rgb(14, 14, 20));

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(18), dp(18), dp(18), dp(24));
        scroll.addView(root);

        TextView title = text("Firestone Hooks", 21, Color.WHITE, true);
        root.addView(title);
        root.addView(text("Scope: " + TemplateConfig.TARGET_PACKAGES[0], 13, Color.rgb(180, 190, 210), false));

        addToggle(root, FeatureRegistry.KEY_NATIVE_HOOKS, "Install native hooks");
        addToggle(root, FeatureRegistry.KEY_FREE_CURRENCY, "Free currency / no spend");
        addToggle(root, FeatureRegistry.KEY_EVENT_EXCHANGE_ZERO_COST, "Event exchange zero cost");
        addToggle(root, FeatureRegistry.KEY_EVENT_EXCHANGE_LOCAL_ONLY, "Event exchange local only");
        addToggle(root, FeatureRegistry.KEY_GOD_MODE, "Hero god-mode");
        addToggle(root, FeatureRegistry.KEY_GAME_SPEED, "Game speed");
        addSlider(root, FeatureRegistry.KEY_MULTIPLIER, "Game speed", 0.25f, 32f, 0.25f);
        addToggle(root, FeatureRegistry.KEY_WAVE_SPEED, "Wave transition speed");
        addSlider(root, FeatureRegistry.KEY_WAVE_SPEED_MULTIPLIER, "Wave speed", 0.25f, 10f, 0.25f);
        addToggle(root, FeatureRegistry.KEY_OHK, "One-hit kill");
        addSlider(root, FeatureRegistry.KEY_DAMAGE_MULTIPLIER, "OHK damage exponent", 1000f, 1000000f, 1000f);
        addToggle(root, FeatureRegistry.KEY_ATTACK_SPEED, "Hero attack speed");
        addSlider(root, FeatureRegistry.KEY_ATTACK_SPEED_MULTIPLIER, "Hero attack speed", 1f, 20f, 0.5f);
        addToggle(root, FeatureRegistry.KEY_ATTACK_SPEED_BATTLE_STAT, "Atk battle stat");
        addToggle(root, FeatureRegistry.KEY_ATTACK_SPEED_IDLE_TIMER, "Atk idle gauge");
        addToggle(root, FeatureRegistry.KEY_ATTACK_SPEED_ATTACK_TIMER, "Atk attack timer");
        addToggle(root, FeatureRegistry.KEY_ATTACK_SPEED_ROSTER_STAT, "Atk roster stat");
        addToggle(root, FeatureRegistry.KEY_SLOW_ENEMIES, "Slow enemies");
        addSlider(root, FeatureRegistry.KEY_ENEMY_ATTACK_SPEED_MULTIPLIER, "Enemy attack interval", 1f, 25f, 0.5f);

        status = text("", 12, Color.rgb(150, 160, 180), false);
        status.setPadding(0, dp(14), 0, 0);
        root.addView(status);
        updateStatus();

        setContentView(scroll);
    }

    private JSONObject loadSettings() {
        try {
            return new JSONObject(FirestoneSettings.readFromProvider(this));
        } catch (Throwable t) {
            try {
                return new JSONObject(FirestoneSettings.defaultJson());
            } catch (Throwable ignored) {
                return new JSONObject();
            }
        }
    }

    private void saveSettings() {
        boolean ok = FirestoneSettings.writeToProvider(this, settings.toString());
        updateStatus();
        if (!ok) Toast.makeText(this, "Settings write failed", Toast.LENGTH_SHORT).show();
    }

    private void addToggle(LinearLayout root, String key, String label) {
        CheckBox box = new CheckBox(this);
        box.setText(label);
        box.setTextColor(Color.WHITE);
        box.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        box.setButtonTintList(android.content.res.ColorStateList.valueOf(Color.rgb(184, 154, 255)));
        box.setChecked(settings.optBoolean(key, false));
        box.setPadding(0, dp(10), 0, dp(2));
        box.setOnCheckedChangeListener((buttonView, isChecked) -> {
            try {
                settings.put(key, isChecked);
            } catch (Throwable ignored) {
            }
            saveSettings();
        });
        root.addView(box);
    }

    private void addSlider(LinearLayout root, String key, String label, float min, float max, float step) {
        TextView caption = text("", 14, Color.WHITE, false);
        caption.setPadding(0, dp(10), 0, 0);
        root.addView(caption);

        SeekBar bar = new SeekBar(this);
        int ticks = Math.max(1, Math.round((max - min) / step));
        bar.setMax(ticks);
        float initial = FirestoneSettings.floatValue(settings, key, min, min, max);
        bar.setProgress(Math.max(0, Math.min(ticks, Math.round((initial - min) / step))));
        setCaption(caption, label, min + bar.getProgress() * step);
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                float value = min + progress * step;
                setCaption(caption, label, value);
                if (fromUser) {
                    try {
                        settings.put(key, Double.parseDouble(String.format(Locale.US, "%.3f", value)));
                    } catch (Throwable ignored) {
                    }
                    saveSettings();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        root.addView(bar);
    }

    private void setCaption(TextView view, String label, float value) {
        view.setText(label + ": x" + FirestoneSettings.format(value));
    }

    private void updateStatus() {
        if (status == null) return;
        status.setText("Settings provider: " + FirestoneSettings.AUTHORITY
                + "\nPublic fallback: " + FirestoneSettings.publicSettingsFile()
                + "\nTarget file: /data/data/" + TemplateConfig.TARGET_PACKAGES[0]
                + "/files/" + TemplateConfig.FEATURE_STATE_FILE_NAME
                + "\nEnable this module in LSPosed and scope it to Firestone, then force-stop and launch the game.");
    }

    private TextView text(String value, int sp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(value);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        t.setGravity(Gravity.START);
        if (bold) t.setTypeface(t.getTypeface(), android.graphics.Typeface.BOLD);
        return t;
    }

    private int dp(float value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }
}
