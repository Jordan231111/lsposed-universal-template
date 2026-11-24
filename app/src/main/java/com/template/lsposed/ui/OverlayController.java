package com.template.lsposed.ui;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Application;
import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Rect;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.DisplayCutout;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import com.template.lsposed.FeatureRegistry;
import com.template.lsposed.FeatureState;
import com.template.lsposed.NativeBridge;
import com.template.lsposed.TemplateConfig;

/**
 * Floating in-Activity overlay.
 *
 * <p>The style intentionally mirrors the dark/lavender Nyx panel from the current module. The small
 * oval tag is movable. When opened, the rectangular panel is also movable by dragging the header.</p>
 */
public final class OverlayController {
    private static final Object LOCK = new Object();
    private static boolean started;

    private final Application app;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Rect bounds = new Rect();
    private final ViewTreeObserver.OnGlobalLayoutListener windowLayout = this::syncGeometry;
    private final Runnable recoverOverlay = new Runnable() {
        @Override public void run() {
            if (currentActivity != null) {
                ensureOverlayAlive();
                mainHandler.postDelayed(this, 2500);
            }
        }
    };

    private int densityDpi;
    private float fontScale;
    private boolean scaleUpdatePending;
    private final Runnable scaleUpdate = this::rebuildForScaleChange;
    private Activity currentActivity;
    private Activity attachedActivity;
    private ViewGroup overlayRoot;
    private TextView tagView;
    private View panelView;
    private View panelDragHandle;
    private OverlayDrag tagDrag, panelDrag;
    private TextView panelStats;
    private TextView panelTitle, panelSubtitle;
    private PanelUpdates panelListener;
    private OverlayFeatures features;
    private final OverlayDrag.Position bubblePosition = new OverlayDrag.Position();
    private final OverlayDrag.Position panelPosition = new OverlayDrag.Position();
    private boolean hidden, panelWanted;
    private int firstFeature, firstOffset;
    private AlertDialog inputDialog;

    private OverlayController(Application app) {
        this.app = app;
    }

