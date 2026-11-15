// ue4.cpp —— UE4 对象系统实现 + 世界转屏幕
#include "ue4.h"

#include <cmath>
#include <cstring>
#include <cstdio>

namespace gy::ue4 {

static constexpr float kPi = 3.14159265358979323846f;

// ---------------------------------------------------------------- Vec3

float Vec3::length() const {
    return sqrtf(x * x + y * y + z * z);
}

// ---------------------------------------------------------------- FName

bool Reader::initGlobals() {
    if (!off_.GNames || !off_.GWorld) return false;

    // GWorld 必须指向一个合法 UObject，且类名可解析
    if (!validObject(off_.GWorld)) return false;
    std::string cn = className(off_.GWorld);
    if (cn != "World" && cn.compare(0, 5, "World") != 0) {
        logf("ue4", "GWorld sanity failed, class=%s", cn.c_str());
        return false;
    }
    return true;
}

std::string Reader::resolveFName(FName name) const {
    if (!off_.GNames) return {};

    uint32_t key = (uint32_t)name.index;
    if (key == 0) return {};

    uint32_t blockIdx = key >> 16;
    uint32_t offset   = key & 0xFFFF;

    // 上限保护：UE4 的 block 数不会超过 8192
    if (blockIdx >= 8192) return {};

    uint64_t blockArr = off_.GNames + off_.FNamePool_Blocks;
    uint64_t block    = mem_.readT<uint64_t>(blockArr + (uint64_t)blockIdx * 8);
    if (!mem_.isPtr(block)) return {};

    uint64_t entry = block + (uint64_t)offset * off_.FName_Stride;
    if (!mem_.mapped(entry, 4)) return {};

    uint16_t header = mem_.readT<uint16_t>(entry);
    bool     wide   = (header & 1) != 0;
    int      len    = (header >> 1) & 0x7FFF;

    if (len <= 0 || len > 1024) return {};

    std::string out;
    out.resize(len + 1);

    if (!wide) {
        if (!mem_.read(entry + 2, &out[0], len)) return {};
        out.resize(len);
    } else {
        // UTF-16 -> ASCII 粗转（UE4 的类名/对象名基本是 ASCII）
        std::vector<uint16_t> wbuf(len);
        if (!mem_.read(entry + 2, wbuf.data(), (size_t)len * 2)) return {};
        out.clear();
        for (int i = 0; i < len; ++i) {
            uint16_t c = wbuf[i];
            out.push_back(c < 0x80 ? (char)c : '?');
        }
    }

    // 裁掉尾部垃圾
    size_t nul = out.find('\0');
    if (nul != std::string::npos) out.resize(nul);

    // UE4 的 FNameEntry 里对象名可能带 "_数字" 后缀，保留原样
    return out;
}

std::string Reader::objectName(uint64_t obj) const {
    if (!mem_.isPtr(obj)) return {};
    FName n = mem_.readT<FName>(obj + off_.UObject_Name);
    return resolveFName(n);
}

uint64_t Reader::objectClass(uint64_t obj) const {
    if (!mem_.isPtr(obj)) return 0;
    uint64_t cls = mem_.readT<uint64_t>(obj + off_.UObject_Class);
    return mem_.isPtr(cls) ? cls : 0;
}

std::string Reader::className(uint64_t obj) const {
    uint64_t cls = objectClass(obj);
    if (!cls) return {};
    return objectName(cls);
}

bool Reader::validObject(uint64_t obj) const {
    if (!mem_.isPtr(obj)) return false;
    // vtable 必须是可读指针
    uint64_t vt = mem_.readT<uint64_t>(obj);
    if (!mem_.isPtr(vt)) return false;
    // 类指针必须有效，且类名可解析出可打印字符
    uint64_t cls = objectClass(obj);
    if (!cls) return false;
    FName n = mem_.readT<FName>(cls + off_.UObject_Name);
    if (n.index == 0 || n.index < 0) return false;
    // index 上限：超过 1<<24 基本是垃圾
    if ((uint32_t)n.index > (1u << 24)) return false;
    return true;
}

// ---------------------------------------------------------------- 结构读取

bool Reader::readVec3(uint64_t addr, Vec3& out) const {
    return mem_.read(addr, &out, sizeof(Vec3));
}

bool Reader::readRot3(uint64_t addr, Rot3& out) const {
    return mem_.read(addr, &out, sizeof(Rot3));
}

bool Reader::readTransform(uint64_t addr, FTransform& out) const {
    return mem_.read(addr, &out, sizeof(FTransform));
}

bool Reader::componentWorldPos(uint64_t component, Vec3& out) const {
    if (!mem_.isPtr(component)) return false;

    // 首选 ComponentToWorld.Translation（世界坐标，不受父级变换影响）
    FTransform t{};
    if (readTransform(component + off_.Scene_ComponentToWorld, t)) {
        Vec3 v{t.trans[0], t.trans[1], t.trans[2]};
        // 过滤明显非法值（NaN / 超大）
        if (std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)) {
            if (fabsf(v.x) < 1e7f && fabsf(v.y) < 1e7f && fabsf(v.z) < 1e7f) {
                out = v;
                return true;
            }
        }
    }

