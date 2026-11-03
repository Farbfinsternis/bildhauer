package org.bildhauer.app;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.graphics.Bitmap;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.ViewGroup;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

// No test framework dependencies. Runs real stylus events through Android,
// the GL-thread queue, picking, sculpting, undo and SC1/OBJ serialization.
public final class SmokeInstrumentation extends Instrumentation {
    private GLSurfaceView view;
    private Method nativeFile;


    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments); start(); }

    @Override public void onStart() {
        Bundle result = new Bundle();
        File before = new File(getTargetContext().getCacheDir(),"smoke-before.sc1");
        try {
            Activity activity = startActivitySync(new Intent().setClassName(getTargetContext(),
                    "org.bildhauer.app.MainActivity").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            runOnMainSync(() -> view=(GLSurfaceView)((ViewGroup)activity.findViewById(android.R.id.content)).getChildAt(0));
            nativeFile=MainActivity.class.getDeclaredMethod("nativeFile",int.class,String.class);
            nativeFile.setAccessible(true);
            SystemClock.sleep(1200);
            file(2,before);
            byte[] original=Files.readAllBytes(before.toPath());
            float width=view.getWidth(),height=view.getHeight();
            float x=width*0.59f,y=height*0.47f;
            long down=SystemClock.uptimeMillis();
            event(down,MotionEvent.ACTION_DOWN,x,y,0.2f,MotionEvent.TOOL_TYPE_STYLUS);
            for (int i=1;i<=24;++i) {
                SystemClock.sleep(20);
                event(down,MotionEvent.ACTION_MOVE,x+i*width*0.0035f,y+i*height*0.002f,
                        0.2f+0.7f*i/24,MotionEvent.TOOL_TYPE_STYLUS);
            }
            event(down,MotionEvent.ACTION_UP,x+24*width*0.0035f,y+24*height*0.002f,0,MotionEvent.TOOL_TYPE_STYLUS);
            SystemClock.sleep(800);
            File after=new File(getTargetContext().getCacheDir(),"smoke-after.sc1");
            file(2,after);
            if (Arrays.equals(original,Files.readAllBytes(after.toPath()))) throw new AssertionError("Stylus did not change the mesh");
            File recovery=new File(getTargetContext().getFilesDir(),"recovery.sc1");
            if (!recovery.isFile() || recovery.length()==0) throw new AssertionError("Stroke recovery was not saved");
            Bitmap screenshot=getUiAutomation().takeScreenshot();
            try(FileOutputStream out=new FileOutputStream(new File(getTargetContext().getExternalFilesDir(null),"smoke-stroke.png"))) {
                screenshot.compress(Bitmap.CompressFormat.PNG,100,out);
            }
            screenshot.recycle();
            float density=getTargetContext().getResources().getDisplayMetrics().density;
            down=SystemClock.uptimeMillis();
            event(down,MotionEvent.ACTION_DOWN,60*density,107*density,1,MotionEvent.TOOL_TYPE_FINGER);
            SystemClock.sleep(80);
            event(down,MotionEvent.ACTION_UP,60*density,107*density,0,MotionEvent.TOOL_TYPE_FINGER);
            SystemClock.sleep(500);
            File undone=new File(getTargetContext().getCacheDir(),"smoke-undone.sc1");
            file(2,undone);
            if (!Arrays.equals(original,Files.readAllBytes(undone.toPath()))) throw new AssertionError("Undo did not restore the exact original SC1");

            File obj=new File(getTargetContext().getCacheDir(),"smoke.obj");
            file(3,obj);
            if (obj.length()==0) throw new AssertionError("OBJ export is empty");
            file(1,before);
            result.putString("stream","PASS: stylus, pressure, recovery, exact SC1 undo and OBJ export\n");
            finish(Activity.RESULT_OK,result);
        } catch(Throwable e) {
            try { if (nativeFile!=null && before.isFile()) file(1,before); } catch(Throwable ignored) { }
            result.putString("stream","FAIL: "+e+"\n");
            finish(Activity.RESULT_CANCELED,result);
        }
    }

    private void file(int action,File file) throws Exception {
        CountDownLatch done=new CountDownLatch(1);
        AtomicReference<Throwable> failure=new AtomicReference<>();
        view.queueEvent(() -> {
            try {

                String error=(String)nativeFile.invoke(null,action,file.getAbsolutePath());
                if (!error.isEmpty()) throw new AssertionError(error);
            } catch(Throwable e) { failure.set(e); }
            finally { done.countDown(); }
        });
        if (!done.await(10,TimeUnit.SECONDS)) throw new AssertionError("Render thread timed out");
        if (failure.get()!=null) throw new AssertionError(failure.get());
    }

    private void event(long down,int action,float x,float y,float pressure,int tool) {
        // UiAutomation takes screen coordinates; MotionEvent handling uses
        // view-local coordinates, including when system bars are visible.
        int[] location=new int[2];
        runOnMainSync(() -> view.getLocationOnScreen(location));
        x+=location[0]; y+=location[1];
        MotionEvent.PointerProperties p=new MotionEvent.PointerProperties(); p.id=0; p.toolType=tool;
        MotionEvent.PointerCoords c=new MotionEvent.PointerCoords(); c.x=x; c.y=y; c.pressure=pressure; c.size=0.01f;
        int source=tool==MotionEvent.TOOL_TYPE_STYLUS ? InputDevice.SOURCE_STYLUS : InputDevice.SOURCE_TOUCHSCREEN;
        MotionEvent e=MotionEvent.obtain(down,SystemClock.uptimeMillis(),action,1,
                new MotionEvent.PointerProperties[]{p},new MotionEvent.PointerCoords[]{c},0,0,1,1,0,0,source,0);
        boolean injected=getUiAutomation().injectInputEvent(e,true); e.recycle();
        if (!injected) throw new AssertionError("Cannot inject pointer event");
    }

}
