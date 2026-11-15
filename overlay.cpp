// overlay.cpp —— OpenGL ES 2.0 渲染实现
#include "overlay.h"
#include "mem.h"

#include <cstring>
#include <cstdio>
#include <cmath>

namespace gy {

// ================================================================
// 5x7 点阵字体：ASCII 32..126，每个字符 5 列，每列 bit0..bit6 从上到下
// ================================================================

static const uint8_t kFont5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x00,0x00,0x5F,0x00,0x00}, // '!'
    {0x00,0x07,0x00,0x07,0x00}, // '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // '$'
    {0x23,0x13,0x08,0x64,0x62}, // '%'
    {0x36,0x49,0x55,0x22,0x50}, // '&'
    {0x00,0x05,0x03,0x00,0x00}, // '\''
    {0x00,0x1C,0x22,0x41,0x00}, // '('
    {0x00,0x41,0x22,0x1C,0x00}, // ')'
    {0x14,0x08,0x3E,0x08,0x14}, // '*'
    {0x08,0x08,0x3E,0x08,0x08}, // '+'
    {0x00,0x50,0x30,0x00,0x00}, // ','
    {0x08,0x08,0x08,0x08,0x08}, // '-'
    {0x00,0x60,0x60,0x00,0x00}, // '.'
    {0x20,0x10,0x08,0x04,0x02}, // '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // '0'
    {0x00,0x42,0x7F,0x40,0x00}, // '1'
    {0x42,0x61,0x51,0x49,0x46}, // '2'
    {0x21,0x41,0x45,0x4B,0x31}, // '3'
    {0x18,0x14,0x12,0x7F,0x10}, // '4'
    {0x27,0x45,0x45,0x45,0x39}, // '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // '6'
    {0x01,0x71,0x09,0x05,0x03}, // '7'
    {0x36,0x49,0x49,0x49,0x36}, // '8'
    {0x06,0x49,0x49,0x29,0x1E}, // '9'
    {0x00,0x36,0x36,0x00,0x00}, // ':'
    {0x00,0x56,0x36,0x00,0x00}, // ';'
    {0x08,0x14,0x22,0x41,0x00}, // '<'
    {0x14,0x14,0x14,0x14,0x14}, // '='
    {0x00,0x41,0x22,0x14,0x08}, // '>'
    {0x02,0x01,0x51,0x09,0x06}, // '?'
    {0x32,0x49,0x79,0x41,0x3E}, // '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 'E'
    {0x7F,0x09,0x09,0x01,0x01}, // 'F'
    {0x3E,0x41,0x41,0x51,0x32}, // 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 'L'
    {0x7F,0x02,0x04,0x02,0x7F}, // 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 'V'
    {0x7F,0x20,0x18,0x20,0x7F}, // 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 'X'
    {0x03,0x04,0x78,0x04,0x03}, // 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 'Z'
    {0x00,0x00,0x7F,0x41,0x41}, // '['
    {0x02,0x04,0x08,0x10,0x20}, // '\\'
    {0x41,0x41,0x7F,0x00,0x00}, // ']'
    {0x04,0x02,0x01,0x02,0x04}, // '^'
    {0x40,0x40,0x40,0x40,0x40}, // '_'
    {0x00,0x01,0x02,0x04,0x00}, // '`'
    {0x20,0x54,0x54,0x54,0x78}, // 'a'
    {0x7F,0x48,0x44,0x44,0x38}, // 'b'
    {0x38,0x44,0x44,0x44,0x20}, // 'c'
    {0x38,0x44,0x44,0x48,0x7F}, // 'd'
    {0x38,0x54,0x54,0x54,0x18}, // 'e'
    {0x08,0x7E,0x09,0x01,0x02}, // 'f'
    {0x08,0x14,0x54,0x54,0x3C}, // 'g'
    {0x7F,0x08,0x04,0x04,0x78}, // 'h'
    {0x00,0x44,0x7D,0x40,0x00}, // 'i'
    {0x20,0x40,0x44,0x3D,0x00}, // 'j'
    {0x00,0x7F,0x10,0x28,0x44}, // 'k'
    {0x00,0x41,0x7F,0x40,0x00}, // 'l'
    {0x7C,0x04,0x18,0x04,0x78}, // 'm'
    {0x7C,0x08,0x04,0x04,0x78}, // 'n'
    {0x38,0x44,0x44,0x44,0x38}, // 'o'
    {0x7C,0x14,0x14,0x14,0x08}, // 'p'
    {0x08,0x14,0x14,0x18,0x7C}, // 'q'
    {0x7C,0x08,0x04,0x04,0x08}, // 'r'
    {0x48,0x54,0x54,0x54,0x20}, // 's'
    {0x04,0x3F,0x44,0x40,0x20}, // 't'
    {0x3C,0x40,0x40,0x20,0x7C}, // 'u'
    {0x1C,0x20,0x40,0x20,0x1C}, // 'v'
    {0x3C,0x40,0x30,0x40,0x3C}, // 'w'
    {0x44,0x28,0x10,0x28,0x44}, // 'x'
    {0x0C,0x50,0x50,0x50,0x3C}, // 'y'
    {0x44,0x64,0x54,0x4C,0x44}, // 'z'
    {0x00,0x08,0x36,0x41,0x00}, // '{'
    {0x00,0x00,0x7F,0x00,0x00}, // '|'
    {0x00,0x41,0x36,0x08,0x00}, // '}'
    {0x08,0x08,0x2A,0x1C,0x08}, // '~'
};

