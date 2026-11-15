// dump.cpp —— 运行时偏移自动发现
#include "dump.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

namespace gy::ue4 {

// ================================================================
// 日志
// ================================================================

void DumpResult::note(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (!log.empty()) log.push_back('\n');
    log += buf;

    logf("dump", "%s", buf);
}

// ================================================================
// 段收集
// ================================================================

namespace {

struct Seg { uint64_t start, end; };

constexpr uint64_t kChunk = 4ull * 1024 * 1024;   // 单次扫描块 4MB
constexpr uint64_t kMaxScanPerSeg = 128ull * 1024 * 1024;  // 单段上限 128MB

std::vector<Seg> collectSegs(ProcessMemory& mem, const char* module,
                             bool needWritable) {
    std::vector<Seg> out;
    for (const auto& m : mem.maps()) {
        if (m.path.find(module) == std::string::npos) continue;
        if (!m.readable()) continue;
        if (needWritable && !m.writable()) continue;
        if (m.size() < 16) continue;

        Seg s{m.start, m.end};
        if (s.end - s.start > kMaxScanPerSeg)
            s.end = s.start + kMaxScanPerSeg;
        out.push_back(s);
    }
    // 大段优先（.bss 通常在最后且最大）
    std::sort(out.begin(), out.end(),
              [](const Seg& a, const Seg& b) {
                  return (a.end - a.start) > (b.end - b.start);
              });
    return out;
}

} // namespace

// ================================================================
// GNames 扫描
//
// 判据：找到一个地址 X，使 *(X + 0x10) = Blocks[0]，
//      且 Blocks[0] 起第一个 FNameEntry 解析出字符串 "None"。
//      UE4 的 FNamePool 第 0 号条目恒为 "None"，这是最强特征。
// ================================================================

static uint64_t scanGNames(ProcessMemory& mem, const std::vector<Seg>& segs,
                           std::string& log2) {
    std::vector<uint8_t> buf;

    for (const auto& seg : segs) {

        for (uint64_t base = seg.start; base < seg.end; base += kChunk) {
            uint64_t len = std::min(kChunk, seg.end - base);
            if (len < 0x40) break;

            buf.assign(len, 0);
            if (!mem.read(base, buf.data(), len)) continue;

            // 8 字节对齐遍历；留 0x20 尾部余量给 Blocks[0] 字段
            for (uint64_t off = 0; off + 0x20 < len; off += 8) {
                uint64_t blocks0;
                memcpy(&blocks0, buf.data() + off + 0x10, 8);

                // 用户态指针粗过滤
                if (blocks0 < 0x100000000ull || blocks0 > 0x800000000000ull)
                    continue;

                // 远程验证：第一个 entry 应是 "None"
                uint8_t hdr[8] = {0};
                if (!mem.read(blocks0, hdr, 8)) continue;

                uint16_t h;
                memcpy(&h, hdr, 2);
                if (h & 1) continue;              // wide 不是 UE4.18 的形态
                int entryLen = (h >> 1) & 0x7FFF;
                if (entryLen != 4) continue;      // "None" 长度 4

                if (memcmp(hdr + 2, "None", 4) != 0) continue;

                char msg[160];
                snprintf(msg, sizeof(msg),
                         "GNames @ 0x%llx (blocks0=0x%llx)",
                         (unsigned long long)(base + off),
                         (unsigned long long)blocks0);
                log2 += msg;
                log2 += '\n';
                logf("dump", "%s", msg);
                return base + off;
            }
        }
    }
    return 0;
}

// ================================================================
// GWorld 扫描
// ================================================================

static uint64_t scanGWorld(ProcessMemory& mem, const std::vector<Seg>& segs,
                           uint64_t gnames, std::string& log2) {
    Offsets off{};
    off.GNames = gnames;
    Reader reader(mem, off);

    std::vector<uint8_t> buf;
    std::vector<uint64_t> candidates;

    // 第一遍：本地过滤出所有「指向合法 UObject 头」的指针
    for (const auto& seg : segs) {
        for (uint64_t base = seg.start; base < seg.end; base += kChunk) {
            uint64_t len = std::min(kChunk, seg.end - base);
            if (len < 8) break;

            buf.assign(len, 0);
            if (!mem.read(base, buf.data(), len)) continue;

            for (uint64_t o = 0; o + 8 <= len; o += 8) {
                uint64_t p;
                memcpy(&p, buf.data() + o, 8);
                if (p < 0x100000000ull || p > 0x800000000000ull) continue;
                candidates.push_back(p);
            }
        }
        if (candidates.size() > 400000) break;  // 防御性上限
    }

    logf("dump", "GWorld candidates: %zu", candidates.size());

    // 第二遍：验证类名
    for (uint64_t p : candidates) {
        if (!reader.validObject(p)) continue;

        std::string cn = reader.className(p);
        if (cn.empty()) continue;

        if (cn == "World" || cn.compare(0, 5, "World") == 0) {
            char msg[160];
            snprintf(msg, sizeof(msg), "GWorld @ 0x%llx class=%s",
                     (unsigned long long)p, cn.c_str());
            log2 += msg;
            log2 += '\n';
            logf("dump", "%s", msg);
            return p;
        }
    }
    return 0;
}

// ================================================================
// UWorld -> PersistentLevel
// ================================================================

static uint32_t findPersistentLevelOff(ProcessMemory& mem, Reader& reader,
                                       uint64_t world, std::string& log2) {
    for (uint32_t off = 0x20; off <= 0x100; off += 8) {
        uint64_t level = mem.readT<uint64_t>(world + off);
        if (!reader.validObject(level)) continue;

        std::string cn = reader.className(level);
        if (cn.empty()) continue;
        if (cn != "Level" && cn.compare(0, 5, "Level") != 0) continue;

        // 进一步确认 level 里有个合理的 Actors 数组
        bool hasActors = false;
        for (uint32_t a = 0x40; a <= 0x200 && !hasActors; a += 8) {
            uint64_t data  = mem.readT<uint64_t>(level + a);
            int32_t  count = mem.readT<int32_t>(level + a + 8);
            if (!mem.isPtr(data) || count <= 0 || count > 200000) continue;

            uint64_t first = mem.readT<uint64_t>(data);
            if (reader.validObject(first)) hasActors = true;
        }
        if (!hasActors) continue;

        char msg[160];
        snprintf(msg, sizeof(msg), "UWorld::PersistentLevel +0x%x (level=0x%llx)",
                 off, (unsigned long long)level);
        log2 += msg;
        log2 += '\n';
        logf("dump", "%s", msg);
        return off;
    }
    return 0;
}

// ================================================================
// ULevel -> Actors (TArray<AActor*>)
// ================================================================

static uint32_t findActorsOff(ProcessMemory& mem, Reader& reader,
                              uint64_t level, std::string& log2) {
    for (uint32_t off = 0x40; off <= 0x300; off += 8) {
        uint64_t data  = mem.readT<uint64_t>(level + off);
        int32_t  count = mem.readT<int32_t>(level + off + 8);
        int32_t  max   = mem.readT<int32_t>(level + off + 12);

        if (!mem.isPtr(data)) continue;
        if (count <= 0 || count > 200000) continue;
        if (max < count || max > 400000) continue;

        uint64_t first = mem.readT<uint64_t>(data);
        if (!reader.validObject(first)) continue;

        // 抽查数组中第二个元素，确认是真正的指针数组
        if (count > 1) {
            uint64_t second = mem.readT<uint64_t>(data + 8);
            if (second && !mem.isPtr(second)) continue;
        }

        char msg[160];
        snprintf(msg, sizeof(msg), "ULevel::Actors +0x%x (count=%d)",
                 off, count);
        log2 += msg;
        log2 += '\n';
        logf("dump", "%s", msg);
        return off;
    }
    return 0;
}

// ================================================================
// AActor -> RootComponent
// ================================================================

static uint32_t findRootComponentOff(ProcessMemory& mem, Reader& reader,
                                     const std::vector<uint64_t>& actors,
                                     std::string& log2) {
    for (uint32_t off = 0x80; off <= 0x400; off += 8) {
        int hit = 0, total = 0;

        for (size_t i = 0; i < actors.size() && total < 40; ++i) {
            uint64_t a = actors[i];
            if (!reader.validObject(a)) continue;
            ++total;

            uint64_t comp = mem.readT<uint64_t>(a + off);
            if (!mem.isPtr(comp)) continue;

            std::string cn = reader.className(comp);
            if (cn.empty()) continue;
            if (cn.find("Component") == std::string::npos) continue;

            // 该组件上应该能读到合理的世界坐标
            bool coordOk = false;
            for (uint32_t ct = 0x100; ct <= 0x280; ct += 0x10) {
                FTransform t{};
                if (!mem.read(comp + ct, &t, sizeof(t))) continue;
                float x = t.trans[0], y = t.trans[1], z = t.trans[2];
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
                    continue;
                if (fabsf(x) < 1e6f && fabsf(y) < 1e6f && fabsf(z) < 1e6f &&
                    (fabsf(x) > 1.f || fabsf(y) > 1.f)) {
                    coordOk = true;
                    break;
                }
            }
            if (coordOk) ++hit;
        }

        if (total >= 3 && hit * 2 >= total) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "AActor::RootComponent +0x%x (%d/%d)", off, hit, total);
            log2 += msg;
            log2 += '\n';
            logf("dump", "%s", msg);
            return off;
        }
    }
    return 0;
}

