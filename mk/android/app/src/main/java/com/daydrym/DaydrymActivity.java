package com.daydrym;

import android.app.Activity;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;
import com.google.vr.ndk.base.AndroidCompat;
import com.google.vr.ndk.base.GvrLayout;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Hosts a GvrLayout (Daydream / Cardboard compositor). All rendering happens
 * in native code (gvr_app.cpp) on the GLSurfaceView's GL thread.
 */
public class DaydrymActivity extends Activity {
    static {
        System.loadLibrary("gvr");
        System.loadLibrary("daydrym");
    }

    private GvrLayout gvrLayout;
    private GLSurfaceView surfaceView;

    private native void nativeInit(long gvrContext);
    private native void nativeDestroy();
    private native void nativeOnResume();
    private native void nativeOnPause();
    private native void nativeOnSurfaceCreated();
    private native void nativeOnDrawFrame();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setImmersive();

        gvrLayout = new GvrLayout(this);

        surfaceView = new GLSurfaceView(this);
        surfaceView.setEGLContextClientVersion(3);
        surfaceView.setEGLConfigChooser(8, 8, 8, 8, 0, 0);
        surfaceView.setPreserveEGLContextOnPause(true);
        surfaceView.setRenderer(new GLSurfaceView.Renderer() {
            @Override
            public void onSurfaceCreated(GL10 gl, EGLConfig config) {
                nativeOnSurfaceCreated();
            }

            @Override
            public void onSurfaceChanged(GL10 gl, int width, int height) {
            }

            @Override
            public void onDrawFrame(GL10 gl) {
                nativeOnDrawFrame();
            }
        });
        gvrLayout.setPresentationView(surfaceView);

        // Daydream-ready devices: async reprojection needs sustained performance.
        if (gvrLayout.setAsyncReprojectionEnabled(true)) {
            AndroidCompat.setSustainedPerformanceMode(this, true);
        }
        AndroidCompat.setVrModeEnabled(this, true);

        setContentView(gvrLayout);
        nativeInit(gvrLayout.getGvrApi().getNativeGvrContext());
    }

    @Override
    protected void onResume() {
        super.onResume();
        gvrLayout.onResume();
        surfaceView.onResume();
        nativeOnResume();
    }

    @Override
    protected void onPause() {
        nativeOnPause();
        surfaceView.onPause();
        gvrLayout.onPause();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        gvrLayout.shutdown();
        nativeDestroy();
        super.onDestroy();
    }

    @Override
    public void onBackPressed() {
        gvrLayout.onBackPressed();
        super.onBackPressed();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            setImmersive();
        }
    }

    private void setImmersive() {
        getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_FULLSCREEN
        );
    }
}
