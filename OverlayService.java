package com.gy.esp;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.IBinder;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;

/**
 * 透明悬浮窗 + SurfaceView，native 用 EGL 在其 Surface 上直接绘制。
 *
 * 关键点：
 *  1. 窗口 FLAG_NOT_TOUCHABLE —— 触摸完全穿透到下层游戏
 *  2. SurfaceView.setZOrderOnTop(true) —— Surface 盖在窗口内容之上
 *  3. PixelFormat.TRANSLUCENT + native 侧 glClearColor(0,0,0,0) —— 背景全透明
 */
public class OverlayService extends Service {

    private static final String TAG = "gy";
    private static final String CHANNEL_ID = "sysmon_ch";
    private static final int    NOTI_ID = 0x4759;

    private WindowManager wm;
    private SurfaceView   surfaceView;
    private WindowManager.LayoutParams lp;

    private int screenW = 0;
    private int screenH = 0;
    private boolean attached = false;

    @Override
    public void onCreate() {
        super.onCreate();
        wm = (WindowManager) getSystemService(WINDOW_SERVICE);

        DisplayMetrics dm = getResources().getDisplayMetrics();
        screenW = dm.widthPixels;
        screenH = dm.heightPixels;

        Log.i(TAG, "service created, screen=" + screenW + "x" + screenH);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        startForegroundCompat();
        showOverlay();
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        hideOverlay();
        NativeBridge.nativeStop();
        super.onDestroy();
        Log.i(TAG, "service destroyed");
    }

    // ---------------------------------------------------------------- 前台通知

    private void startForegroundCompat() {
        NotificationManager nm = null;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            nm = getSystemService(NotificationManager.class);
            NotificationChannel ch = new NotificationChannel(
                    CHANNEL_ID, "system monitor", NotificationManager.IMPORTANCE_MIN);
            ch.setShowBadge(false);
            ch.setSound(null, null);
            nm.createNotificationChannel(ch);
        }

        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            b = new Notification.Builder(this, CHANNEL_ID);
        } else {
            b = new Notification.Builder(this);
        }

        Notification n = b.setContentTitle("system monitor")
                .setSmallIcon(android.R.drawable.stat_notify_sync)
                .setOngoing(true)
                .build();

        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(NOTI_ID, n,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        } else {
            startForeground(NOTI_ID, n);
        }
    }

    // ---------------------------------------------------------------- 悬浮窗

    private void showOverlay() {
        if (attached) return;

        surfaceView = new SurfaceView(this);
        surfaceView.setZOrderOnTop(true);
        surfaceView.getHolder().setFormat(PixelFormat.TRANSLUCENT);

        int type = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                : WindowManager.LayoutParams.TYPE_PHONE;

        lp = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.MATCH_PARENT,
                WindowManager.LayoutParams.MATCH_PARENT,
                type,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                        | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                PixelFormat.TRANSLUCENT);

        lp.gravity = Gravity.TOP | Gravity.START;
        lp.x = 0;
        lp.y = 0;

        surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                Log.i(TAG, "surface created");
                NativeBridge.nativeStart(holder.getSurface(), screenW, screenH);
                Log.i(TAG, "native started, status=" + NativeBridge.nativeStatus());
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format,
                                       int width, int height) {
                Log.i(TAG, "surface changed " + width + "x" + height);
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                Log.i(TAG, "surface destroyed");
                NativeBridge.nativeStop();
            }
        });

        try {
            wm.addView(surfaceView, lp);
            attached = true;
        } catch (Throwable t) {
            Log.e(TAG, "addView failed: " + t);
        }
    }

    private void hideOverlay() {
        if (surfaceView != null && attached) {
            try {
                wm.removeView(surfaceView);
            } catch (Throwable ignored) {
            }
        }
        surfaceView = null;
        attached = false;
    }
}
