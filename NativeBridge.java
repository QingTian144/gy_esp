package com.gy.esp;

import android.view.Surface;

/**
 * native 桥接。
 *
 * 注意：库名 gyesp 与 JNI 符号前缀必须一致。
 * 若要改包名，需同步修改 main.cpp 里所有
 * Java_com_gy_esp_NativeBridge_* 符号，或改用 RegisterNatives 动态注册。
 */
public final class NativeBridge {

    static {
        System.loadLibrary("gyesp");
    }

    private NativeBridge() {}

    /** 传入 Surface 与屏幕尺寸，内部起渲染线程。 */
    public static native void nativeStart(Surface surface, int width, int height);

    /** 停止并释放 EGL / Surface。 */
    public static native void nativeStop();

    public static native boolean nativeIsRunning();

    /** 当前状态文本，便于界面显示。 */
    public static native String nativeStatus();

    /** 偏移自动发现的日志，初始化失败时用来排查。 */
    public static native String nativeDumpLog();

    /** 绘制开关位图，取值见 esp.h 的 DrawFlag。 */
    public static native void nativeSetFlags(int flags);
}
