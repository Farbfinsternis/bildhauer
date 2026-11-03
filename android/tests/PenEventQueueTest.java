package org.bildhauer.app;

/** Runs on the desktop JVM, without Android or injected device events. */
public final class PenEventQueueTest {
    private static void check(boolean value,String message) {
        if(!value) throw new AssertionError(message);
    }
    public static void main(String[] args) throws Exception {
        final int backlog=PenEventQueue.MOTION_BACKLOG;
        PenEventQueue q=new PenEventQueue();
        // Motion is kept exactly until the backlog limit; later samples merge
        // into the newest one, which always carries the latest position.
        q.add(0,10,20,0.1f,false);
        for(int i=0;i<600;++i) q.add(1,i,(i%2)*40,i/600f,false);
        q.add(5,610,20,0,false); // hover exit stays distinct from cancellation
        q.add(2,610,20,0,false);
        q.add(0,20,30,0.2f,false);
        q.add(3,20,30,0,false);
        check(q.poll().action==0,"contact down comes first");
        for(int i=0;i<backlog-2;++i) {
            PenEventQueue.Sample s=q.poll();
            check(s!=null && s.action==1 && s.x==i && s.y==(i%2)*40 && s.pressure==i/600f,"lost/reordered motion sample "+i);
        }
        PenEventQueue.Sample merged=q.poll();
        check(merged.action==1 && merged.x==599 && merged.y==40 && merged.pressure==599/600f,"backlog ends at the latest motion");
        check(q.poll().action==5,"hover exit is retained independently");
        check(q.poll().action==2,"release follows the last motion");
        check(q.poll().action==0 && q.poll().action==3,"next contact/cancel stay separate");
        check(q.poll()==null,"queue drained completely");

        // Hover keeps only the latest position, but never swallows contact.
        for(int i=0;i<100;++i) q.add(4,i,i,0,false);
        q.add(0,5,5,0.5f,false);
        q.add(4,7,7,0,false);
        PenEventQueue.Sample hover=q.poll();
        check(hover.action==4 && hover.x==99,"hover coalesces to its latest position");
        check(q.poll().action==0 && q.poll().x==7 && q.poll()==null,"hover around contact keeps order");

        // A side-button change is a separate sample even in a full backlog.
        for(int i=0;i<backlog;++i) q.add(1,i,0,1,false);
        q.add(1,50,0,1,true);
        q.add(1,51,0,1,true);
        int count=0; PenEventQueue.Sample s, last=null;
        while((s=q.poll())!=null) { ++count; last=s; }
        check(count==backlog+1 && last.button && last.x==51,"button change keeps its own sample");

        Thread producer=new Thread(() -> { for(int i=0;i<10000;++i) q.add(1,i,-i,0.5f,false); });
        producer.start();
        float previous=-1;
        while(true) {
            s=q.poll();
            if(s==null) { if(!producer.isAlive() && q.isEmpty()) break; Thread.yield(); continue; }
            check(s.x>previous && s.y==-s.x,"cross-thread sample order "+s.x);
            previous=s.x;
        }
        producer.join();
        check(previous==9999,"last motion sample arrives");
        System.out.println("PASS: bounded pen FIFO, hover coalescing, contact boundaries and concurrent producer");
    }
}
