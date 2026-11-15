// overlay.h —— 极简 OpenGL ES 2.0 渲染器（无第三方依赖）
//
// 不引入 ImGui，原因是 GFX 框架的特征字符串极易被内存扫描命中。
// 这里只做方框 / 线段 / 点阵文字，够透视用。
#pragma once

#include <cstdint>
#include <vector>

#if defined(__ANDROID__)
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#endif

namespace gy {

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    static Color rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return Color{r / 255.f, g / 255.f, b / 255.f, a / 255.f};
    }
};

class Overlay {
public:
    Overlay() = default;
    ~Overlay() { shutdown(); }

    // window 是 ANativeWindow*（从 Surface 取）
    bool init(void* nativeWindow, int width, int height);
    void resize(int width, int height);
    void shutdown();

    bool ready() const { return ready_; }

    void beginFrame();
    void endFrame();

    // 基本图元（坐标单位：像素，原点左上角）
    void rect(float x, float y, float w, float h, const Color& c, float thickness);
    void rectFilled(float x, float y, float w, float h, const Color& c);
    void line(float x1, float y1, float x2, float y2, const Color& c, float thickness);
    void text(float x, float y, const char* s, const Color& c, float scale);
    void textCentered(float cx, float y, const char* s, const Color& c, float scale);

    // 复合图元
    void healthBar(float x, float y, float h, float pct, const Color& fg,
                   const Color& bg, float width);

    float textWidth(const char* s, float scale) const;

private:
    void pushQuad(float x0, float y0, float x1, float y1, const Color& c);
    void flush();

    struct Vtx { float x, y, r, g, b, a; };
    std::vector<Vtx> verts_;

    int width_  = 0;
    int height_ = 0;
    bool ready_ = false;

#if defined(__ANDROID__)
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLConfig  config_  = nullptr;

    GLuint program_ = 0;
    GLuint vbo_     = 0;
    GLint  aPos_    = -1;
    GLint  aCol_    = -1;
#endif
};

} // namespace gy
