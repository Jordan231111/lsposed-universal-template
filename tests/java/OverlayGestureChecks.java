package com.template.lsposed.ui;

import android.graphics.Rect;
import android.view.MotionEvent;
import android.view.View;
import static android.view.MotionEvent.*;

public class OverlayGestureChecks {
    private static void touch(View v, int action, int id, float x, float y) {
        v.listener.onTouch(v, new MotionEvent(action, 0, new int[]{id}, new float[]{x}, new float[]{y}));
    }
    private static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }
    private static void at(View v, float x, float y) {
        check(Math.abs(v.getX()-x)<0.01 && Math.abs(v.getY()-y)<0.01,
                "Expected " + x + "," + y + " but got " + v.getX() + "," + v.getY());
    }
    public static void main(String[] args) {
        Rect safe = new Rect(20, 30, 320, 430);
        View bubble = new View();
        OverlayDrag drag = new OverlayDrag(bubble, bubble, safe, true, new OverlayDrag.Position());
        drag.place();
        at(bubble, 32, 150);
        touch(bubble,ACTION_DOWN,7,10,10);
        touch(bubble,ACTION_MOVE,7,14,12);
        touch(bubble,ACTION_UP,7,14,12);
        at(bubble,32,150);
        check(bubble.clicks==1,"Tap with jitter must click exactly once");
        touch(bubble,ACTION_DOWN,7,10,10);
        touch(bubble,ACTION_CANCEL,7,10,10);
        touch(bubble,ACTION_DOWN,7,10,10);
        touch(bubble,ACTION_UP,7,500,1000);
        at(bubble,272,382);
        check(bubble.clicks==1,"Cancellation and a distant release must not click");
        drag.setMovementEnabled(false);
        touch(bubble,ACTION_DOWN,7,100,100);
        touch(bubble,ACTION_UP,7,5,9);
        at(bubble,272,382);
        check(bubble.clicks==1,"A swipe on the hidden bubble must not move or open it");
        touch(bubble,ACTION_DOWN,7,15,15);
        touch(bubble,ACTION_UP,7,15,15);
        check(bubble.clicks==2,"A tap on the hidden bubble must still work");
        safe.right=180; safe.bottom=220;
        drag.place();
        at(bubble,132,172);

        View header=new View(), panel=new View();
        panel.width=80; panel.height=100;
        OverlayDrag panelDrag=new OverlayDrag(header,panel,safe,false,new OverlayDrag.Position());
        panelDrag.place();
        at(panel,60,75);
        touch(header,ACTION_DOWN,1,100,100);
        touch(header,ACTION_MOVE,1,112,115);
        at(panel,72,90);
        int[] ids={1,9}; float[] xs={112,180}, ys={115,190};
        header.listener.onTouch(header,new MotionEvent(ACTION_POINTER_DOWN,1,ids,xs,ys));
        header.listener.onTouch(header,new MotionEvent(ACTION_POINTER_UP,0,ids,xs,ys));
        touch(header,ACTION_MOVE,9,185,187);
        at(panel,77,87);
        touch(header,ACTION_UP,9,185,187);
        check(panel.clicks==0 && header.clicks==0,"Header drag must not click");
        View rtl=new View(); rtl.direction=View.LAYOUT_DIRECTION_RTL;
        new OverlayDrag(rtl,rtl,safe,true,new OverlayDrag.Position()).place();
        at(rtl,120,150);
    }
}
