// esp.cpp —— 透视数据层实现
#include "esp.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace gy {

using namespace ue4;

static bool isPlayerClass(const std::string& n) {
    if (n.empty()) return false;
    if (n.compare(0, 9, "Default__") == 0) return false;
    static const char* kMarks[] = {
        "STExtraPlayerCharacter",
        "STExtraSimpleCharacter",
        "STExtraBaseCharacter",
        "PlayerCharacter",
        "PlayerPawn",
    };
    for (const char* m : kMarks)
        if (n.find(m) != std::string::npos) return true;
    return false;
}

static bool isControllerClass(const std::string& n) {
    return n.find("PlayerController") != std::string::npos &&
           n.compare(0, 9, "Default__") != 0;
}

// ================================================================
// 初始化
// ================================================================

bool Esp::init(ProcessMemory& mem, uint32_t pid) {
    mem_  = &mem;
    pid_  = pid;
    ready_ = false;

    libUe4_ = mem.moduleBase("libUE4.so");
    if (!libUe4_) {
        err_ = "libUE4.so 未找到";
        return false;
    }

    uint64_t libSize = mem.moduleSize("libUE4.so");
    logf("esp", "libUE4.so base=0x%llx size=%llu MB",
         (unsigned long long)libUe4_,
         (unsigned long long)(libSize >> 20));

    // ---- 自动发现偏移 ----
    dump_ = autoDump(mem, libUe4_, libSize);
    if (!dump_.ok) {
        err_ = "偏移自动发现失败";
        return false;
    }

    applyDump(dump_);

    off_.libBase = libUe4_;
    off_.libSize = libSize;

    reader_ = std::make_unique<Reader>(mem, off_);

    if (!resolveWorld()) {
        err_ = "UWorld 解析失败";
        return false;
    }

    ready_ = true;
    logf("esp", "初始化完成，level=0x%llx", (unsigned long long)level_);
    return true;
}

void Esp::applyDump(const DumpResult& d) {
    dump_ = d;
    off_.GNames = d.gnames;
    off_.GWorld = d.gworld;
    if (d.off_persistentLevel) off_.UWorld_PersistentLevel = d.off_persistentLevel;
    if (d.off_actors)          off_.ULevel_Actors          = d.off_actors;
    if (d.off_rootComponent)   off_.AActor_RootComponent   = d.off_rootComponent;
    if (d.off_cameraManager)   off_.PC_PlayerCameraManager = d.off_cameraManager;
    if (d.off_cameraCache)     off_.PCM_CameraCache        = d.off_cameraCache;
    if (d.off_health)          off_.Char_Health            = d.off_health;
    if (d.off_teamId)          off_.Char_TeamID            = d.off_teamId;
}

bool Esp::resolveWorld() {
    if (!reader_ || !mem_) return false;

    uint64_t world = mem_->readT<uint64_t>(off_.GWorld);
    if (!mem_->isPtr(world)) return false;

    level_ = mem_->readT<uint64_t>(world + off_.UWorld_PersistentLevel);
    return mem_->isPtr(level_);
}

// ================================================================
// 本地玩家
// ================================================================

