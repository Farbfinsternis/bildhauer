package org.bildhauer.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.net.Uri;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.DisplayCutout;
import android.view.MotionEvent;

import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.util.Log;
import android.widget.Toast;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;


import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public final class MainActivity extends Activity {
    static { System.loadLibrary("bildhauer_android"); }
    private static native boolean nativeInit(String directory, float density, boolean restore);
    private static native void nativeResize(int width, int height);
    private static native void nativeInsets(int left, int top, int right, int bottom);
    private static native int nativeFrame();
    private static native void nativePointer(int action, float x, float y, float pressure, boolean button);

    private static native void nativePause();
    private static native String nativeFile(int action, String filename);
    private static native void nativeBench(String filename);

    private SculptView view;
    private final ExecutorService files = Executors.newSingleThreadExecutor();
    private boolean pickerOpen;
    // Saved state exists only when Android recreates a killed activity.
    private boolean restoreSession;
    private File exportSnapshot;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        restoreSession = state != null;
        // The view fills the screen around a camera hole; the UI keeps clear of it.
        getWindow().getAttributes().layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        hideStatusBar();
        view = new SculptView();
        setContentView(view);
    }

    // Only the status bar goes: without hardware keys, the navigation bar is
    // the way out of the app. The UI keeps clear of it (onApplyWindowInsets).
    private void hideStatusBar() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    @Override protected void onResume() { super.onResume(); view.onResume(); }

    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);

        if (focused) hideStatusBar();
    }

    @Override protected void onPause() {
        // All native state belongs to the GL thread, including recovery saves.

        CountDownLatch saved = new CountDownLatch(1);
        view.queueEvent(() -> { if (view.ready) { view.drainPen(false); nativePause(); } saved.countDown(); });
        try { saved.await(3, TimeUnit.SECONDS); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
        view.onPause();
        super.onPause();
    }

    @Override protected void onDestroy() { files.shutdown(); super.onDestroy(); }

    private void toast(String text) { runOnUiThread(() -> Toast.makeText(this, text, Toast.LENGTH_LONG).show()); }

    private void chooseFile(int action) {
        if (pickerOpen || isFinishing()) return;
        pickerOpen = true;
        if (action == 1) {
            new AlertDialog.Builder(this).setTitle("Open model")
                .setMessage("Opening a model replaces this session. Save a copy first if you want to keep it.")
                .setPositiveButton("Open", (dialog, which) -> launchPicker(action))
                .setNegativeButton("Cancel", (dialog, which) -> pickerOpen = false)
                .setOnCancelListener(dialog -> pickerOpen = false).show();
        } else {
            // Freeze the export before the system picker pauses the GL surface.
            exportSnapshot = new File(getCacheDir(), action == 2 ? "export.sc1" : "export.obj");
            final File snapshot = exportSnapshot;
            view.queueEvent(() -> {
                view.drainPen(false);
                String error = nativeFile(action, snapshot.getAbsolutePath());
                runOnUiThread(() -> {
                    if (error.isEmpty()) launchPicker(action);
                    else { pickerOpen = false; toast(error); }
                });
            });
            view.wake();
        }
    }

    private void launchPicker(int action) {
        Intent intent = new Intent(action == 1 ? Intent.ACTION_OPEN_DOCUMENT : Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType(action == 1 ? "*/*" : "application/octet-stream");
        if (action != 1) intent.putExtra(Intent.EXTRA_TITLE, action == 2 ? "bildhauer.sc1" : "bildhauer.obj");
        try { startActivityForResult(intent, action); }
        catch (android.content.ActivityNotFoundException e) { pickerOpen = false; toast("No document picker is available"); }
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        pickerOpen = false;
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        final File snapshot = exportSnapshot;
        files.execute(() -> {
            try {
                if (request == 1) {
                    File input = new File(getCacheDir(), "import.sc1");
                    try (InputStream in = getContentResolver().openInputStream(uri); OutputStream out = new FileOutputStream(input)) {
                        copy(in, out, 64L * 1024 * 1024);
                    }
                    view.queueEvent(() -> {
                        view.drainPen(false);
                        String error = nativeFile(1, input.getAbsolutePath());
                        toast(error.isEmpty() ? "Model opened" : error);
                    });
                    view.wake();
                } else {
                    try (InputStream in = new FileInputStream(snapshot); OutputStream out = getContentResolver().openOutputStream(uri, "wt")) {
                        copy(in, out, Long.MAX_VALUE);
                    }
                    toast("File saved");
                }
            } catch (Exception e) { toast("File operation failed: " + e.getMessage()); }
        });
    }

    private static void copy(InputStream in, OutputStream out, long limit) throws java.io.IOException {
        if (in == null || out == null) throw new java.io.IOException("Cannot access the document");
        byte[] buffer = new byte[65536];
        long total = 0;
        for (int n; (n = in.read(buffer)) != -1;) {
            total += n;
            if (total > limit) throw new java.io.IOException("This prototype accepts files up to 64 MiB");
            out.write(buffer, 0, n);
        }
    }

    private final class SculptView extends GLSurfaceView implements GLSurfaceView.Renderer {
        boolean ready;
        private int penId = -1;
        private long lastPen;
        // tools/perf-bench.ps1 starts debug builds with this extra.
        private boolean benchPending = (getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0
                && getIntent().getBooleanExtra("bench", false);
        // Frames continue this long after input so the UI can settle.
        private volatile long activeUntil;
        private final float density = getResources().getDisplayMetrics().density;
        private final PenEventQueue penEvents=new PenEventQueue();
        private final FingerGesture fingers=new FingerGesture(this::pointer);
        SculptView() {
            super(MainActivity.this);
            setEGLContextClientVersion(3);
            setEGLConfigChooser(8,8,8,8,24,0);
            setPreserveEGLContextOnPause(true);
            setRenderer(this);
            // Draw on input and while native work is pending, not continuously:
            // an idle tablet should not stay at full CPU/GPU load.
            setRenderMode(RENDERMODE_WHEN_DIRTY);
        }
        void wake() {
            activeUntil=SystemClock.uptimeMillis()+500;
            requestRender();
        }
        @Override public void onSurfaceCreated(GL10 gl, EGLConfig config) {
            ready = nativeInit(getFilesDir().getAbsolutePath(), density, restoreSession);
            if (!ready) toast("Graphics initialization failed; see the bildhauer log");
        }
        @Override public void onSurfaceChanged(GL10 gl, int width, int height) { nativeResize(width,height); }
        @Override public WindowInsets onApplyWindowInsets(WindowInsets insets) {
            final DisplayCutout cutout=insets.getDisplayCutout();
            final int left=Math.max(insets.getSystemWindowInsetLeft(),cutout==null ? 0 : cutout.getSafeInsetLeft());
            final int top=Math.max(insets.getSystemWindowInsetTop(),cutout==null ? 0 : cutout.getSafeInsetTop());
            final int right=Math.max(insets.getSystemWindowInsetRight(),cutout==null ? 0 : cutout.getSafeInsetRight());
            final int bottom=Math.max(insets.getSystemWindowInsetBottom(),cutout==null ? 0 : cutout.getSafeInsetBottom());
            queueEvent(() -> nativeInsets(left,top,right,bottom));
            wake();
            return super.onApplyWindowInsets(insets);
        }
        @Override public void onDrawFrame(GL10 gl) {
            if (!ready) return;
            drainPen(true);
            int result = nativeFrame();
            if (benchPending) {
                benchPending = false;
                nativeBench(new File(getFilesDir(), "bench.sc1").getAbsolutePath());
            }
            int action = result & 0xff;
            if (action != 0) runOnUiThread(() -> chooseFile(action));
            if ((result & 0x100) != 0 || !penEvents.isEmpty() || SystemClock.uptimeMillis() < activeUntil) requestRender();
        }
        private void pointer(int action, float x, float y, float pressure, boolean button) {
            penEvents.add(action,x,y,pressure,button);
            wake();
        }
        private void drainPen(boolean frameBudget) {
            final long start=SystemClock.uptimeMillis();
            PenEventQueue.Sample sample;
            while ((sample=penEvents.poll())!=null) {
                nativePointer(sample.action,sample.x,sample.y,sample.pressure,sample.button);
                // Preserve every sample, but let GLSurfaceView draw/swap
                // instead of draining an arbitrarily long queueEvent backlog.
                if (frameBudget && SystemClock.uptimeMillis()-start>=8) break;
            }
        }
        @Override public boolean onTouchEvent(MotionEvent event) {
            final int action = event.getActionMasked();
            int pen = -1;
            for (int i=0;i<event.getPointerCount();++i) {
                int tool=event.getToolType(i);
                if (tool==MotionEvent.TOOL_TYPE_STYLUS || tool==MotionEvent.TOOL_TYPE_ERASER) { pen=i; break; }
            }

            if (pen >= 0) {
                lastPen=SystemClock.uptimeMillis();
                int id=event.getPointerId(pen);
                boolean button=(event.getButtonState() & MotionEvent.BUTTON_STYLUS_PRIMARY)!=0
                        || event.getToolType(pen)==MotionEvent.TOOL_TYPE_ERASER;
                if (action!=MotionEvent.ACTION_MOVE)
                    Log.i("bildhauer","Pen contact action="+action+" id="+id+" pressure="+event.getPressure(pen));
                if ((action==MotionEvent.ACTION_DOWN || action==MotionEvent.ACTION_POINTER_DOWN) && event.getActionIndex()==pen) {
                    fingers.cancel(); penId=id;
                    pointer(0,event.getX(pen),event.getY(pen),event.getPressure(pen),button);
                } else if (action==MotionEvent.ACTION_MOVE && penId==id) {
                    for (int h=0;h<event.getHistorySize();++h)
                        pointer(1,event.getHistoricalX(pen,h),event.getHistoricalY(pen,h),event.getHistoricalPressure(pen,h),button);
                    pointer(1,event.getX(pen),event.getY(pen),event.getPressure(pen),button);
                } else if (action==MotionEvent.ACTION_CANCEL ||
                        ((action==MotionEvent.ACTION_UP || action==MotionEvent.ACTION_POINTER_UP) && event.getActionIndex()==pen)) {
                    pointer(action==MotionEvent.ACTION_CANCEL ? 3 : 2,event.getX(pen),event.getY(pen),event.getPressure(pen),button);
                    penId=-1;
                }
                return true; // Finger/palm events never sculpt while a pen is present.
            }
            if (action==MotionEvent.ACTION_CANCEL) {
                fingers.cancel();
                if (penId!=-1) { pointer(3,0,0,0,false); penId=-1; }
                return true;
            }
            // A nearby pen means the hand is resting on the glass, not sculpting.
            if (penId!=-1 || SystemClock.uptimeMillis()-lastPen<400) { fingers.cancel(); return true; }
            final int index=event.getActionIndex();
            if (action==MotionEvent.ACTION_DOWN) {
                fingers.down(event.getPointerId(index),event.getX(index),event.getY(index));
            } else if (action==MotionEvent.ACTION_POINTER_DOWN) {
                fingers.added(event.getPointerId(index),event.getX(index),event.getY(index));
            } else if (action==MotionEvent.ACTION_UP || action==MotionEvent.ACTION_POINTER_UP) {
                fingers.lifted(event.getPointerId(index));
            } else if (action==MotionEvent.ACTION_MOVE) {
                final int count=event.getPointerCount();
                for (int h=0;h<event.getHistorySize();++h)
                    for (int i=0;i<count;++i)
                        fingers.moved(event.getPointerId(i),event.getHistoricalX(i,h),event.getHistoricalY(i,h));
                for (int i=0;i<count;++i) fingers.moved(event.getPointerId(i),event.getX(i),event.getY(i));
                fingers.endOfEvent();
            }
            return true;
        }
        @Override public boolean onGenericMotionEvent(MotionEvent event) {
            if (event.getToolType(0)==MotionEvent.TOOL_TYPE_STYLUS) {
                if (event.getActionMasked()==MotionEvent.ACTION_HOVER_EXIT) {
                    pointer(5,event.getX(),event.getY(),0,false);
                } else if (event.getActionMasked()==MotionEvent.ACTION_HOVER_ENTER
                        || event.getActionMasked()==MotionEvent.ACTION_HOVER_MOVE) {
                    lastPen=SystemClock.uptimeMillis();
                    pointer(4,event.getX(),event.getY(),0,false);
                }
                return true;
            }
            return super.onGenericMotionEvent(event);
        }
    }
}
