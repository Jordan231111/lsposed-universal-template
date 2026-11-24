package android.view;

import android.content.Context;
import android.content.res.Resources;

public class View {
    public interface OnTouchListener { boolean onTouch(View view, MotionEvent event); }
    public interface OnLayoutChangeListener {
        void onLayoutChange(View view, int l, int t, int r, int b, int ol, int ot, int or, int ob);
    }
    public static final int LAYOUT_DIRECTION_RTL = 1;
    public int width = 48, height = 48, clicks, direction;
    private float x, y;
    private final Resources resources = new Resources();
    public OnTouchListener listener;
    public Context getContext() { return new Context(); }
    public Resources getResources() { return resources; }
    public void setOnTouchListener(OnTouchListener value) { listener = value; }
    public void addOnLayoutChangeListener(OnLayoutChangeListener value) {}
    public int getWidth() { return width; }
    public int getHeight() { return height; }
    public float getX() { return x; }
    public float getY() { return y; }
    public void setX(float value) { x = value; }
    public void setY(float value) { y = value; }
    public int getLayoutDirection() { return direction; }
    public boolean performClick() { clicks++; return true; }
}
