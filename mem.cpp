// mem.cpp
#include "mem.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

// ---- 各架构 process_vm_readv / writev 系统调用号 ----
#ifndef __NR_process_vm_readv
#if defined(__aarch64__)
#define __NR_process_vm_readv 270
#define __NR_process_vm_writev 271
#elif defined(__arm__) || defined(__i386__)
#define __NR_process_vm_readv 376
#define __NR_process_vm_writev 377
#elif defined(__x86_64__)
#define __NR_process_vm_readv 310
#define __NR_process_vm_writev 311
#endif
#endif

namespace gy {

void logf(const char* tag, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, tag, "%s", buf);
#else
    fprintf(stderr, "[%s] %s\n", tag, buf);
    fflush(stderr);
#endif
}

// ---------------------------------------------------------------- 工具

static std::string readFileString(const char* path, size_t maxLen = 4096) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    std::vector<char> buf(maxLen + 1);
    ssize_t n = ::read(fd, buf.data(), maxLen);
    close(fd);
    if (n <= 0) return {};
    buf[n] = '\0';
    return std::string(buf.data(), ::strnlen(buf.data(), (size_t)n));
}

// 从 /proc/<pid>/cmdline 取第一段，即进程名/包名
static std::string readCmdline(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", (int)pid);
    return readFileString(path, 512);
}

// ---------------------------------------------------------------- 进程发现

pid_t ProcessMemory::FindProcess(const char* processName) {
    if (!processName || !*processName) return -1;
    auto all = FindAllProcesses(processName);
    return all.empty() ? -1 : all.front();
}

std::vector<pid_t> ProcessMemory::FindAllProcesses(const char* processName) {
    std::vector<pid_t> out;
    if (!processName || !*processName) return out;

    DIR* d = opendir("/proc");
    if (!d) return out;

    pid_t self = getpid();
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        pid_t pid = (pid_t)atoi(e->d_name);
        if (pid <= 1 || pid == self) continue;

        std::string cmd = readCmdline(pid);
        if (cmd.empty()) continue;
        // 去掉末尾 NUL 残留
        while (!cmd.empty() && cmd.back() == '\0') cmd.pop_back();

        if (cmd == processName) {
            out.insert(out.begin(), pid);   // 精确匹配优先
        } else if (cmd.find(processName) != std::string::npos) {
            out.push_back(pid);
        }
    }
    closedir(d);
    return out;
}

// ---------------------------------------------------------------- 挂载

bool ProcessMemory::attach(const char* processName) {
    pid_t pid = FindProcess(processName);
    if (pid <= 0) {
        logf("mem", "process '%s' not found", processName);
        return false;
    }
    return attachByPid(pid);
}

bool ProcessMemory::attachByPid(pid_t pid) {
    if (pid <= 1) return false;
    detach();

    pid_ = pid;
    // 先尝试打开 /proc/pid/mem 作为备用通道（读取会走 pread）
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/mem", (int)pid);
    memFd_ = open(path, O_RDWR | O_CLOEXEC);
    if (memFd_ < 0) {
        memFd_ = open(path, O_RDONLY | O_CLOEXEC);
    }

    refreshMaps();
    if (maps_.empty()) {
        logf("mem", "attach pid %d failed: maps empty (permission?)", (int)pid);
        detach();
        return false;
    }
    logf("mem", "attached pid %d, %zu mappings, memFd=%d",
         (int)pid, maps_.size(), memFd_);
    return true;
}

void ProcessMemory::detach() {
    if (memFd_ >= 0) {
        close(memFd_);
        memFd_ = -1;
    }
    pid_ = -1;
    maps_.clear();
}

bool ProcessMemory::alive() const {
    if (pid_ <= 0) return false;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", (int)pid_);
    return access(path, R_OK) == 0;
}

// ---------------------------------------------------------------- maps

void ProcessMemory::refreshMaps() {
    maps_.clear();
    if (pid_ <= 0) return;

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", (int)pid_);
    FILE* f = fopen(path, "re");
    if (!f) return;

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        MapEntry m{};
        char perms[8] = {0};
        char dev[16] = {0};
        unsigned long inode = 0;
        int pathOff = 0;

        int n = sscanf(line, "%lx-%lx %7s %lx %15s %lu %n",
                       (unsigned long*)&m.start, (unsigned long*)&m.end,
                       perms, (unsigned long*)&m.offset, dev, &inode, &pathOff);
        if (n < 6) continue;
        memcpy(m.perms, perms, sizeof(m.perms) - 1);

        if (pathOff > 0) {
            size_t len = strlen(line);
            if ((size_t)pathOff < len) {
                char* p = line + pathOff;
                while (*p == ' ' || *p == '\t') ++p;
                size_t plen = strlen(p);
                while (plen > 0 && (p[plen - 1] == '\n' || p[plen - 1] == '\r')) --plen;
                m.path.assign(p, plen);
            }
        }
        maps_.push_back(std::move(m));
    }
    fclose(f);
}

