package com.template.lsposed.ui;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.text.method.LinkMovementMethod;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.ScrollView;
import android.widget.TextView;

import com.template.lsposed.R;
import com.template.lsposed.TemplateConfig;

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
        text.setText(getString(R.string.launcher_instructions, firstTarget()));

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.rgb(14, 14, 20));
        scroll.addView(text);
        setContentView(scroll);
    }

    private int dp(float value) {
        return (int) (value * getResources().getDisplayMetrics().density);
    }

    private static String firstTarget() {
        return TemplateConfig.TARGET_PACKAGES.length == 0
                ? "<none configured>" : TemplateConfig.TARGET_PACKAGES[0];
    }
}
