// test_math.cpp —— W2S 数学验证（host 侧单元测试）
#include "ue4.h"

#include <cstdio>
#include <cmath>
#include <cstring>

using namespace gy::ue4;

static int g_pass = 0;
static int g_fail = 0;

static void expectNear(const char* name, float got, float want, float tol = 0.5f) {
    float d = fabsf(got - want);
    if (d <= tol) {
        printf("  [PASS] %-34s got=%9.3f want=%9.3f\n", name, got, want);
        ++g_pass;
    } else {
        printf("  [FAIL] %-34s got=%9.3f want=%9.3f  diff=%.3f\n",
               name, got, want, d);
        ++g_fail;
    }
}

static void expectBool(const char* name, bool got, bool want) {
    if (got == want) {
        printf("  [PASS] %-34s got=%s\n", name, got ? "true" : "false");
        ++g_pass;
    } else {
        printf("  [FAIL] %-34s got=%s want=%s\n", name,
               got ? "true" : "false", want ? "true" : "false");
        ++g_fail;
    }
}

int main() {
    printf("=== 旋转轴验证 ===\n");
    {
        Vec3 f, r, u;
        // 默认朝向：pitch=0 yaw=0 roll=0 -> forward=+X right=+Y up=+Z
        rotatorToAxes(Rot3{0, 0, 0}, f, r, u);
        expectNear("yaw0 fwd.x", f.x, 1.0f);
        expectNear("yaw0 fwd.y", f.y, 0.0f);
        expectNear("yaw0 right.x", r.x, 0.0f);
        expectNear("yaw0 right.y", r.y, 1.0f);
        expectNear("yaw0 up.z", u.z, 1.0f);
    }
    {
        Vec3 f, r, u;
        // yaw=90：UE4 左手系，forward 转到 +Y，right 转到 -X
        rotatorToAxes(Rot3{0, 90, 0}, f, r, u);
        expectNear("yaw90 fwd.x", f.x, 0.0f);
        expectNear("yaw90 fwd.y", f.y, 1.0f);
        expectNear("yaw90 right.x", r.x, -1.0f);
        expectNear("yaw90 up.z", u.z, 1.0f);
    }
    {
        Vec3 f, r, u;
        // pitch=90：抬头看天，forward 转到 +Z
        rotatorToAxes(Rot3{90, 0, 0}, f, r, u);
        expectNear("pitch90 fwd.z", f.z, 1.0f);
        expectNear("pitch90 fwd.x", f.x, 0.0f);
    }

    printf("\n=== W2S 针孔投影验证 (1920x1080, FOV=90) ===\n");
    FMinimalViewInfo cam{};
    cam.location = {0, 0, 0};
    cam.rotation = {0, 0, 0};
    cam.fov      = 90.f;

    Vec2 s{};

    // 正前方 -> 屏幕正中心
    bool ok = worldToScreen({1000, 0, 0}, cam, 1920, 1080, s);
    expectBool("前方目标可见", ok, true);
    expectNear("前方 sx", s.x, 960.f);
    expectNear("前方 sy", s.y, 540.f);

    // 横向 45 度 -> 屏幕右边缘
    // focal = 960 / tan(45) = 960；sx = 960 + 1000*960/1000 = 1920
    ok = worldToScreen({1000, 1000, 0}, cam, 1920, 1080, s);
    expectBool("右侧 45 度可见", ok, true);
    expectNear("右侧 45 度 sx", s.x, 1920.f);
    expectNear("右侧 45 度 sy", s.y, 540.f);

    // 垂直上方同角度 -> 屏幕上方外
    ok = worldToScreen({1000, 0, 1000}, cam, 1920, 1080, s);
    expectBool("上方 45 度可见", ok, true);
    expectNear("上方 45 度 sx", s.x, 960.f);
    expectNear("上方 45 度 sy", s.y, -420.f);

    // 背后 -> 不可见
    ok = worldToScreen({-1000, 0, 0}, cam, 1920, 1080, s);
    expectBool("背后目标不可见", ok, false);

    printf("\n=== W2S 朝向 90 度验证 ===\n");
    cam.rotation = {0, 90, 0};
    // 面朝 +Y，正前方目标
    ok = worldToScreen({0, 1000, 0}, cam, 1920, 1080, s);
    expectBool("朝+Y 正前方可见", ok, true);
    expectNear("朝+Y 正前方 sx", s.x, 960.f);
    expectNear("朝+Y 正前方 sy", s.y, 540.f);

    // 宇宙 -X 方向应在屏幕右侧（左手系 right=-X）
    ok = worldToScreen({-1000, 1000, 0}, cam, 1920, 1080, s);
    expectBool("朝+Y 右手边可见", ok, true);
    expectNear("朝+Y 右手边 sx", s.x, 1920.f);

    printf("\n=== 距离尺度验证 ===\n");
    {
        Vec3 a{0, 0, 0}, b{300, 400, 0};
        // UE4 单位是厘米，300,400 -> 500cm = 5m
        expectNear("3-4-5 三角距离(cm)", (b - a).length(), 500.f);
        expectNear("换算米", (b - a).length() / 100.f, 5.f);
    }

    printf("\n=== 边界条件 ===\n");
    {
        cam.rotation = {0, 0, 0};
        cam.fov = 90;
        ok = worldToScreen({0, 0, 0}, cam, 1920, 1080, s);
        expectBool("原点目标拒绝", ok, false);

        ok = worldToScreen({0.5f, 0, 0}, cam, 1920, 1080, s);
        expectBool("过近目标拒绝", ok, false);

        ok = worldToScreen({1000, 0, 0}, cam, 0, 0, s);
        expectBool("零尺寸屏幕拒绝", ok, false);
    }

    printf("\n========================================\n");
    printf("通过 %d, 失败 %d\n", g_pass, g_fail);
    printf("========================================\n");
    return g_fail == 0 ? 0 : 1;
}