    // 回退 RelativeLocation
    Vec3 rel{};
    if (readVec3(component + off_.Scene_RelativeLocation, rel)) {
        if (std::isfinite(rel.x) && std::isfinite(rel.y) && std::isfinite(rel.z)) {
            out = rel;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- 相机

bool Reader::getCamera(uint64_t pc, FMinimalViewInfo& out) const {
    if (!mem_.isPtr(pc)) return false;
    if (!off_.PC_PlayerCameraManager || !off_.PCM_CameraCache) return false;

    uint64_t camMgr = mem_.readT<uint64_t>(pc + off_.PC_PlayerCameraManager);
    if (!mem_.isPtr(camMgr)) return false;

    uint64_t povAddr = camMgr + off_.PCM_CameraCache + off_.Cache_POV;
    FMinimalViewInfo v{};
    if (!mem_.read(povAddr, &v, sizeof(v))) return false;

    // 合理性校验：FOV 必须在 (10, 170) 之间，坐标必须有限
    if (!std::isfinite(v.fov) || v.fov < 10.f || v.fov > 170.f) return false;
    if (!std::isfinite(v.location.x) || !std::isfinite(v.location.y) ||
        !std::isfinite(v.location.z)) return false;
    if (fabsf(v.location.x) > 1e7f || fabsf(v.location.y) > 1e7f) return false;

    out = v;
    return true;
}

// ---------------------------------------------------------------- 数学

void rotatorToAxes(const Rot3& r, Vec3& fwd, Vec3& right, Vec3& up) {
    const float SP = sinf(r.pitch * kPi / 180.f), CP = cosf(r.pitch * kPi / 180.f);
    const float SY = sinf(r.yaw   * kPi / 180.f), CY = cosf(r.yaw   * kPi / 180.f);
    const float SR = sinf(r.roll  * kPi / 180.f), CR = cosf(r.roll  * kPi / 180.f);

    // UE4 FRotationMatrix 的三行，对应世界空间的 X/Y/Z 轴
    fwd.x   = CP * CY;
    fwd.y   = CP * SY;
    fwd.z   = SP;

    right.x = SR * SP * CY - CR * SY;
    right.y = SR * SP * SY + CR * CY;
    right.z = -SR * CP;

    up.x    = -(CR * SP * CY + SR * SY);
    up.y    = CY * SR - CR * SP * SY;
    up.z    = CR * CP;
}

bool worldToScreen(const Vec3& world, const FMinimalViewInfo& cam,
                   int screenW, int screenH, Vec2& out) {
    if (screenW <= 0 || screenH <= 0) return false;

    Vec3 fwd, right, up;
    rotatorToAxes(cam.rotation, fwd, right, up);

    Vec3 delta = world - cam.location;

    // 相机空间：x=前, y=右, z=上
    float cx = delta.dot(fwd);
    float cy = delta.dot(right);
    float cz = delta.dot(up);

    // 在相机背后或太近
    if (cx < 1.0f) return false;

    // 针孔相机：focal = (W/2) / tan(hFov/2)
    float halfFovRad = cam.fov * kPi / 360.f;
    float tanHalf = tanf(halfFovRad);
    if (tanHalf <= 1e-6f) return false;

    float focal = (screenW * 0.5f) / tanHalf;

    out.x = screenW * 0.5f + (cy * focal) / cx;
    out.y = screenH * 0.5f - (cz * focal) / cx;

    if (!std::isfinite(out.x) || !std::isfinite(out.y)) return false;
    return true;
}

} // namespace gy::ue4
