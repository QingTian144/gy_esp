// mem.h —— 跨进程内存读写（外挂式，process_vm_readv 优先）
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <string>
#include <vector>
#include <sys/types.h>

namespace gy {

struct MapEntry {
    uint64_t    start  = 0;
    uint64_t    end    = 0;
    char        perms[8] = {0};
    uint64_t    offset = 0;
    std::string path;

    uint64_t size()        const { return end > start ? end - start : 0; }
    bool readable()        const { return perms[0] == 'r'; }
    bool writable()        const { return perms[1] == 'w'; }
    bool executable()      const { return perms[2] == 'x'; }
};

class ProcessMemory {
public:
    // 按 /proc/<pid>/cmdline 精确匹配进程名
    static pid_t FindProcess(const char* processName);
    static std::vector<pid_t> FindAllProcesses(const char* processName);

    bool attach(const char* processName);
    bool attachByPid(pid_t pid);
    void detach();
    bool alive() const;

    pid_t pid() const { return pid_; }
    const std::vector<MapEntry>& maps() const { return maps_; }
    void refreshMaps();

    // 模块（.so）基址 = 该文件所有映射里地址最小的那个 start
    uint64_t moduleBase(const char* moduleName) const;
    uint64_t moduleSize(const char* moduleName) const;
    const MapEntry* moduleMap(const char* moduleName) const;
    // 主映射（r-xp 的 text 段），某些偏移相对这里而不是 file offset 0
    uint64_t moduleTextBase(const char* moduleName) const;

    bool read(uint64_t addr, void* buf, size_t size) const;
    bool write(uint64_t addr, const void* buf, size_t size) const;

    template <typename T>
    bool readT(uint64_t addr, T& out) const {
        return read(addr, &out, sizeof(T));
    }
    template <typename T>
    T readT(uint64_t addr) const {
        T v{};
        read(addr, &v, sizeof(T));
        return v;
    }

    std::vector<uint8_t> readBytes(uint64_t addr, size_t size) const;
    // 读到 NUL 或 maxLen-1 为止
    bool readCStr(uint64_t addr, char* out, size_t maxLen) const;
    std::string readStdString(uint64_t addr, size_t maxLen = 128) const;

    bool mapped(uint64_t addr, size_t size = 1) const;
    bool isPtr(uint64_t p) const {
        return p > 0x10000ULL && mapped(p, sizeof(uint64_t));
    }

    // 统计：诊断用
    struct Stats {
        uint64_t reads = 0;
        uint64_t fails = 0;
    };
    Stats stats() const { return stats_; }
    void resetStats() { stats_ = Stats{}; }

private:
    pid_t                    pid_   = -1;
    mutable int              memFd_ = -1;
    std::vector<MapEntry>    maps_;
    mutable Stats            stats_;
};

// 简化日志：Android 走 __android_log_print，host 走 stderr
void logf(const char* tag, const char* fmt, ...);

} // namespace gy