// 字形尺寸
static constexpr float kGlyphW  = 5.f;
static constexpr float kGlyphH  = 7.f;
static constexpr float kAdvance = 6.f;   // 字距（含 1 列空隙）

// ================================================================
// 图元
// ================================================================

void Overlay::pushQuad(float x0, float y0, float x1, float y1, const Color& c) {
    if (width_ <= 0 || height_ <= 0) return;
    if (x1 <= x0 || y1 <= y0) return;

    // 像素坐标 -> NDC
    auto nx = [this](float v) { return v / (float)width_  * 2.f - 1.f; };
    auto ny = [this](float v) { return 1.f - v / (float)height_ * 2.f; };

    float ax = nx(x0), bx = nx(x1);
    float ay = ny(y0), by = ny(y1);

    Vtx a{ax, ay, c.r, c.g, c.b, c.a};
    Vtx b{bx, ay, c.r, c.g, c.b, c.a};
    Vtx d{bx, by, c.r, c.g, c.b, c.a};
    Vtx e{ax, by, c.r, c.g, c.b, c.a};

    verts_.push_back(a);
    verts_.push_back(b);
    verts_.push_back(d);

    verts_.push_back(a);
    verts_.push_back(d);
    verts_.push_back(e);
}

void Overlay::rect(float x, float y, float w, float h, const Color& c, float t) {
    if (t < 0.5f) t = 0.5f;
    rectFilled(x, y, w, t, c);              // 上
    rectFilled(x, y + h - t, w, t, c);      // 下
    rectFilled(x, y, t, h, c);              // 左
    rectFilled(x + w - t, y, t, h, c);      // 右
}

void Overlay::rectFilled(float x, float y, float w, float h, const Color& c) {
    pushQuad(x, y, x + w, y + h, c);
}

void Overlay::line(float x1, float y1, float x2, float y2, const Color& c, float t) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.01f) return;

    // 法向量
    float nx = -dy / len * (t * 0.5f);
    float ny =  dx / len * (t * 0.5f);

    if (width_ <= 0 || height_ <= 0) return;
    auto nxN = [this](float v) { return v / (float)width_  * 2.f - 1.f; };
    auto nyN = [this](float v) { return 1.f - v / (float)height_ * 2.f; };

    Vtx a{nxN(x1 + nx), nyN(y1 + ny), c.r, c.g, c.b, c.a};
    Vtx b{nxN(x2 + nx), nyN(y2 + ny), c.r, c.g, c.b, c.a};
    Vtx d{nxN(x2 - nx), nyN(y2 - ny), c.r, c.g, c.b, c.a};
    Vtx e{nxN(x1 - nx), nyN(y1 - ny), c.r, c.g, c.b, c.a};

    verts_.push_back(a);
    verts_.push_back(b);
    verts_.push_back(d);

    verts_.push_back(a);
    verts_.push_back(d);
    verts_.push_back(e);
}

float Overlay::textWidth(const char* s, float scale) const {
    if (!s) return 0.f;
    size_t n = strlen(s);
    if (!n) return 0.f;
    return (n * kAdvance - 1.f) * scale;
}

void Overlay::text(float x, float y, const char* s, const Color& c, float scale) {
    if (!s || scale <= 0.f) return;

    float px = scale;      // 一个字体像素对应的屏幕尺寸
    float cx = x;

    for (const char* p = s; *p; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch < 32 || ch > 126) ch = '?';

        const uint8_t* g = kFont5x7[ch - 32];
        for (int col = 0; col < 5; ++col) {
            uint8_t bits = g[col];
            for (int row = 0; row < 7; ++row) {
                if (bits & (1u << row)) {
                    float gx = cx + col * px;
                    float gy = y  + row * px;
                    pushQuad(gx, gy, gx + px, gy + px, c);
                }
            }
        }
        cx += kAdvance * px;
    }
}

void Overlay::textCentered(float cx, float y, const char* s, const Color& c,
                           float scale) {
    float w = textWidth(s, scale);
    text(cx - w * 0.5f, y, s, c, scale);
}

