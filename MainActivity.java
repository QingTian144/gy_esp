package com.gy.esp;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

/**
 * 控制面板：申请悬浮窗权限、启停服务、查看驱动与偏移状态。
 */
public class MainActivity extends Activity {

    private static final int REQ_OVERLAY = 0x1001;

    private TextView statusView;
    private TextView dumpView;
    private Button   startBtn;
    private Button   stopBtn;

    private final Handler ui = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(buildUi());
        refresh();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
        ui.postDelayed(new Runnable() {
            @Override public void run() {
                refresh();
                ui.postDelayed(this, 1000);
            }
        }, 1000);
    }

    // ---------------------------------------------------------------- UI

    private View buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        root.setBackgroundColor(Color.parseColor("#101418"));
        int pad = dp(24);
        root.setPadding(pad, pad * 2, pad, pad);

        TextView title = new TextView(this);
        title.setText("system monitor");
        title.setTextColor(Color.WHITE);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 22);
        title.setGravity(Gravity.CENTER);
        root.addView(title, matchWrap());

        statusView = new TextView(this);
        statusView.setTextColor(Color.parseColor("#7CFFB2"));
        statusView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        statusView.setPadding(0, dp(20), 0, dp(12));
        root.addView(statusView, matchWrap());

        startBtn = new Button(this);
        startBtn.setText("START");
        startBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                onStartClicked();
            }
        });
        root.addView(startBtn, matchWrap());

        stopBtn = new Button(this);
        stopBtn.setText("STOP");
        stopBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                if (!hasOverlayPermission()) {
                    toast("no overlay permission");
                    return;
                }
                stopService(new Intent(MainActivity.this, OverlayService.class));
                NativeBridge.nativeStop();
                refresh();
            }
        });
        root.addView(stopBtn, matchWrap());

        dumpView = new TextView(this);
        dumpView.setTextColor(Color.parseColor("#9AA7B4"));
        dumpView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        dumpView.setPadding(0, dp(20), 0, 0);
        dumpView.setTextIsSelectable(true);
        root.addView(dumpView, matchWrap());

        return root;
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
    }

    private int dp(int v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v,
                getResources().getDisplayMetrics());
    }

    // ---------------------------------------------------------------- 权限

    private boolean hasOverlayPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            return Settings.canDrawOverlays(this);
        }
        return true;
    }

    private void requestOverlayPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            Intent i = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                    Uri.parse("package:" + getPackageName()));
            startActivityForResult(i, REQ_OVERLAY);
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_OVERLAY) {
            refresh();
            if (hasOverlayPermission())
                toast("permission granted");
            else
                toast("permission denied");
        }
    }

    // ---------------------------------------------------------------- 动作

    private void onStartClicked() {
        if (!hasOverlayPermission()) {
            toast("grant overlay permission first");
            requestOverlayPermission();
            return;
        }

        try {
            startForegroundService(new Intent(this, OverlayService.class));
        } catch (Throwable t) {
            // Android 7 及以下没有这个入口
            startService(new Intent(this, OverlayService.class));
        }
        ui.postDelayed(new Runnable() {
            @Override public void run() { refresh(); }
        }, 800);
    }

    private void refresh() {
        if (statusView == null) return;

        boolean running = NativeBridge.nativeIsRunning();
        StringBuilder sb = new StringBuilder();
        sb.append("overlay permission : ").append(hasOverlayPermission() ? "yes" : "no").append('\n');
        sb.append("native running    : ").append(running ? "yes" : "no").append('\n');
        sb.append("status            : ").append(NativeBridge.nativeStatus());

        statusView.setText(sb.toString());

        startBtn.setEnabled(!running);
        stopBtn.setEnabled(running);

        String dump = NativeBridge.nativeDumpLog();
        if (dump != null && !dump.isEmpty()) {
            dumpView.setText(dump);
        }
    }

    private void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_SHORT).show();
    }
}
