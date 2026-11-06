package org.bildhauer.app;

/**
 * Turns finger contacts into pointer samples. The first finger behaves like the
 * pen: the render thread, which owns the mesh, decides whether it hit the UI or
 * the model (a stroke) or landed beside it (the view orbits). Only a second
 * finger adds something: it reports a pinch, which the render thread uses or
 * ignores depending on what the first finger started. Further fingers, and
 * fingers still down after the first one lifted, are ignored.
 */
final class FingerGesture {
    interface Sink { void add(int action,float x,float y,float pressure,boolean button); }

    private final Sink out;
    private int first=-1, second=-1;
    private float firstX, firstY, secondX, secondY;

    FingerGesture(Sink out) { this.out=out; }

    void down(int id,float x,float y) {
        first=id; second=-1; firstX=x; firstY=y;
        out.add(PenEventQueue.DOWN,x,y,1,false);
    }
    void added(int id,float x,float y) {
        if (first<0 || second>=0) return;
        second=id; secondX=x; secondY=y;
        pinch(PenEventQueue.PINCH_START);
    }
    void moved(int id,float x,float y) {
        if (id==first) { firstX=x; firstY=y; out.add(PenEventQueue.MOVE,x,y,1,false); }
        else if (id==second) { secondX=x; secondY=y; }
    }
    /** Call after the moved() calls of one input event, once all fingers are current. */
    void endOfEvent() { if (second>=0) pinch(PenEventQueue.PINCH); }
    void lifted(int id) {
        if (id==first) {
            out.add(PenEventQueue.UP,firstX,firstY,1,false);
            first=second=-1;
        } else if (id==second) {
            second=-1;
            out.add(PenEventQueue.PINCH_END,firstX,firstY,0,false);
        }
    }
    /** Ends the contact, because Android cancelled it or a pen took over. */
    void cancel() {
        if (first<0) return;
        out.add(PenEventQueue.CANCEL,firstX,firstY,1,false);
        first=second=-1;
    }
    private void pinch(int action) {
        out.add(action,(firstX+secondX)*0.5f,(firstY+secondY)*0.5f,
                (float)Math.hypot(firstX-secondX,firstY-secondY),false);
    }
}