void Overlay::healthBar(float x, float y, float h, float pct, const Color& fg,
                        const Color& bg, float width) {
    if (pct < 0.f) pct = 0.f;
    if (pct > 1.f) pct = 1.f;

    rectFilled(x - 1, y - 1, width + 2, h + 2, Color::rgb(0, 0, 0, 160));
    rectFilled(x, y, width, h, bg);

    float fillH = h * pct;
    rectFilled(x, y + (h - fillH), width, fillH, fg);
}

// ================================================================
// EGL / GL 初始化
// ================================================================

#if defined(__ANDROID__)

static const char* kVertSrc =
    "attribute vec2 aPos;\n"
    "attribute vec4 aCol;\n"
    "varying vec4 vCol;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "    vCol = aCol;\n"
    "}\n";

static const char* kFragSrc =
    "precision mediump float;\n"
    "varying vec4 vCol;\n"
    "void main() {\n"
    "    gl_FragColor = vCol;\n"
    "}\n";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetShaderInfoLog(s, sizeof(log) - 1, nullptr, log);
        logf("gl", "shader compile failed: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

#endif // __ANDROID__

bool Overlay::init(void* nativeWindow, int w, int h) {
    width_  = w;
    height_ = h;

#if defined(__ANDROID__)
    if (!nativeWindow) return false;

    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) {
        logf("gl", "eglGetDisplay failed");
        return false;
    }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(display_, &major, &minor)) {
        logf("gl", "eglInitialize failed");
        return false;
    }

    // ALPHA_SIZE 必须是 8，否则悬浮窗背景不透明
    const EGLint cfgAttr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      0,
        EGL_STENCIL_SIZE,    0,
        EGL_NONE
    };

    EGLint numCfg = 0;
    if (!eglChooseConfig(display_, cfgAttr, &config_, 1, &numCfg) || numCfg < 1) {
        logf("gl", "eglChooseConfig failed");
        return false;
    }

    const EGLint ctxAttr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, ctxAttr);
    if (context_ == EGL_NO_CONTEXT) {
        logf("gl", "eglCreateContext failed");
        return false;
    }

    surface_ = eglCreateWindowSurface(display_, config_,
                                      (ANativeWindow*)nativeWindow, nullptr);
    if (surface_ == EGL_NO_SURFACE) {
        logf("gl", "eglCreateWindowSurface failed");
        return false;
    }

    if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
        logf("gl", "eglMakeCurrent failed");
        return false;
    }

    eglSwapInterval(display_, 0);   // 关垂直同步

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragSrc);
    if (!vs || !fs) return false;

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);

    GLint linked = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!linked) {
        char log[512] = {0};
        glGetProgramInfoLog(program_, sizeof(log) - 1, nullptr, log);
        logf("gl", "program link failed: %s", log);
        return false;
    }

    aPos_ = glGetAttribLocation(program_, "aPos");
    aCol_ = glGetAttribLocation(program_, "aCol");

    glGenBuffers(1, &vbo_);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    verts_.reserve(1 << 16);
    ready_ = true;

    logf("gl", "overlay ready %dx%d (EGL %d.%d)", w, h, major, minor);
    return true;
#else
    (void)nativeWindow;
    return false;
#endif
}

void Overlay::shutdown() {
#if defined(__ANDROID__)
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        eglTerminate(display_);
    }
    display_ = EGL_NO_DISPLAY;
    surface_ = EGL_NO_SURFACE;
    context_ = EGL_NO_CONTEXT;

    if (vbo_)     { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (program_) { glDeleteProgram(program_); program_ = 0; }
#endif
    ready_ = false;
    verts_.clear();
}

// ================================================================
// 帧
// ================================================================

void Overlay::beginFrame() {
    verts_.clear();
}

void Overlay::endFrame() {
    flush();
}

void Overlay::flush() {
#if defined(__ANDROID__)
    if (!ready_) return;

    glViewport(0, 0, width_, height_);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (verts_.empty()) {
        eglSwapBuffers(display_, surface_);
        return;
    }

    glUseProgram(program_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(verts_.size() * sizeof(Vtx)),
                 verts_.data(), GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray((GLuint)aPos_);
    glVertexAttribPointer((GLuint)aPos_, 2, GL_FLOAT, GL_FALSE,
                          sizeof(Vtx), (void*)0);

    glEnableVertexAttribArray((GLuint)aCol_);
    glVertexAttribPointer((GLuint)aCol_, 4, GL_FLOAT, GL_FALSE,
                          sizeof(Vtx), (void*)(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts_.size());

    glDisableVertexAttribArray((GLuint)aPos_);
    glDisableVertexAttribArray((GLuint)aCol_);

    eglSwapBuffers(display_, surface_);
#else
    verts_.clear();
#endif
}

void Overlay::resize(int width, int height) {
    width_  = width;
    height_ = height;
}

} // namespace gy
