// main.cpp —— JNI 入口与主渲染循环
#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>

#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include "mem.h"
#include "kdrv.h"
#include "esp.h"
#include "overlay.h"

using namespace gy;

#define LOG_TAG "gy"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ================================================================
// 全局状态
// ================================================================

static std::atomic<bool>  g_running{false};
static std::atomic<bool>  g_stopReq{false};
static std::thread        g_worker;
static std::mutex         g_mutex;

static ANativeWindow*     g_window = nullptr;
static int                g_width  = 0;
static int                g_height = 0;
static std::string        g_status = "idle";

static Esp                g_esp;

// 游戏包名（国服和平精英）
static const char* kGamePackage = "com.tencent.tmgp.pubgmhd";

// ================================================================
// 绘制
// ================================================================

static void drawPlayer(Overlay& ov, const PlayerInfo& p, const EspConfig& cfg) {
    if (!p.onScreen) return;

    float h = p.screenFeet.y - p.screenHead.y;
    if (h < 4.f || h > 3000.f) return;

    // 人形宽高比约 0.55
    float w = h * 0.55f;
    float x = p.screenFeet.x - w * 0.5f;
    float y = p.screenHead.y;

    // 基础色：敌人红 / 机器人黄
    Color base = Color::rgb(cfg.colEnemy[0], cfg.colEnemy[1], cfg.colEnemy[2]);

    // ---- 方框 ----
    if (cfg.flags & D_Box) {
        ov.rect(x, y, w, h, base, cfg.boxThickness);
    }

    // ---- 血条 ----
    if (cfg.flags & D_Health) {
        float barW = 3.f;
        float barH = h * 0.9f;
        float barX = x - barW - 4.f;
        float barY = y + h * 0.05f;

        float pct = 1.f;
        if (p.healthMax > 0.f && p.health > 0.f)
            pct = p.health / p.healthMax;

        ov.healthBar(barX, barY, barH, pct,
                     Color::rgb(cfg.colHealth[0], cfg.colHealth[1], cfg.colHealth[2]),
                     Color::rgb(40, 40, 40, 220),
                     barW);

        // 血量数字
        if (p.health > 0.f) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.0f", p.health);
            ov.text(barX - 26.f, barY + barH * 0.5f - 4.f, buf,
                    Color::rgb(cfg.colText[0], cfg.colText[1],
                               cfg.colText[2]), 1.0f);
        }
    }

    // ---- 距离 ----
    if (cfg.flags & D_Distance) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0fm", p.distance);
        ov.textCentered(p.screenFeet.x, y + h + 4.f, buf,
                        Color::rgb(cfg.colText[0], cfg.colText[1],
                                   cfg.colText[2]), 1.0f);
    }

    // ---- 骨骼连线（仅展示骨架主线，需要骨骼偏移可用） ----
    if ((cfg.flags & D_Skeleton) && p.bones.size() >= 2 && p.bones.size() == p.boneValid.size()) {
        Color sc = Color::rgb(cfg.colSkeleton[0], cfg.colSkeleton[1],
                              cfg.colSkeleton[2]);
        // bones 数组内部自行成对： (a,b) 表示一条连线
        for (size_t i = 0; i + 1 < p.bones.size(); i += 2) {
            if (!p.boneValid[i] || !p.boneValid[i + 1]) continue;
            ov.line(p.bones[i].x, p.bones[i].y,
                    p.bones[i + 1].x, p.bones[i + 1].y, sc, 1.2f);
        }
    }
}

static void drawHud(Overlay& ov, const Esp& esp, int count) {
    Color c = Color::rgb(0, 255, 120);

    char buf[128];
    snprintf(buf, sizeof(buf), "PLAYERS %d", count);
    ov.text(20.f, 40.f, buf, c, 1.4f);

    if (!esp.hasCamera()) {
        ov.text(20.f, 70.f, "CAMERA LOST", Color::rgb(255, 80, 80), 1.2f);
    }
}