    public static void attach(Context context) {
        if (context == null) return;
        Context appContext = context.getApplicationContext() != null ? context.getApplicationContext() : context;
        if (!(appContext instanceof Application)) {
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.w(TemplateConfig.LOG_TAG, "Overlay attach skipped: context is not an Application");
            }
            return;
        }
        FeatureRegistry.initialize(appContext);
        synchronized (LOCK) {
            if (started) return;
            started = true;
            new OverlayController((Application) appContext).start();
        }
    }

    private void start() {
        app.registerActivityLifecycleCallbacks(new Application.ActivityLifecycleCallbacks() {
            @Override public void onActivityCreated(Activity activity, Bundle savedInstanceState) {}
            @Override public void onActivityStarted(Activity activity) {}
            @Override public void onActivityResumed(Activity activity) {
                currentActivity = activity;
                ensureOverlayAlive();
                mainHandler.removeCallbacks(recoverOverlay);
                mainHandler.postDelayed(recoverOverlay, 2500);
            }
            @Override public void onActivityPaused(Activity activity) {
                hideOverlay(activity);
            }
            @Override public void onActivityStopped(Activity activity) {}
            @Override public void onActivitySaveInstanceState(Activity activity, Bundle outState) {}
            @Override public void onActivityDestroyed(Activity activity) {
                hideOverlay(activity);
            }
        });
    }

    private void ensureOverlayAlive() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            mainHandler.post(this::ensureOverlayAlive);
            return;
        }
        Activity target = currentActivity;
        if (target == null) return;

        ViewGroup root = (ViewGroup) target.getWindow().getDecorView();
        if (attachedActivity != target || tagView == null || tagView.getParent() != root) {
            detachOverlay();
            showOverlay(target);
            return;
        }
        if (panelView != null && panelView.getParent() != root) {
            closePanel();
            if (panelWanted) openPanel();
        }
        int count = root.getChildCount();
        if (panelView == null) {
            if (root.getChildAt(count - 1) != tagView) tagView.bringToFront();
        } else if (root.getChildAt(count - 1) != panelView || root.getChildAt(count - 2) != tagView) {
            tagView.bringToFront();
            panelView.bringToFront();
        }
    }

    private void showOverlay(Activity target) {
        try {
            attachedActivity = target;
            overlayRoot = (ViewGroup) target.getWindow().getDecorView();
            Configuration config = target.getResources().getConfiguration();
            densityDpi = config.densityDpi;
            fontScale = config.fontScale;
            tagView = buildTagView();
            tagDrag = new OverlayDrag(tagView, tagView, bounds, true, bubblePosition);
            updateTagAppearance();
            tagView.setOnApplyWindowInsetsListener((view, insets) -> {
                syncGeometry();
                return insets;
            });
            attachChild(tagView, dp(48), dp(48));
            overlayRoot.getViewTreeObserver().addOnGlobalLayoutListener(windowLayout);
            syncGeometry();
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.i(TemplateConfig.LOG_TAG, "Overlay attached to " + target.getClass().getName());
            }
        } catch (Throwable t) {
            detachOverlay();
            if (TemplateConfig.VERBOSE_LOGS) Log.e(TemplateConfig.LOG_TAG, "Overlay attach failed", t);
        }
    }

    private TextView buildTagView() {
        TextView t = new TextView(attachedActivity);
        t.setTextColor(Color.WHITE);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        t.setGravity(Gravity.CENTER);
        t.setTypeface(t.getTypeface(), Typeface.BOLD);
        GradientDrawable bg = new GradientDrawable();
        bg.setShape(GradientDrawable.OVAL);
        bg.setColor(Color.argb(0xCC, 0x3A, 0x2D, 0x6B));
        bg.setStroke(dp(1), Color.argb(0xFF, 0xB8, 0x9A, 0xFF));
        t.setBackground(bg);
        t.setFocusable(true);
        t.setOnClickListener(v -> {
            if (hidden) {
                hidden = false;
                updateTagAppearance();
            }
            togglePanel();
        });
        return t;
    }

    private void updateTagAppearance() {
        tagView.setText(hidden ? "" : TemplateConfig.MENU_BUBBLE_TEXT);
        tagView.getBackground().setAlpha(hidden ? 0 : 255);
        tagView.setDefaultFocusHighlightEnabled(!hidden);
        tagView.setContentDescription(hidden ? "Show menu" : null);
        tagDrag.setMovementEnabled(!hidden);
    }

    private void togglePanel() {
        panelWanted = panelView == null;
        if (panelWanted) openPanel();
        else closePanel();
    }

    private void openPanel() {
        if (hidden || panelView != null || overlayRoot == null || bounds.isEmpty()) return;
        panelView = buildPanelView();
        panelDrag = new OverlayDrag(panelDragHandle, panelView, bounds, false, panelPosition);
        attachChild(panelView, panelWidth(), panelSize(bounds.height(), 600));
        features.restoreScroll(firstFeature, firstOffset);
    }

    private void hideMenu() {
        hidden = true;
        panelWanted = false;
        closePanel();
        updateTagAppearance();
    }

    // Labels/status come from the consuming module; this in-app panel uses the target Context.
    @android.annotation.SuppressLint("SetTextI18n")
    private View buildPanelView() {

        LinearLayout root = new LinearLayout(attachedActivity);
        root.setOrientation(LinearLayout.VERTICAL);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(Color.argb(0xE0, 0x0E, 0x0E, 0x14));
        bg.setCornerRadius(dp(16));
        bg.setStroke(dp(1), Color.argb(0xFF, 0x6B, 0x5B, 0xFF));
        root.setBackground(bg);

        LinearLayout heading = new LinearLayout(attachedActivity);
        heading.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout header = new LinearLayout(attachedActivity);
        header.setOrientation(LinearLayout.VERTICAL);
        header.setMinimumHeight(dp(48));
        header.setGravity(Gravity.CENTER_VERTICAL);
        panelDragHandle = header;

        TextView title = new TextView(attachedActivity);
        panelTitle = title;
        title.setText(TemplateConfig.MENU_TITLE);
        title.setMaxLines(2);
        title.setEllipsize(TextUtils.TruncateAt.END);
        title.setTextColor(Color.WHITE);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        title.setTypeface(title.getTypeface(), Typeface.BOLD);
        header.addView(title);

        TextView sub = new TextView(attachedActivity);
        panelSubtitle = sub;
        sub.setText(TemplateConfig.MENU_SUBTITLE);
        sub.setSingleLine(true);
        sub.setEllipsize(TextUtils.TruncateAt.END);
        sub.setTextColor(Color.argb(0xFF, 0xB8, 0x9A, 0xFF));
        sub.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        sub.setTypeface(sub.getTypeface(), Typeface.BOLD);
        header.addView(sub);
        heading.addView(header, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        Button close = nyxButton(attachedActivity, "X");
        close.setContentDescription("Close menu");
        close.setOnClickListener(v -> togglePanel());
        heading.addView(close, new LinearLayout.LayoutParams(dp(48), LinearLayout.LayoutParams.WRAP_CONTENT));
        root.addView(heading);

        View hairline = new View(attachedActivity);
        LinearLayout.LayoutParams hp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(1));
        hp.bottomMargin = dp(8);
        hairline.setBackgroundColor(Color.argb(0x55, 0xB8, 0x9A, 0xFF));
        root.addView(hairline, hp);

        ListView list = new ListView(attachedActivity);
        list.setDivider(null);
        list.setSelector(android.R.color.transparent);
        list.setCacheColorHint(Color.TRANSPARENT);
        list.setItemsCanFocus(true);
        list.setScrollBarStyle(View.SCROLLBARS_OUTSIDE_OVERLAY);
        list.setFastScrollEnabled(true);
        final TextView stats = new TextView(attachedActivity);
        stats.setTextColor(Color.parseColor("#9DA5B4"));
        stats.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        stats.setPadding(0, dp(12), 0, dp(10));
        stats.setText(FeatureState.summary());
        panelStats = stats;
        list.addFooterView(stats, null, false);
        features = new OverlayFeatures(list, this::editNumber);
        list.setAdapter(features);
        features.setColumns(columnsFor(panelWidth()));
        features.showFeatures(FeatureRegistry.features());

        /* Optional tabs for a consuming module. Uncomment to group the same list by control type.
        android.widget.RadioGroup tabs = new android.widget.RadioGroup(attachedActivity);
        tabs.setOrientation(LinearLayout.HORIZONTAL);
        for (String name : new String[]{"All", "Toggles", "Numbers"}) {
            android.widget.RadioButton tab = new android.widget.RadioButton(attachedActivity);
            tab.setId(View.generateViewId());
            tab.setText(name);
            tab.setTextColor(Color.WHITE);
            tabs.addView(tab, new android.widget.RadioGroup.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
            if (name.equals("All")) tabs.check(tab.getId());
            tab.setOnClickListener(v -> {
                java.util.List<FeatureRegistry.Feature> items = FeatureRegistry.features();
                if (!name.equals("All")) items.removeIf(f ->
                        (f.type == FeatureRegistry.Type.BOOL) != name.equals("Toggles"));
                features.showFeatures(items);
            });
        }
        root.addView(tabs);
        */
        root.addView(list, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));

        LinearLayout row = new LinearLayout(attachedActivity);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setLayoutDirection(View.LAYOUT_DIRECTION_LTR); // The hide control stays at the physical bottom-left.
        row.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams rowLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        rowLp.topMargin = dp(4);
        row.setLayoutParams(rowLp);

        TextView hide = new TextView(attachedActivity);
        hide.setAllCaps(false);
        hide.setText("hide");
        hide.setTextColor(0xFFB89AFF);
        hide.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        hide.setGravity(Gravity.CENTER);
        hide.setMinWidth(dp(48));
        hide.setMinHeight(dp(48));
        hide.setFocusable(true);
        hide.setTooltipText("Tap the bubble's last position to reopen the menu");
        hide.setOnClickListener(v -> hideMenu());
        row.addView(hide);

        Button reset = nyxButton(attachedActivity, "Reset");
        reset.setOnClickListener(v -> {
            FeatureRegistry.resetToDefaults();
            Toast.makeText(attachedActivity, "Defaults restored", Toast.LENGTH_SHORT).show();
        });

        Button nativeRetry = nyxButton(attachedActivity, "Status");
        nativeRetry.setOnClickListener(v -> {
            stats.setText(FeatureState.summary() + "\n" + NativeBridge.getNativeRecords());
            features.showStatus();
        });

        LinearLayout.LayoutParams flex = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        flex.setMargins(0, 0, dp(4), 0);
        reset.setLayoutParams(flex);
        nativeRetry.setLayoutParams(new LinearLayout.LayoutParams(flex));
        row.addView(reset);
        row.addView(nativeRetry);
        root.addView(row);

        panelListener = new PanelUpdates(root);
        FeatureRegistry.addListener(panelListener);
        spacePanel(root, panelWidth(), panelSize(bounds.height(), 600));
        return root;
    }

    @android.annotation.SuppressLint("SetTextI18n")
    private void editNumber(FeatureRegistry.Feature feature) {
        Activity activity = currentActivity != null ? currentActivity : attachedActivity;
        if (activity == null || activity.isFinishing()) return;
        EditText input = new EditText(activity);
        input.setSingleLine(true);
        input.setInputType(android.text.InputType.TYPE_CLASS_NUMBER | android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL
                | android.text.InputType.TYPE_NUMBER_FLAG_SIGNED);
        input.setText(Float.toString(FeatureRegistry.getFloat(feature.key)));
        input.selectAll();
        AlertDialog editor = new AlertDialog.Builder(activity).setTitle(feature.label).setView(input)
                .setNegativeButton("Cancel", null).setPositiveButton("Apply", (dialog, which) -> {
                    try {
                        float value = Float.parseFloat(input.getText().toString().trim());
                        if (!Float.isFinite(value)) throw new NumberFormatException();
                        FeatureRegistry.setFloat(feature.key, value);
                    } catch (NumberFormatException error) {
                        Toast.makeText(activity, "Enter a finite number", Toast.LENGTH_SHORT).show();
                    }
                }).create();
        inputDialog = editor;
        editor.setOnDismissListener(dialog -> { if (inputDialog == editor) inputDialog = null; });
        editor.show();
        input.requestFocus();
        input.selectAll();
        if (editor.getWindow() != null) editor.getWindow().setSoftInputMode(
                android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
    }

    private final class PanelUpdates implements FeatureRegistry.Listener, Runnable {
        private final View panel;
        private boolean statsDirty;
        private boolean scheduled, closed;

        PanelUpdates(View panel) { this.panel = panel; }

        @Override public synchronized void onChanged(String key) {
            if (closed) return;
            statsDirty |= FeatureRegistry.KEY_ENABLED.equals(key) || FeatureRegistry.KEY_MULTIPLIER.equals(key)
                    || FeatureRegistry.KEY_NATIVE_HOOKS.equals(key);
            if (!scheduled) {
                scheduled = true;
                panel.postOnAnimation(this);
            }
        }

        @Override public synchronized void run() {
            if (closed) return;
            scheduled = false;
            features.refreshVisible();
            if (statsDirty) {
                statsDirty = false;
                String summary = FeatureState.summary();
                if (!TextUtils.equals(panelStats.getText(), summary)) panelStats.setText(summary);
            }
        }

        synchronized void close() {
            closed = true;
            panel.removeCallbacks(this);
        }
    }

    private void closePanel() {
        if (panelListener != null) {
            FeatureRegistry.removeListener(panelListener);
            panelListener.close();
            panelListener = null;
        }
        if (inputDialog != null) { inputDialog.dismiss(); inputDialog = null; }
        if (features != null) {
            firstFeature = features.firstFeature();
            firstOffset = features.firstOffset();
            features = null;
        }
        if (panelView != null) FeatureRegistry.commit();
        detachFromParent(panelView);
        panelView = null;
        panelDrag = null;
        panelDragHandle = null;
        panelStats = null;
        panelTitle = null;
        panelSubtitle = null;
    }

    static Button nyxButton(Context context, String text) {
        Button b = new Button(context);
        b.setText(text);
        b.setAllCaps(true);
        b.setTextColor(Color.WHITE);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        b.setMaxLines(2);
        b.setIncludeFontPadding(false);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(Color.argb(0xFF, 0x1A, 0x1A, 0x24));
        bg.setCornerRadius(dp(context, 8));
        bg.setStroke(dp(context, 1), Color.argb(0xFF, 0x3A, 0x35, 0x50));
        b.setBackground(bg);
        b.setMinimumHeight(dp(context, 48));
        b.setMinHeight(dp(context, 48));
        b.setMinimumWidth(0);
        b.setMinWidth(0);
        b.setPadding(dp(context, 10), dp(context, 6), dp(context, 10), dp(context, 6));
        b.setStateListAnimator(null);
        b.setElevation(0f);
        return b;
    }

    private int panelSize(int available, int capDp) {
        int gutter = Math.min(dp(12), available / 20);
        return Math.min(dp(capDp), available - 2 * gutter);
    }

    private int columnsFor(int width) {
        float labelSize = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, 13,
                attachedActivity.getResources().getDisplayMetrics());
        int minCell = dp(220) + Math.round(4 * Math.max(0f, labelSize - dp(13)));
        int inner = width - 2 * Math.min(dp(18), width / 18);
        return inner >= 2 * minCell + dp(12) ? 2 : 1;
    }

    private int panelWidth() {
        int wide = panelSize(bounds.width(), 680);
        return bounds.width() > bounds.height() || columnsFor(wide) == 2 ? wide : panelSize(bounds.width(), 380);
    }

    private void spacePanel(View panel, int width, int height) {
        int x = Math.min(dp(18), width / 18);
        int y = Math.min(dp(18), height / 30);
        panel.setPadding(x, y, x, y);
        // Keep a numeric row and the footer reachable before spending extra height on the heading.
        int headingBudget = height - 2 * y - dp(48 + 4 + 9 + 96);
        int titleLine = panelTitle.getLineHeight(), subtitleLine = panelSubtitle.getLineHeight();
        int lines = headingBudget < 2 * titleLine + subtitleLine ? 1 : 2;
        if (panelTitle.getMaxLines() != lines) panelTitle.setMaxLines(lines);
        panelSubtitle.setVisibility(headingBudget < titleLine + subtitleLine ? View.GONE : View.VISIBLE);
    }

    private static void resize(View child, int width, int height) {
        if (child == null || child.getLayoutParams() == null) return;
        ViewGroup.LayoutParams lp = child.getLayoutParams();
        if (lp.width == width && lp.height == height) return;
        lp.width = width;
        lp.height = height;
        child.setLayoutParams(lp);
    }

    @SuppressWarnings("deprecation") // API 28–29 expose system window and cutout insets separately.
    private void syncGeometry() {
        ViewGroup root = overlayRoot;
        if (root == null || root.getWidth() == 0 || root.getHeight() == 0) return;
        Configuration config = attachedActivity.getResources().getConfiguration();
        if (densityDpi != config.densityDpi || fontScale != config.fontScale) {
            // Some hosts handle configuration changes without recreating their Activity.
            if (!scaleUpdatePending) {
                scaleUpdatePending = true;
                mainHandler.post(scaleUpdate);
            }
            return;
        }
        int left = root.getPaddingLeft(), top = root.getPaddingTop();
        if (features != null) features.fitScrollGutter();
        int right = root.getPaddingRight(), bottom = root.getPaddingBottom();
        WindowInsets insets = root.getRootWindowInsets();
        if (insets != null) {
            if (Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets i = insets.getInsets(WindowInsets.Type.systemBars()
                        | WindowInsets.Type.displayCutout() | WindowInsets.Type.ime());
                left = Math.max(left, i.left); top = Math.max(top, i.top);
                right = Math.max(right, i.right); bottom = Math.max(bottom, i.bottom);
            } else {
                left = Math.max(left, insets.getSystemWindowInsetLeft());
                top = Math.max(top, insets.getSystemWindowInsetTop());
                right = Math.max(right, insets.getSystemWindowInsetRight());
                bottom = Math.max(bottom, insets.getSystemWindowInsetBottom());
                DisplayCutout c = insets.getDisplayCutout();
                if (c != null) {
                    left = Math.max(left, c.getSafeInsetLeft()); top = Math.max(top, c.getSafeInsetTop());
                    right = Math.max(right, c.getSafeInsetRight()); bottom = Math.max(bottom, c.getSafeInsetBottom());
                }
            }
        }
        right = Math.max(left, root.getWidth() - right);
        bottom = Math.max(top, root.getHeight() - bottom);
        if (bounds.left == left && bounds.top == top && bounds.right == right && bounds.bottom == bottom) return;
        bounds.set(left, top, right, bottom);
        int tagSize = Math.min(dp(48), Math.min(bounds.width(), bounds.height()));
        resize(tagView, tagSize, tagSize);
        if (tagDrag != null) tagDrag.place();
        if (panelView != null) {
            int width = panelWidth(), height = panelSize(bounds.height(), 600);
            features.setColumns(columnsFor(width));
            spacePanel(panelView, width, height);
            resize(panelView, width, height);
            panelDrag.place();
        } else if (panelWanted && !hidden) {
            openPanel();
        }
    }

    private void attachChild(View child, int width, int height) {
        // Keep the overlay above the host without rasterizing a 128dp elevation shadow.
        child.setOutlineProvider(null);
        child.setZ(dp(128));
        overlayRoot.addView(child, new FrameLayout.LayoutParams(width, height, Gravity.TOP | Gravity.START));
    }

    private void rebuildForScaleChange() {
        Activity target = attachedActivity;
        detachOverlay();
        if (target != null && target == currentActivity) showOverlay(target);
    }

    private void hideOverlay(Activity activity) {
        if (currentActivity == activity) {
            currentActivity = null;
            mainHandler.removeCallbacks(recoverOverlay);
        }
        if (attachedActivity == activity) detachOverlay();
    }

    private void detachOverlay() {
        mainHandler.removeCallbacks(scaleUpdate);
        scaleUpdatePending = false;
        if (overlayRoot != null && overlayRoot.getViewTreeObserver().isAlive()) {
            overlayRoot.getViewTreeObserver().removeOnGlobalLayoutListener(windowLayout);
        }
        closePanel();
        detachFromParent(tagView);
        tagView = null;
        tagDrag = null;
        overlayRoot = null;
        attachedActivity = null;
        bounds.setEmpty();
    }

    private static void detachFromParent(View view) {
        if (view == null) return;
        if (view.getParent() instanceof ViewGroup) {
            ((ViewGroup) view.getParent()).removeView(view);
        }
    }

    static int dp(Context context, float value) {
        return Math.round(value * context.getResources().getDisplayMetrics().density);
    }

    private int dp(float value) { return dp(attachedActivity, value); }
}
