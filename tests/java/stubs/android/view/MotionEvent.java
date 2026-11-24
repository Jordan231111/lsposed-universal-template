package android.view;

public class MotionEvent {
    public static final int ACTION_DOWN=0, ACTION_UP=1, ACTION_MOVE=2, ACTION_CANCEL=3,
            ACTION_POINTER_DOWN=5, ACTION_POINTER_UP=6;
    private final int action, index;
    private final int[] ids;
    private final float[] xs, ys;
    public MotionEvent(int action, int index, int[] ids, float[] xs, float[] ys) {
        this.action=action; this.index=index; this.ids=ids; this.xs=xs; this.ys=ys;
    }
    public int getActionMasked() { return action; }
    public int getActionIndex() { return index; }
    public int getPointerId(int i) { return ids[i]; }
    public int findPointerIndex(int id) {
        for (int i=0; i<ids.length; i++) if (ids[i]==id) return i;
        return -1;
    }
    public float getX() { return xs[0]; }
    public float getY() { return ys[0]; }
    public float getX(int i) { return xs[i]; }
    public float getY(int i) { return ys[i]; }
    public float getRawX() { return xs[0] + 10; }
    public float getRawY() { return ys[0] + 30; }
}
