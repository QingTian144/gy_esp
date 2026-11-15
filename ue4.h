// ue4.h —— UE4 对象系统抽象（针对和平精英 / PUBG Mobile 系列）
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "mem.h"

namespace gy::ue4 {

// 所有偏移集中在这里。版本更新后只需要改这一个结构体，
// 或者交给 dump.cpp 的 autoDump 自动回填。
struct Offsets {
    // ---- UObject ----
    uint32_t UObject_Class       = 0x10;   // UClass*
    uint32_t UObject_Name        = 0x18;   // FName (8 bytes)
    uint32_t UObject_Outer       = 0x20;   // UObject*

    // ---- FNamePool ----
    uint32_t FNamePool_Blocks    = 0x10;   // Blocks[0] 起点
    uint32_t FName_Stride        = 2;      // UE4.18 变长 entry，key&0xFFFF 需 *2

    // ---- UWorld ----
    uint32_t UWorld_PersistentLevel     = 0x30;
    uint32_t UWorld_GameState           = 0x00;
    uint32_t UWorld_OwningGameInstance  = 0x00;

    // ---- ULevel ----
    uint32_t ULevel_Actors       = 0xA0;   // TArray<AActor*>

    // ---- AActor ----
    uint32_t AActor_RootComponent = 0x1B0; // USceneComponent*

    // ---- USceneComponent ----
    uint32_t Scene_ComponentToWorld = 0x1A0;  // FTransform (0x30 bytes)
    uint32_t Scene_RelativeLocation = 0x1D0;  // FVector
    uint32_t Scene_RelativeRotation = 0x1DC;  // FRotator

    // ---- APlayerController ----
    uint32_t PC_PlayerCameraManager = 0x00;
    uint32_t PC_AcknowledgedPawn    = 0x00;

    // ---- APlayerCameraManager ----
    uint32_t PCM_CameraCache        = 0x00;   // FCameraCacheEntry

    // ---- FCameraCacheEntry ----
    uint32_t Cache_POV              = 0x10;   // FMinimalViewInfo 起点

    // ---- STExtraPlayerCharacter ----
    uint32_t Char_Health            = 0x00;
    uint32_t Char_bDead             = 0x00;
    uint32_t Char_TeamID            = 0x00;
    uint32_t Char_PlayerName        = 0x00;
    uint32_t Char_Mesh              = 0x00;

    // ---- 全局 ----
    uint64_t GWorld        = 0;
    uint64_t GNames        = 0;
    uint64_t GUObjectArray = 0;
    uint64_t libBase       = 0;
    uint64_t libSize       = 0;
};

// ---------------------------------------------------------------- 基础类型

struct Vec2 { float x = 0, y = 0; };

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    float length() const;
};

struct Rot3 { float pitch = 0, yaw = 0, roll = 0; };

struct FName { int32_t index = 0; int32_t number = 0; };

// UE4 TArray<T>
template <typename T>
struct TArray {
    T*      data  = nullptr;
    int32_t count = 0;
    int32_t max   = 0;
};

// FTransform：UE4.18 布局 FQuat(16) + Translation(12)+pad + Scale3D(12)+pad
struct FTransform {
    float quat[4];
    float trans[3];
    float pad0;
    float scale[3];
    float pad1;
};

struct FMinimalViewInfo {
    Vec3   location;      // +0x00
    Rot3   rotation;      // +0x0C
    float  fov;           // +0x18
    float  desiredFov;    // +0x1C
};

// ---------------------------------------------------------------- 读取器

class Reader {
public:
    explicit Reader(ProcessMemory& mem) : mem_(mem) {}
    Reader(ProcessMemory& mem, const Offsets& o) : mem_(mem), off_(o) {}

    Offsets&       offsets()       { return off_; }
    const Offsets& offsets() const { return off_; }
    ProcessMemory& mem()           { return mem_; }

    bool initGlobals();
    uint64_t world() const { return off_.GWorld; }

    std::string resolveFName(FName name) const;
    std::string objectName(uint64_t obj) const;
    std::string className(uint64_t obj) const;
    uint64_t    objectClass(uint64_t obj) const;

    bool validObject(uint64_t obj) const;

    bool readVec3(uint64_t addr, Vec3& out) const;
    bool readRot3(uint64_t addr, Rot3& out) const;
    bool readTransform(uint64_t addr, FTransform& out) const;

    bool componentWorldPos(uint64_t component, Vec3& out) const;
    bool getCamera(uint64_t playerController, FMinimalViewInfo& out) const;

private:
    ProcessMemory& mem_;
    Offsets        off_;
};

// ---------------------------------------------------------------- 数学

void rotatorToAxes(const Rot3& r, Vec3& fwd, Vec3& right, Vec3& up);

bool worldToScreen(const Vec3& world, const FMinimalViewInfo& cam,
                   int screenW, int screenH, Vec2& out);

} // namespace gy::ue4
