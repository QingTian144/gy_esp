// kdrv.cpp
#include "kdrv.h"
#include "mem.h"

#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cerrno>

// 共享协议头位于 <repo>/kernel/gy_kdrv.h，CMake 里会加 include 路径
#include "gy_kdrv.h"

namespace gy {

bool Kdrv::open(const char* devPath) {
    close();
    fd_ = ::open(devPath, O_RDWR | O_CLOEXEC);
    if (fd_ < 0)
        fd_ = ::open(devPath, O_RDONLY | O_CLOEXEC);
    if (fd_ < 0) {
        logf("kdrv", "open %s failed: %s", devPath, strerror(errno));
        return false;
    }
    logf("kdrv", "%s opened, version=0x%08x", devPath, version());
    return true;
}

void Kdrv::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

uint32_t Kdrv::version() const {
    if (fd_ < 0) return 0;
    struct gy_req req;
    memset(&req, 0, sizeof(req));
    if (ioctl(fd_, GY_VERSION, &req) < 0) return 0;
    return req.result;
}

bool Kdrv::read(uint32_t pid, uint64_t addr, void* buf, uint32_t size) const {
    if (fd_ < 0 || !buf || size == 0 || size > GY_MAX_XFER) return false;

    struct gy_req req;
    memset(&req, 0, sizeof(req));
    req.pid  = pid;
    req.addr = addr;
    req.ubuf = (uint64_t)(uintptr_t)buf;
    req.size = size;

    if (ioctl(fd_, GY_KREAD, &req) < 0) return false;
    return req.result == size;
}

bool Kdrv::write(uint32_t pid, uint64_t addr, const void* buf, uint32_t size) const {
    if (fd_ < 0 || !buf || size == 0 || size > GY_MAX_XFER) return false;

    struct gy_req req;
    memset(&req, 0, sizeof(req));
    req.pid  = pid;
    req.addr = addr;
    // 内核侧会 copy_from_user，所以这里直接把用户缓冲指针传过去
    req.ubuf = (uint64_t)(uintptr_t)buf;
    req.size = size;

    if (ioctl(fd_, GY_KWRITE, &req) < 0) return false;
    return req.result == size;
}

uint64_t Kdrv::moduleBase(uint32_t pid, const std::string& moduleName) const {
    if (fd_ < 0 || moduleName.empty()) return 0;

    struct gy_req req;
    memset(&req, 0, sizeof(req));
    req.pid = pid;
    snprintf((char*)req.name, GY_MAX_NAME, "%s", moduleName.c_str());

    if (ioctl(fd_, GY_MODBASE, &req) < 0) return 0;
    return req.result64;
}

uint64_t Kdrv::symbol(const std::string& name) const {
    if (fd_ < 0 || name.empty()) return 0;

    struct gy_req req;
    memset(&req, 0, sizeof(req));
    snprintf((char*)req.name, GY_MAX_NAME, "%s", name.c_str());

    if (ioctl(fd_, GY_KSYM, &req) < 0) return 0;
    return req.result64;
}

bool Kdrv::probe(uint32_t pid) const {
    if (fd_ < 0) return false;
    struct gy_req req;
    memset(&req, 0, sizeof(req));
    req.pid = pid;
    if (ioctl(fd_, GY_PROBE, &req) < 0) return false;
    return req.result == 1;
}

} // namespace gy
