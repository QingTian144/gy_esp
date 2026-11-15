// esp.h —— 透视数据层：遍历 Actor 列表，产出待绘制的玩家信息
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include "ue4.h"
#include "dump.h"

namespace gy {

enum DrawFlag : uint32_t {
    D_Box      = 1u << 0,
    D_Skeleton = 1u << 1,
    D_Health   = 1u << 2,
    D_Name     = 1u << 3,
    D_Distance = 1u << 4,
    D_Snapline = 1u << 5,
    D_Weapon   = 1u << 6,
    D_Alert    = 1u << 7,   // 朝向自己时高亮
};

struct PlayerInfo {
    uint64_t   actor  = 0;
    ue4::Vec3  worldPos{};      // 脚下
    ue4::Vec3  headPos{};       // 头顶
    ue4::Vec2  screenFeet{};    // 屏幕坐标：脚
    ue4::Vec2  screenHead{};    // 屏幕坐标：头
    float      distance = 0.f;  // 米
    float      health   = 0.f;
    float      healthMax = 100.f;
    int        teamId   = -1;
    bool       isBot    = false;
    bool       dead     = false;
    bool       onScreen = false;
    bool       aimedAtMe = false;
    std::string name;
    // 骨骼屏幕坐标（可选）
    std::vector<ue4::Vec2> bones;
    std::vector<uint8_t>   boneValid;
};

struct EspConfig {
    uint32_t flags = D_Box | D_Health | D_Distance;

    bool  teamCheck      = true;    // 过滤队友
    bool  drawBots       = true;
    bool  drawDead       = false;
    float maxDistance    = 500.f;   // 米
    float boxThickness   = 1.6f;
    float textScale      = 1.0f;

    // 颜色（RGBA 0-255）
    uint8_t colEnemy[4]   = {255, 60, 60, 255};
    uint8_t colTeam[4]    = {60, 200, 255, 255};
    uint8_t colBot[4]     = {255, 200, 60, 255};
    uint8_t colHealth[4]  = {80, 255, 120, 255};
    uint8_t colText[4]    = {255, 255, 255, 255};
    uint8_t colSkeleton[4]= {255, 255, 255, 255};

    // 骨骼索引（和平精英标准人形，按需覆盖）
    int boneHead    = 6;
    int boneNeck    = 5;
    int bonePelvis  = 1;
    int boneLCollar = 8;
    int boneRCollar = 11;
    int boneLElbow  = 9;
    int boneRElbow  = 12;
    int boneLHand   = 10;
    int boneRHand   = 13;
    int boneLThigh  = 14;
    int boneRThigh  = 18;
    int boneLKnee   = 15;
    int boneRKnee   = 19;
    int boneLFoot   = 16;
    int boneRFoot   = 20;
};

class Esp {
public:
    Esp() = default;

    // 初始化：传入已 attach 的 ProcessMemory，内部完成偏移自动发现
    bool init(ProcessMemory& mem, uint32_t pid);

    void setConfig(const EspConfig& c) { cfg_ = c; }
    EspConfig&       config()       { return cfg_; }
    const EspConfig& config() const { return cfg_; }

    // 应用 dump 结果
    void applyDump(const ue4::DumpResult& d);

    // 每帧调用
    void update(int screenW, int screenH);

    const std::vector<PlayerInfo>& players() const { return players_; }

    bool    ready()      const { return ready_; }
    bool    hasCamera()  const { return hasCam_; }
    uint64_t localPawn() const { return localPawn_; }
    int     localTeam()  const { return localTeam_; }
    const ue4::FMinimalViewInfo& camera() const { return cam_; }

    const std::string& lastError() const { return err_; }
    const ue4::DumpResult& dumpResult() const { return dump_; }

    // 世界坐标 -> 屏幕（供绘制层复用相机参数）
    bool project(const ue4::Vec3& w, ue4::Vec2& s) const;

private:
    bool resolveWorld();
    bool readLocalPlayer();
    bool readOnePlayer(uint64_t actor, PlayerInfo& out);
    void readSkeleton(uint64_t actor, PlayerInfo& out);

    ProcessMemory*  mem_ = nullptr;
    std::unique_ptr<ue4::Reader> reader_;
    ue4::Offsets    off_{};
    ue4::DumpResult dump_{};

    uint32_t pid_ = 0;
    uint64_t libUe4_ = 0;

    EspConfig  cfg_;
    std::vector<PlayerInfo> players_;

    uint64_t level_     = 0;
    uint64_t localPawn_ = 0;
    int      localTeam_ = -1;
    bool     ready_     = false;
    bool     hasCam_    = false;

    ue4::FMinimalViewInfo cam_{};
    int screenW_ = 0;
    int screenH_ = 0;
    std::string err_;
};

} // namespace gy
