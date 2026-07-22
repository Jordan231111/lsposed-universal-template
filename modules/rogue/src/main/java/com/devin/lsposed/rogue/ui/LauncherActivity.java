package com.devin.lsposed.rogue.ui;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.text.method.LinkMovementMethod;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.ScrollView;
import android.widget.TextView;

import com.devin.lsposed.rogue.TemplateConfig;

/** Simple launcher screen so the APK is easy to identify after install. */
public final class LauncherActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        TextView text = new TextView(this);
        text.setTextColor(Color.rgb(230, 230, 240));
        text.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        text.setGravity(Gravity.START);
        text.setPadding(dp(18), dp(18), dp(18), dp(18));
        text.setLinksClickable(true);
        text.setMovementMethod(LinkMovementMethod.getInstance());
        text.setText("Rogue with the Dead Toolkit\n\n"
                + "Target package: " + TemplateConfig.TARGET_PACKAGES[0] + "\n\n"
                + "1. Enable this module in LSPosed / Vector.\n"
                + "2. The scope is pre-configured for the target above.\n"
                + "3. Force-stop and reopen the target app.\n\n"
                + "Hooks: Java-only (ABI-neutral - arm64 + x86_64). "
                + "Features: time multiplier (defeats ACTk), PAIRIP no-op, anti-idle, telemetry "
                + "suppression. Toggle live from the in-app bubble inside the target Activity.");

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.rgb(14, 14, 20));
        scroll.addView(text);
        setContentView(scroll);
    }

    private int dp(float value) {
        return (int) (value * getResources().getDisplayMetrics().density);
    }
}
