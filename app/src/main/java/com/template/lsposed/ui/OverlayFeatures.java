package com.template.lsposed.ui;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.SeekBar;
import android.widget.Space;
import android.widget.TextView;

import com.template.lsposed.FeatureRegistry;
import com.template.lsposed.FeatureRegistry.Feature;

import java.util.Collections;
import java.util.List;
import java.util.function.Consumer;

import static android.view.ViewGroup.LayoutParams.MATCH_PARENT;
import static android.view.ViewGroup.LayoutParams.WRAP_CONTENT;

/** One or two controls per recycled list row. Each row measures its tallest control. */
final class OverlayFeatures extends BaseAdapter {
    private final ListView list;
    private final Context context;
    private final Consumer<Feature> editNumber;
    private List<Feature> features = Collections.emptyList();
    private int columns = 1;

    OverlayFeatures(ListView list, Consumer<Feature> editNumber) {
        this.list = list;
        context = list.getContext();
        this.editNumber = editNumber;
    }

    void showFeatures(List<Feature> items) {
        features = items;
        notifyDataSetChanged();
        list.setSelection(0);
    }

    void setColumns(int count) {
        if (columns == count) return;
        int anchor = firstFeature(), offset = firstOffset();
        columns = count;
        notifyDataSetChanged();
        restoreScroll(anchor, offset);
    }

    int firstFeature() { return list.getFirstVisiblePosition() * columns; }
    int firstOffset() { return list.getChildCount() == 0 ? 0 : list.getChildAt(0).getTop() - list.getPaddingTop(); }
    void restoreScroll(int feature, int offset) {
        list.setSelectionFromTop(Math.min(feature / columns, getCount()), offset);
    }
    void showStatus() { list.setSelection(getCount()); } // Status is the list footer.

    void fitScrollGutter() {
        // Even the shortest rows fill at least two viewports. Keep the thumb discoverable.
        int height = list.getHeight();
        boolean keepThumb = height > 0 && (long) getCount() * dp(48) >= 2L * height;
        if (list.isFastScrollAlwaysVisible() != keepThumb) list.setFastScrollAlwaysVisible(keepThumb);
        // The native fast scroller intercepts a touch target wider than its visible thumb.
        int gutter = list.isFastScrollEnabled() ? Math.max(dp(48), list.getVerticalScrollbarWidth()) : 0;
        if (list.getPaddingEnd() != gutter) list.setPaddingRelative(0, 0, gutter, 0);
    }

    @Override public int getCount() { return (features.size() + columns - 1) / columns; }
    @Override public Feature getItem(int row) { return features.get(row * columns); }
    @Override public long getItemId(int row) { return row; }
    @Override public int getViewTypeCount() { return 8; }

    private static int kind(Feature feature) {
        return feature.type == FeatureRegistry.Type.BOOL ? 0 : 1;
    }

    @Override public int getItemViewType(int row) {
        int first = kind(getItem(row));
        if (columns == 1) return first;
        int next = row * columns + 1;
        if (next == features.size()) return 2 + first;
        return 4 + 2 * first + kind(features.get(next));
    }

