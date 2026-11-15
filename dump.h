// dump.h —— 运行时偏移自动发现
//
// 和平精英每次大版本更新都会重排结构体偏移，硬编码的偏移活不过一个赛季。
// 这里用「结构特征 + 类名解析」在运行时反推偏移，不依赖任何固定值。
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "ue4.h"

namespace gy::ue4 {

struct DumpResult {
    // 全局
    uint64_t gnames = 0;
    uint64_t gworld = 0;
    uint64_t guobjectarray = 0;

    // 结构偏移（0 表示未发现）
    uint32_t off_persistentLevel = 0;
    uint32_t off_actors          = 0;
    uint32_t off_rootComponent   = 0;
    uint32_t off_cameraManager   = 0;
    uint32_t off_cameraCache     = 0;
    uint32_t off_pov             = 0;
    uint32_t off_health          = 0;
    uint32_t off_teamId          = 0;

    // 采样信息
    int      sampledActors    = 0;
    int      playerActors     = 0;
    std::string sampleClasses;   // 前 N 个 actor 类名，便于人工核对

    bool ok = false;
    std::string log;

    void note(const char* fmt, ...);
};

// 全自动扫描。libBase/libSize 来自 libUE4.so 的映射范围。
DumpResult autoDump(ProcessMemory& mem, uint64_t libBase, uint64_t libSize);

// 已知 GNames 的情况下单独找 GWorld（版本更新后复用）
uint64_t findGWorld(ProcessMemory& mem, uint64_t libBase, uint64_t libSize,
                    uint64_t gnames, uint32_t fnameStride = 2);

} // namespace gy::ue4