bool Esp::readLocalPlayer() {
    // 遍历 Actor 找 PlayerController，再取它持有的 Pawn
    uint64_t data  = mem_->readT<uint64_t>(level_ + off_.ULevel_Actors);
    int32_t  count = mem_->readT<int32_t>(level_ + off_.ULevel_Actors + 8);
    if (!mem_->isPtr(data) || count <= 0 || count > 200000) return false;

    for (int i = 0; i < count; ++i) {
        uint64_t a = mem_->readT<uint64_t>(data + (uint64_t)i * 8);
        if (!reader_->validObject(a)) continue;

        std::string cn = reader_->className(a);
        if (!isControllerClass(cn)) continue;

        // 在 PC 里找 Pawn 指针：指向的对象类名应是玩家类
        for (uint32_t o = 0x200; o <= 0x700; o += 8) {
            uint64_t pawn = mem_->readT<uint64_t>(a + o);
            if (!mem_->isPtr(pawn)) continue;
            if (!reader_->validObject(pawn)) continue;

            std::string pcn = reader_->className(pawn);
            if (!isPlayerClass(pcn)) continue;

            localPawn_ = pawn;
            off_.PC_AcknowledgedPawn = o;

            // 顺便在 PC 里找 CameraManager
            for (uint32_t c = 0x200; c <= 0x700; c += 8) {
                uint64_t cm = mem_->readT<uint64_t>(a + c);
                if (!mem_->isPtr(cm) || !reader_->validObject(cm)) continue;
                std::string ccn = reader_->className(cm);
                if (ccn.find("CameraManager") == std::string::npos) continue;

                off_.PC_PlayerCameraManager = c;

                // 在 CameraManager 里找 CameraCache
                for (uint32_t cc = 0x100; cc <= 0xF00; cc += 0x10) {
                    float fov = mem_->readT<float>(cm + cc + off_.Cache_POV + 0x18);
                    if (!std::isfinite(fov) || fov < 10.f || fov > 170.f) continue;

                    Vec3 loc{};
                    if (!mem_->read(cm + cc + off_.Cache_POV, &loc, sizeof(loc))) continue;
                    if (!std::isfinite(loc.x) || fabsf(loc.x) > 1e7f) continue;
                    if (fabsf(loc.x) < 1.f && fabsf(loc.y) < 1.f &&
                        fabsf(loc.z) < 1.f) continue;

                    off_.PCM_CameraCache = cc;
                    logf("esp", "CameraCache +0x%x, POV fov=%.1f", cc, fov);
                    return true;
                }
            }
            return true;
        }
    }
    return false;
}

// ================================================================
// 单个玩家
// ================================================================

bool Esp::readOnePlayer(uint64_t actor, PlayerInfo& out) {
    uint64_t root = mem_->readT<uint64_t>(actor + off_.AActor_RootComponent);
    if (!mem_->isPtr(root)) return false;

    Vec3 pos{};
    if (!reader_->componentWorldPos(root, pos)) return false;

    // 过滤明显非角色的坐标（比如相机自身、默认出生点）
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z))
        return false;
    if (fabsf(pos.x) > 1e6f || fabsf(pos.y) > 1e6f) return false;

    out.actor    = actor;
    out.worldPos = pos;
    // UE4 角色高度约 180cm，透视框用这个估算头顶
    out.headPos  = {pos.x, pos.y, pos.z + 165.f};

    // 血量 / 队伍
    if (off_.Char_Health) {
        float h = mem_->readT<float>(actor + off_.Char_Health);
        if (std::isfinite(h) && h >= 0.f && h <= 20000.f)
            out.health = h;
    }
    if (off_.Char_TeamID) {
        int t = mem_->readT<int32_t>(actor + off_.Char_TeamID);
        if (t >= 0 && t < 1000) out.teamId = t;
    }
    if (off_.Char_bDead) {
        uint8_t d = mem_->readT<uint8_t>(actor + off_.Char_bDead);
        out.dead = (d != 0);
    } else {
        out.dead = (out.health >= 0.f && out.health <= 0.f);
    }

    return true;
}

// ================================================================
// 主更新
// ================================================================