const MapEntry* ProcessMemory::moduleMap(const char* moduleName) const {
    if (!moduleName) return nullptr;
    const MapEntry* best = nullptr;
    for (const auto& m : maps_) {
        if (m.path.find(moduleName) == std::string::npos) continue;
        if (!best || m.start < best->start) best = &m;
    }
    return best;
}

uint64_t ProcessMemory::moduleBase(const char* moduleName) const {
    const MapEntry* m = moduleMap(moduleName);
    return m ? m->start : 0;
}

uint64_t ProcessMemory::moduleSize(const char* moduleName) const {
    if (!moduleName || !*moduleName) return 0;
    uint64_t lo = UINT64_MAX, hi = 0;
    for (const auto& m : maps_) {
        if (m.path.find(moduleName) == std::string::npos) continue;
        if (m.start < lo) lo = m.start;
        if (m.end   > hi) hi = m.end;
    }
    return hi > lo ? hi - lo : 0;
}

uint64_t ProcessMemory::moduleTextBase(const char* moduleName) const {
    if (!moduleName) return 0;
    uint64_t best = 0;
    for (const auto& m : maps_) {
        if (m.path.find(moduleName) == std::string::npos) continue;
        if (!m.executable() || !m.readable()) continue;
        if (!best || m.start < best) best = m.start;
    }
    return best;
}

bool ProcessMemory::mapped(uint64_t addr, size_t size) const {
    if (addr == 0) return false;
    uint64_t end = addr + (uint64_t)size;
    if (end < addr) return false;
    for (const auto& m : maps_) {
        if (addr >= m.start && addr < m.end) {
            return end <= m.end || size < 2;
        }
    }
    return false;
}

// ---------------------------------------------------------------- 读写

bool ProcessMemory::read(uint64_t addr, void* buf, size_t size) const {
    if (pid_ <= 0 || !buf || size == 0) return false;

    uint8_t* out = (uint8_t*)buf;
    size_t   done = 0;

    while (done < size) {
        size_t chunk = size - done;
        // 单次上限 64KB，跨页不连续时更容易部分成功
        if (chunk > 0x10000) chunk = 0x10000;

        bool ok = false;

#if defined(__NR_process_vm_readv)
        {
            struct iovec local { out + done, chunk };
            struct iovec remote { (void*)(uintptr_t)(addr + done), chunk };
            ssize_t r = syscall(__NR_process_vm_readv, pid_, &local, 1, &remote, 1, 0);
            if (r == (ssize_t)chunk) ok = true;
        }
#endif
        if (!ok && memFd_ >= 0) {
            ssize_t r = pread(memFd_, out + done, chunk, (off_t)(addr + done));
            if (r == (ssize_t)chunk) ok = true;
        }

        if (!ok) {
            ++stats_.fails;
            return false;
        }
        done += chunk;
        ++stats_.reads;
    }
    return true;
}

bool ProcessMemory::write(uint64_t addr, const void* buf, size_t size) const {
    if (pid_ <= 0 || !buf || size == 0) return false;

    uint8_t* p = (uint8_t*)buf;
    size_t done = 0;
    while (done < size) {
        size_t chunk = size - done;
        if (chunk > 0x10000) chunk = 0x10000;
        bool ok = false;
#if defined(__NR_process_vm_writev)
        {
            struct iovec local { p + done, chunk };
            struct iovec remote { (void*)(uintptr_t)(addr + done), chunk };
            ssize_t r = syscall(__NR_process_vm_writev, pid_, &local, 1, &remote, 1, 0);
            if (r == (ssize_t)chunk) ok = true;
        }
#endif
        if (!ok && memFd_ >= 0) {
            ssize_t r = pwrite(memFd_, p + done, chunk, (off_t)(addr + done));
            if (r == (ssize_t)chunk) ok = true;
        }
        if (!ok) return false;
        done += chunk;
    }
    return true;
}

std::vector<uint8_t> ProcessMemory::readBytes(uint64_t addr, size_t size) const {
    std::vector<uint8_t> v(size);
    if (!read(addr, v.data(), size)) v.clear();
    return v;
}

bool ProcessMemory::readCStr(uint64_t addr, char* out, size_t maxLen) const {
    if (!out || maxLen < 2) return false;
    size_t got = 0;
    // 一次 32 字节慢慢拉，避免跨页失败
    while (got + 1 < maxLen) {
        char tmp[32];
        size_t want = sizeof(tmp);
        if (got + want > maxLen - 1) want = maxLen - 1 - got;
        if (!read(addr + got, tmp, want)) break;
        for (size_t i = 0; i < want; ++i) {
            if (tmp[i] == '\0') {
                out[got] = '\0';
                return got > 0;
            }
            out[got++] = tmp[i];
        }
    }
    if (got == 0) return false;
    out[got] = '\0';
    return true;
}

std::string ProcessMemory::readStdString(uint64_t addr, size_t maxLen) const {
    std::vector<char> buf(maxLen + 1, 0);
    if (!readCStr(addr, buf.data(), maxLen + 1)) return {};
    return std::string(buf.data());
}

} // namespace gy
