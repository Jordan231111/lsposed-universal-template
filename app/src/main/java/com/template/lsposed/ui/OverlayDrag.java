package com.template.lsposed.ui;

import android.graphics.Rect;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;

/** Moves one decor child within the usable window, independently of the host's layout pass. */
final class OverlayDrag implements View.OnTouchListener {
    private final View child;
    private final Rect bounds;
    private final boolean bubble;
    private final int slop;
    static final class Position { float x = Float.NaN, y = Float.NaN; }
    private final Position position;
    private int pointer = -1;
    private float downX, downY, startX, startY;
    private boolean dragging, tap;
    private boolean movementEnabled = true;

    OverlayDrag(View handle, View child, Rect bounds, boolean bubble, Position position) {
        this.child = child;
        this.bounds = bounds;
        this.bubble = bubble;
        this.position = position;
        slop = ViewConfiguration.get(handle.getContext()).getScaledTouchSlop();
        handle.setOnTouchListener(this);
        child.addOnLayoutChangeListener((v, l, t, r, b, oldL, oldT, oldR, oldB) -> place());
    }

    void setMovementEnabled(boolean enabled) { movementEnabled = enabled; }

    void place() {
        if (bounds.isEmpty() || child.getWidth() == 0 || child.getHeight() == 0) return;
        int spanX = Math.max(0, bounds.width() - child.getWidth());
        int spanY = Math.max(0, bounds.height() - child.getHeight());
        float x = spanX * 0.5f, y = spanY * 0.5f;
        if (bubble) {
            float density = child.getResources().getDisplayMetrics().density;
            x = Math.min(spanX, Math.round(12 * density));
            if (child.getLayoutDirection() == View.LAYOUT_DIRECTION_RTL) x = spanX - x;
            y = Math.min(spanY, Math.round(120 * density));
        }
        child.setX(bounds.left + (Float.isNaN(position.x) ? x : position.x * spanX));
        child.setY(bounds.top + (Float.isNaN(position.y) ? y : position.y * spanY));
    }

    // Local pointer differences plus the raw origin also work on API 28, which lacks getRawX(index).
    private static float rawX(MotionEvent e, int i) {
        return i == 0 ? e.getRawX() : e.getRawX() + (e.getX(i) - e.getX());
    }
    private static float rawY(MotionEvent e, int i) {
        return i == 0 ? e.getRawY() : e.getRawY() + (e.getY(i) - e.getY());
    }

    private void grab(MotionEvent e, int i) {
        pointer = e.getPointerId(i);
        downX = rawX(e, i);
        downY = rawY(e, i);
        startX = child.getX();
        startY = child.getY();
    }

    private void move(MotionEvent e) {
        int i = e.findPointerIndex(pointer);
        if (i < 0 || bounds.isEmpty()) return;
        float dx = rawX(e, i) - downX, dy = rawY(e, i) - downY;
        if (!dragging && dx * dx + dy * dy <= slop * slop) return;
        dragging = true;
        tap = false;
        if (!movementEnabled) return;
        int spanX = Math.max(0, bounds.width() - child.getWidth());
        int spanY = Math.max(0, bounds.height() - child.getHeight());
        float x = Math.max(0, Math.min(spanX, startX + dx - bounds.left));
        float y = Math.max(0, Math.min(spanY, startY + dy - bounds.top));
        if (spanX > 0) position.x = x / spanX;
        if (spanY > 0) position.y = y / spanY;
        child.setX(bounds.left + x);
        child.setY(bounds.top + y);
    }

    @Override public boolean onTouch(View handle, MotionEvent e) {
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                grab(e, 0);
                dragging = false;
                tap = bubble;
                break;
            case MotionEvent.ACTION_MOVE:
                move(e);
                break;
            case MotionEvent.ACTION_POINTER_DOWN:
                tap = false;
                break;
            case MotionEvent.ACTION_POINTER_UP:
                if (e.getPointerId(e.getActionIndex()) == pointer) {
                    grab(e, e.getActionIndex() == 0 ? 1 : 0);
                }
                break;
            case MotionEvent.ACTION_UP:
                move(e);
                if (pointer != -1 && tap) handle.performClick();
                pointer = -1;
                break;
            case MotionEvent.ACTION_CANCEL:
                pointer = -1;
                tap = false;
                break;
            default:
                break;
        }
        return true;
    }
}