void Esp::update(int screenW, int screenH) {
    players_.clear();
    screenW_ = screenW;
    screenH_ = screenH;

    if (!ready_ || !mem_ || !reader_) return;
    if (!mem_->alive()) { ready_ = false; err_ = "目标进程已退出"; return; }

    // UWorld 可能因地图切换重新分配，每帧重解析
    if (!resolveWorld()) return;

    // ---- 本地玩家 + 相机 ----
    if (!localPawn_ || off_.PCM_CameraCache == 0) {
        readLocalPlayer();
    }

    hasCam_ = false;
    if (localPawn_ && off_.PC_PlayerCameraManager && off_.PCM_CameraCache) {
        // 从 PC 拿相机
        uint64_t data  = mem_->readT<uint64_t>(level_ + off_.ULevel_Actors);
        int32_t  count = mem_->readT<int32_t>(level_ + off_.ULevel_Actors + 8);

        if (mem_->isPtr(data) && count > 0 && count < 200000) {
            // 本地玩家位置作为相机 fallback
            Vec3 localPos{};
            uint64_t root = mem_->readT<uint64_t>(localPawn_ + off_.AActor_RootComponent);
            if (mem_->isPtr(root)) reader_->componentWorldPos(root, localPos);

            for (int i = 0; i < count && !hasCam_; ++i) {
                uint64_t a = mem_->readT<uint64_t>(data + (uint64_t)i * 8);
                if (!reader_->validObject(a)) continue;
                if (!isControllerClass(reader_->className(a))) continue;

                hasCam_ = reader_->getCamera(a, cam_);
                if (hasCam_) break;

                // 相机读失败就退化为「本地玩家位置 + 视线方向」
                if (!hasCam_ && localPos.x != 0.f) {
                    cam_.location = {localPos.x, localPos.y, localPos.z + 60.f};
                    cam_.rotation = {0, 0, 0};
                    cam_.fov      = 90.f;
                }
            }
        }
    }

    if (!hasCam_) return;

    // ---- Actors ----
    uint64_t data  = mem_->readT<uint64_t>(level_ + off_.ULevel_Actors);
    int32_t  count = mem_->readT<int32_t>(level_ + off_.ULevel_Actors + 8);
    if (!mem_->isPtr(data) || count <= 0 || count > 200000) return;

    // 批量读 Actor 指针数组，减少 ioctl 次数
    std::vector<uint64_t> actors((size_t)count, 0);
    if (!mem_->read(data, actors.data(), (size_t)count * 8)) return;

    Vec3 camPos = cam_.location;

    for (int i = 0; i < count; ++i) {
        uint64_t a = actors[(size_t)i];
        if (!mem_->isPtr(a) || a == localPawn_) continue;
        if (!reader_->validObject(a)) continue;

        std::string cn = reader_->className(a);
        if (!isPlayerClass(cn)) continue;

        PlayerInfo p;
        if (!readOnePlayer(a, p)) continue;

        // 距离（厘米 -> 米）
        Vec3 d{p.worldPos.x - camPos.x, p.worldPos.y - camPos.y,
               p.worldPos.z - camPos.z};
        p.distance = d.length() / 100.f;

        if (p.distance > cfg_.maxDistance) continue;
        if (p.distance < 0.5f) continue;                 // 跳过自己
        if (p.dead && !cfg_.drawDead) continue;
        if (cfg_.teamCheck && localTeam_ >= 0 && p.teamId == localTeam_) continue;

        // ---- 投影 ----
        Vec2 feet{}, head{};
        bool okFeet = worldToScreen(p.worldPos, cam_, screenW_, screenH_, feet);
        bool okHead = worldToScreen(p.headPos,  cam_, screenW_, screenH_, head);

        if (!okFeet && !okHead) continue;

        p.screenFeet = okFeet ? feet : head;
        p.screenHead = okHead ? head : feet;
        p.onScreen   = true;

        // 屏幕外过滤
        float boxH = p.screenFeet.y - p.screenHead.y;
        if (boxH <= 0.5f) continue;
        if (p.screenFeet.x < -200 || p.screenFeet.x > screenW_ + 200) continue;

        players_.push_back(std::move(p));
    }

    // 远到近排序，保证近处画在上层
    std::sort(players_.begin(), players_.end(),
              [](const PlayerInfo& a, const PlayerInfo& b) {
                  return a.distance > b.distance;
              });
}

bool Esp::project(const Vec3& w, Vec2& s) const {
    if (!hasCam_) return false;
    return worldToScreen(w, cam_, screenW_, screenH_, s);
}

// ================================================================
// 骨骼
// ================================================================

void Esp::readSkeleton(uint64_t actor, PlayerInfo& out) {
    (void)actor;
    (void)out;
    // 骨骼需要 Mesh -> ComponentSpaceTransforms 的偏移，
    // 各版本差异大，这里留接口，偏移确认后再填。
}

} // namespace gy