    @Override public View getView(int row, View recycled, ViewGroup parent) {
        Feature first = getItem(row);
        int next = row * columns + 1;
        Feature second = columns == 2 && next < features.size() ? features.get(next) : null;
        LinearLayout line = (LinearLayout) recycled;
        if (line == null) {
            line = new LinearLayout(context);
            line.setOrientation(LinearLayout.HORIZONTAL);
            line.addView(cell(first), new LinearLayout.LayoutParams(0, WRAP_CONTENT, 1));
            if (columns == 2) {
                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, WRAP_CONTENT, 1);
                lp.setMarginStart(dp(12));
                line.addView(second == null ? new Space(context) : cell(second), lp);
            }
        }
        ((Cell) line.getChildAt(0)).bind(first);
        if (second != null) ((Cell) line.getChildAt(1)).bind(second);
        return line;
    }

    void refreshVisible() {
        for (int i = 0; i < list.getChildCount(); ++i) {
            View child = list.getChildAt(i);
            if (!(child instanceof LinearLayout line)) continue;
            for (int j = 0; j < line.getChildCount(); ++j)
                if (line.getChildAt(j) instanceof Cell cell) cell.refresh();
        }
    }

    private int dp(float value) { return OverlayController.dp(context, value); }
    private Cell cell(Feature f) { return kind(f) == 0 ? new ToggleCell() : new NumberCell(); }

    private abstract class Cell extends LinearLayout {
        Feature feature;
        final TextView label;
        Cell() {
            super(context);
            label = new TextView(context);
            label.setTextColor(Color.WHITE);
            label.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
            label.setGravity(Gravity.CENTER_VERTICAL);
        }
        abstract void bind(Feature f);
        abstract void refresh();
    }

    private final class ToggleCell extends Cell {
        final Button toggle = OverlayController.nyxButton(context, "");
        ToggleCell() {
            setGravity(Gravity.CENTER_VERTICAL);
            setPadding(0, dp(2), 0, dp(2));
            addView(label, new LinearLayout.LayoutParams(0, WRAP_CONTENT, 1));
            toggle.setMinWidth(dp(60));
            addView(toggle, new LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT));
            toggle.setOnClickListener(v -> FeatureRegistry.setBool(feature.key, !FeatureRegistry.getBool(feature.key)));
        }
        @Override void bind(Feature f) {
            if (feature != f) { feature = f; label.setText(f.label); }
            refresh();
        }
        @Override void refresh() {
            String text = FeatureRegistry.getBool(feature.key) ? "ON" : "OFF";
            if (!TextUtils.equals(toggle.getText(), text)) toggle.setText(text);
        }
    }

    @android.annotation.SuppressLint("SetTextI18n")
    private final class NumberCell extends Cell {
        final SeekBar bar = new SeekBar(context);
        final Button minus = OverlayController.nyxButton(context, "−"), plus = OverlayController.nyxButton(context, "+");
        double step;
        float displayed = Float.NaN;
        NumberCell() {
            setOrientation(VERTICAL);
            label.setMinHeight(dp(48));
            label.setFocusable(true);
            label.setOnClickListener(v -> editNumber.accept(feature));
            addView(label, new LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT));
            LinearLayout controls = new LinearLayout(context);
            controls.setGravity(Gravity.CENTER_VERTICAL);
            controls.addView(minus, new LinearLayout.LayoutParams(dp(48), WRAP_CONTENT));
            controls.addView(bar, new LinearLayout.LayoutParams(0, dp(48), 1));
            controls.addView(plus, new LinearLayout.LayoutParams(dp(48), WRAP_CONTENT));
            addView(controls);
            ColorStateList tint = ColorStateList.valueOf(0xFFB89AFF);
            bar.setProgressTintList(tint);
            bar.setThumbTintList(tint);
            bar.setProgressBackgroundTintList(ColorStateList.valueOf(0xFF3A3550));
            minus.setOnClickListener(v -> FeatureRegistry.setFloat(feature.key, (float) (FeatureRegistry.getFloat(feature.key) - step)));
            plus.setOnClickListener(v -> FeatureRegistry.setFloat(feature.key, (float) (FeatureRegistry.getFloat(feature.key) + step)));
            bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                    if (fromUser) FeatureRegistry.previewFloat(feature.key, progress == bar.getMax() ? feature.max
                            : (float) (feature.min + progress * step));
                }
                @Override public void onStartTrackingTouch(SeekBar seekBar) {}
                @Override public void onStopTrackingTouch(SeekBar seekBar) { FeatureRegistry.commit(); }
            });
        }
        @Override void bind(Feature f) {
            if (feature != f) {
                feature = f;
                step = f.step > 0 ? f.step : ((double) f.max - f.min) / 1000;
                bar.setMax((int) Math.ceil(((double) f.max - f.min) / step));
                minus.setContentDescription("Decrease " + f.label);
                plus.setContentDescription("Increase " + f.label);
                displayed = Float.NaN;
            }
            refresh();
        }
        @Override void refresh() {
            float value = FeatureRegistry.getFloat(feature.key);
            if (Float.compare(value, displayed) == 0) return;
            displayed = value;
            String text = feature.label + ": " + Float.toString(value);
            label.setText(text);
            label.setContentDescription("Edit " + text);
            bar.setContentDescription(text);
            int progress = value == feature.max ? bar.getMax() : (int) Math.round(((double) value - feature.min) / step);
            if (bar.getProgress() != progress) bar.setProgress(progress);
        }
    }
}