// ================================================================
// 渲染线程
// ================================================================

static void renderThread() {
    LOGI("render thread start");

    ProcessMemory mem;
    Kdrv drv;

    bool useKdrv = drv.open();
    if (useKdrv) {
        LOGI("kernel driver active, version=0x%08x", drv.version());
        g_status = "kdrv ok";
    } else {
        LOGI("kernel driver unavailable, falling back to process_vm_readv");
    }

    if (!mem.attach(kGamePackage)) {
        LOGE("attach %s failed", kGamePackage);
        g_status = "attach failed";
        g_running = false;
        return;
    }
    g_status = "attached";

    if (!g_esp.init(mem, (uint32_t)mem.pid())) {
        LOGE("esp init failed: %s", g_esp.lastError().c_str());
        g_status = "esp init failed: " + g_esp.lastError();
        g_running = false;
        return;
    }
    g_status = "ready";

    Overlay ov;
    if (!ov.init(g_window, g_width, g_height)) {
        LOGE("overlay init failed");
        g_status = "overlay init failed";
        g_running = false;
        return;
    }

    float fpsTimer = 0.f;
    int   frames   = 0;

    while (g_running && !g_stopReq) {
        g_esp.update(g_width, g_height);

        ov.beginFrame();

        auto& cfg = g_esp.config();
        for (const auto& p : g_esp.players())
            drawPlayer(ov, p, cfg);

        drawHud(ov, g_esp, (int)g_esp.players().size());

        ov.endFrame();

        ++frames;
        fpsTimer += 0.016f;
        if (fpsTimer >= 1.f) {
            LOGI("fps=%d players=%zu", frames, g_esp.players().size());
            frames = 0;
            fpsTimer = 0.f;
        }

        // 约 60fps 上限，不占满 CPU
        usleep(16000);
    }

    ov.shutdown();
    mem.detach();
    drv.close();

    LOGI("render thread exit");
    g_status = "stopped";
}

// ================================================================
// JNI
// ================================================================

extern "C" {

JNIEXPORT void JNICALL
Java_com_gy_esp_NativeBridge_nativeStart(JNIEnv* env, jclass, jobject surface,
                                        jint width, jint height) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_running) {
        LOGI("already running");
        return;
    }

    if (!surface) {
        LOGE("surface is null");
        return;
    }

    g_window = ANativeWindow_fromSurface(env, surface);
    if (!g_window) {
        LOGE("ANativeWindow_fromSurface failed");
        return;
    }

    g_width  = width;
    g_height = height;
    g_stopReq = false;
    g_running = true;
    g_status  = "starting";

    if (g_worker.joinable()) g_worker.join();
    g_worker = std::thread(renderThread);
}

JNIEXPORT void JNICALL
Java_com_gy_esp_NativeBridge_nativeStop(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(g_mutex);

    g_stopReq = true;

    if (g_worker.joinable()) {
        // 等渲染线程自己退出，最多 2 秒
        for (int i = 0; i < 200 && g_running; ++i)
            usleep(10000);
        g_worker.join();
    }

    g_running = false;

    if (g_window) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }

    g_status = "stopped";
}

JNIEXPORT jboolean JNICALL
Java_com_gy_esp_NativeBridge_nativeIsRunning(JNIEnv*, jclass) {
    return g_running ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_gy_esp_NativeBridge_nativeStatus(JNIEnv* env, jclass) {
    return env->NewStringUTF(g_status.c_str());
}

JNIEXPORT void JNICALL
Java_com_gy_esp_NativeBridge_nativeSetFlags(JNIEnv*, jclass, jint flags) {
    // 由 Java 层调用，写入全局配置；实际生效在 ESP 实例里
    g_esp.config().flags = (uint32_t)flags;
}

JNIEXPORT jstring JNICALL
Java_com_gy_esp_NativeBridge_nativeDumpLog(JNIEnv* env, jclass) {
    return env->NewStringUTF(g_esp.dumpResult().log.c_str());
}

} // extern "C"
