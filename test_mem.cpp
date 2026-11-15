// test_mem.cpp —— 内存层端到端验证（以宿主进程自身为目标）
#include "mem.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <cstdlib>

using namespace gy;

static int   g_pass = 0;
static int   g_fail = 0;
static int   g_magic = 0x47594B44;             // 'GYKD'
static char  g_text[] = "gy_esp_probe_string_42";
static float g_vec[4] = {1.5f, -2.5f, 3.25f, 100.0f};

static void check(const char* name, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (ok) ++g_pass; else ++g_fail;
}

int main() {
    printf("=== 自读内存层验证 (pid=%d) ===\n", (int)getpid());

    ProcessMemory mem;
    check("attachByPid(self)", mem.attachByPid(getpid()));
    check("maps 非空", !mem.maps().empty());
    check("alive()", mem.alive());

    printf("  映射数量: %zu\n", mem.maps().size());

    // ---- 读整型 ----
    int v = mem.readT<int>((uint64_t)(uintptr_t)&g_magic);
    printf("  g_magic 读到 0x%08X, 期望 0x%08X\n", v, g_magic);
    check("readT<int> 正确", v == 0x47594B44);

    // ---- 读浮点数组 ----
    float fv[4] = {0};
    bool okf = mem.read((uint64_t)(uintptr_t)g_vec, fv, sizeof(fv));
    printf("  g_vec 读到 %.2f %.2f %.2f %.2f\n", fv[0], fv[1], fv[2], fv[3]);
    check("read 浮点数组",
          okf && fv[0] == 1.5f && fv[1] == -2.5f && fv[2] == 3.25f);

    // ---- 读字符串 ----
    std::string s = mem.readStdString((uint64_t)(uintptr_t)g_text, 64);
    printf("  字符串读到 \"%s\"\n", s.c_str());
    check("readStdString", s == "gy_esp_probe_string_42");

    // ---- 写 ----
    int newv = 0x11223344;
    bool okw = mem.write((uint64_t)(uintptr_t)&g_magic, &newv, sizeof(newv));
    printf("  写入后 g_magic = 0x%08X\n", g_magic);
    check("write 生效", okw && g_magic == 0x11223344);
    g_magic = 0x47594B44;

    // ---- mapped 判定 ----
    check("mapped(&g_magic)", mem.mapped((uint64_t)(uintptr_t)&g_magic, 4));
    check("未映射地址被拒", !mem.mapped(0xDEAD0000ull, 4));

    // ---- 非法地址读 ----
    int junk = 0;
    check("非法地址读失败", !mem.read(0xDEAD0000ull, &junk, sizeof(junk)));

    // ---- 模块定位 ----
    uint64_t libc = mem.moduleBase("libc");
    if (libc) {
        printf("  libc 基址 0x%llx\n", (unsigned long long)libc);
        check("libc 基址可读", mem.mapped(libc, 8));
    } else {
        printf("  (未找到 libc 映射，跳过)\n");
    }

    // ---- 跨页批量读 ----
    // 用页对齐的堆内存，确保 8KB 落在同一映射内且跨 2 个页
    {
        uint8_t* page = (uint8_t*)aligned_alloc(4096, 8192);
        if (page) {
            memset(page, 0xAB, 8192);
            uint8_t buf[8192];
            bool ok8k = mem.read((uint64_t)(uintptr_t)page, buf, sizeof(buf));
            bool intact = ok8k && buf[0] == 0xAB && buf[4095] == 0xAB &&
                          buf[4096] == 0xAB && buf[8191] == 0xAB;
            printf("  8KB 读返回=%d 首=%02X 跨界=%02X 尾=%02X\n",
                   (int)ok8k, buf[0], buf[4096], buf[8191]);
            check("8KB 跨页读", intact);
            free(page);
        }
    }

    // ---- 统计 ----
    auto st = mem.stats();
    printf("  reads=%llu fails=%llu\n",
           (unsigned long long)st.reads, (unsigned long long)st.fails);

    mem.detach();

    printf("\n========================================\n");
    printf("通过 %d, 失败 %d\n", g_pass, g_fail);
    printf("========================================\n");
    return g_fail == 0 ? 0 : 1;
}
