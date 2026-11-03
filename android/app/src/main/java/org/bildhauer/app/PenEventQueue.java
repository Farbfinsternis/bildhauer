package org.bildhauer.app;

import java.util.ArrayDeque;

/**
 * FIFO between Android input and the render thread. Contact boundaries
 * (down/up/cancel/hover exit) are always kept. Hover keeps only its latest
 * position, and contact motion merges into the newest queued motion once the
 * backlog reaches {@link #MOTION_BACKLOG}, so a slow brush cannot fall
 * further and further behind the pen.
 */
final class PenEventQueue {
    static final int DOWN=0, MOVE=1, UP=2, CANCEL=3, HOVER=4, HOVER_EXIT=5;
    static final int MOTION_BACKLOG=16;

    static final class Sample {
        final int action;
        float x, y, pressure;
        final boolean button;
        Sample(int action, float x, float y, float pressure, boolean button) {
            this.action=action; this.x=x; this.y=y; this.pressure=pressure; this.button=button;
        }
    }
    private final ArrayDeque<Sample> samples=new ArrayDeque<>();
    synchronized void add(int action,float x,float y,float pressure,boolean button) {
        Sample last=samples.peekLast();
        boolean merge=last!=null && last.action==action && last.button==button
                && (action==HOVER || (action==MOVE && samples.size()>=MOTION_BACKLOG));
        if (merge) { last.x=x; last.y=y; last.pressure=pressure; }
        else samples.addLast(new Sample(action,x,y,pressure,button));
    }
    synchronized Sample poll() { return samples.pollFirst(); }
    synchronized boolean isEmpty() { return samples.isEmpty(); }
}
