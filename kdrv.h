// kdrv.h —— 用户态访问 gy_kdrv 内核模块
#pragma once

#include <cstdint>
#include <string>

namespace gy {

class Kdrv {
public:
    Kdrv() = default;
    ~Kdrv() { close(); }

    Kdrv(const Kdrv&)            = delete;
    Kdrv& operator=(const Kdrv&) = delete;

    bool open(const char* devPath = "/dev/gyk");
    void close();
    bool ready() const { return fd_ >= 0; }

    uint32_t version() const;

    bool read(uint32_t pid, uint64_t addr, void* buf, uint32_t size) const;
    bool write(uint32_t pid, uint64_t addr, const void* buf, uint32_t size) const;

    uint64_t moduleBase(uint32_t pid, const std::string& moduleName) const;
    uint64_t symbol(const std::string& name) const;
    bool     probe(uint32_t pid) const;

private:
    int fd_ = -1;
};

} // namespace gy