// ================================================================
// 玩家类判定
// ================================================================

static bool isPlayerClass(const std::string& n) {
    if (n.empty()) return false;
    if (n.compare(0, 9, "Default__") == 0) return false;  // CDO
    static const char* kMarks[] = {
        "STExtraPlayerCharacter",
        "STExtraSimpleCharacter",
        "STExtraBaseCharacter",
        "PlayerCharacter",
        "PlayerPawn",
    };
    for (const char* m : kMarks) {
        if (n.find(m) != std::string::npos) return true;
    }
    return false;
}

// ================================================================
// 主流程
// ================================================================

DumpResult autoDump(ProcessMemory& mem, uint64_t libBase, uint64_t libSize) {
    DumpResult r;

    (void)libBase;
    (void)libSize;

    auto segs = collectSegs(mem, "libUE4.so", true);
    if (segs.empty()) {
        r.note("libUE4.so 可写段为空，检查是否 attach 成功");
        return r;
    }

    uint64_t total = 0;
    for (auto& s : segs) total += s.end - s.start;
    r.note("libUE4.so 可写段 %zu 个，共 %llu MB",
           segs.size(), (unsigned long long)(total >> 20));

    // ---- 1. GNames ----
    std::string extra;
    r.gnames = scanGNames(mem, segs, extra);
    if (!r.gnames) {
        r.note("GNames 未找到");
        return r;
    }
    r.note("GNames = 0x%llx", (unsigned long long)r.gnames);

    // ---- 2. GWorld ----
    r.gworld = scanGWorld(mem, segs, r.gnames, extra);
    if (!r.gworld) {
        r.note("GWorld 未找到");
        return r;
    }
    r.note("GWorld = 0x%llx", (unsigned long long)r.gworld);

    // ---- 3. 构造 Reader ----
    Offsets off{};
    off.GNames = r.gnames;
    off.GWorld = r.gworld;
    Reader reader(mem, off);

    if (!reader.initGlobals()) {
        r.note("GWorld 自检失败");
        return r;
    }
    r.note("GWorld 自检通过，class=%s", reader.className(r.gworld).c_str());

    // ---- 4. PersistentLevel ----
    r.off_persistentLevel = findPersistentLevelOff(mem, reader, r.gworld, extra);
    if (!r.off_persistentLevel) {
        r.note("PersistentLevel 偏移未找到");
        return r;
    }

    uint64_t level = mem.readT<uint64_t>(r.gworld + r.off_persistentLevel);
    if (!reader.validObject(level)) {
        r.note("PersistentLevel 指针无效");
        return r;
    }

    // ---- 5. Actors ----
    r.off_actors = findActorsOff(mem, reader, level, extra);
    if (!r.off_actors) {
        r.note("ULevel::Actors 偏移未找到");
        return r;
    }

    uint64_t actorsData = mem.readT<uint64_t>(level + r.off_actors);
    int32_t  actorCount = mem.readT<int32_t>(level + r.off_actors + 8);
    r.sampledActors = actorCount;
    r.note("Actor 数组 0x%llx, count=%d",
           (unsigned long long)actorsData, actorCount);

    // ---- 6. 采样类名 ----
    std::vector<uint64_t> validActors;
    int sampled = std::min(actorCount, 400);
    std::string classes;

    for (int i = 0; i < sampled; ++i) {
        uint64_t a = mem.readT<uint64_t>(actorsData + (uint64_t)i * 8);
        if (!reader.validObject(a)) continue;
        validActors.push_back(a);

        std::string cn = reader.className(a);
        if (cn.empty()) continue;

        if (isPlayerClass(cn)) ++r.playerActors;

        if (classes.size() < 600) {
            if (!classes.empty()) classes += ", ";
            classes += cn;
        }
    }
    r.sampleClasses = classes;
    r.note("采样 %d 个 Actor，其中玩家类 %d 个", (int)validActors.size(),
           r.playerActors);

    // ---- 7. RootComponent ----
    r.off_rootComponent =
        findRootComponentOff(mem, reader, validActors, extra);
    if (!r.off_rootComponent) {
        r.note("RootComponent 偏移未找到（透视仍可能工作，仅骨骼受影响）");
    }

    r.ok = true;
    r.log += extra;
    r.note("偏移发现完成");
    return r;
}

uint64_t findGWorld(ProcessMemory& mem, uint64_t libBase, uint64_t libSize,
                    uint64_t gnames, uint32_t fnameStride) {
    (void)fnameStride;
    (void)libBase;
    (void)libSize;
    auto segs = collectSegs(mem, "libUE4.so", true);
    std::string dummy;
    return scanGWorld(mem, segs, gnames, dummy);
}

} // namespace gy::ue4
