package org.bildhauer.app;

import java.util.ArrayList;
import java.util.List;

/** Runs on the desktop JVM, without Android or injected device events. */
public final class FingerGestureTest {
    private static final List<float[]> samples=new ArrayList<>();
    private static final FingerGesture fingers=new FingerGesture((action,x,y,pressure,button) ->
            samples.add(new float[]{action,x,y,pressure}));

    private static void check(boolean value,String message) {
        if(!value) throw new AssertionError(message);
    }
    private static void expect(String message,int... actions) {
        check(samples.size()==actions.length,message+": got "+samples.size()+" samples");
        for(int i=0;i<actions.length;++i) check(samples.get(i)[0]==actions[i],message+": sample "+i);
    }
    public static void main(String[] args) {
        // One finger is a pen contact at full pressure, history included.
        fingers.down(3,10,20);
        fingers.moved(3,11,20); fingers.moved(3,12,20); fingers.endOfEvent();
        fingers.lifted(3);
        expect("one finger",PenEventQueue.DOWN,PenEventQueue.MOVE,PenEventQueue.MOVE,PenEventQueue.UP);
        check(samples.get(0)[3]==1 && samples.get(2)[1]==12 && samples.get(3)[1]==12,"pressure and position");
        samples.clear();

        // A second finger reports center and distance, after both are current.
        fingers.down(0,0,0);
        fingers.added(1,100,0);
        fingers.moved(0,0,0); fingers.moved(1,60,80); fingers.endOfEvent();
        expect("pinch",PenEventQueue.DOWN,PenEventQueue.PINCH_START,PenEventQueue.MOVE,PenEventQueue.PINCH);
        float[] start=samples.get(1), pinch=samples.get(3);
        check(start[1]==50 && start[2]==0 && start[3]==100,"pinch start center and span");
        check(pinch[1]==30 && pinch[2]==40 && pinch[3]==100,"pinch center and span");
        samples.clear();

        // A third finger changes nothing; lifting the second ends the pinch
        // while the first one stays down, and it can pinch again.
        fingers.added(2,500,500);
        fingers.moved(2,501,500); fingers.endOfEvent();
        expect("third finger",PenEventQueue.PINCH);
        samples.clear();
        fingers.lifted(2);
        fingers.lifted(1);
        fingers.moved(0,1,1); fingers.endOfEvent();
        fingers.added(1,50,50);
        expect("second lifted",PenEventQueue.PINCH_END,PenEventQueue.MOVE,PenEventQueue.PINCH_START);
        samples.clear();

        // Lifting the first ends the contact; the finger left behind stays inert.
        fingers.lifted(0);
        fingers.moved(1,60,60); fingers.endOfEvent();
        fingers.added(4,70,70);
        fingers.lifted(1);
        fingers.cancel();
        expect("first lifted",PenEventQueue.UP);
        samples.clear();

        // A cancelled contact ends once; its remaining fingers are ignored.
        fingers.down(0,5,6);
        fingers.added(1,7,8);
        samples.clear();
        fingers.cancel();
        fingers.cancel();
        fingers.moved(0,9,9); fingers.endOfEvent();
        fingers.lifted(0); fingers.lifted(1);
        expect("cancel",PenEventQueue.CANCEL);
        check(samples.get(0)[1]==5 && samples.get(0)[2]==6,"cancel position");
        samples.clear();

        // Pinch keeps its latest position in the queue, without merging its boundaries.
        PenEventQueue q=new PenEventQueue();
        q.add(PenEventQueue.PINCH_START,0,0,100,false);
        for(int i=1;i<=50;++i) q.add(PenEventQueue.PINCH,i,i,100+i,false);
        q.add(PenEventQueue.PINCH_END,50,50,0,false);
        check(q.poll().action==PenEventQueue.PINCH_START,"pinch start stays");
        PenEventQueue.Sample merged=q.poll();
        check(merged.action==PenEventQueue.PINCH && merged.x==50 && merged.pressure==150,"pinch merges to the latest");
        check(q.poll().action==PenEventQueue.PINCH_END && q.poll()==null,"pinch end stays");

        System.out.println("PASS: finger contact, pinch, extra fingers, lifting order, cancel and pinch merging");
    }
}
