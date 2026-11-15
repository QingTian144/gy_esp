# gy_esp — 和平精英单透视

外挂式 ESP（只画透视，不含自瞄）。内存读取走自研内核模块，绕过用户态检测面。

---

## 0. 架构

```
┌──────────────────────────────────────────┐
│  和平精英  com.tencent.tmgp.pubgmhd       │
│  ├─ libUE4.so   (UWorld / Actors / Camera)│
│  └─ libanogs.so + ACE 内核组件             │
└───────────────┬──────────────────────────┘
                │ access_process_vm (内核态，无 syscall)
┌───────────────▼──────────────────────────┐
│  gy_kdrv.ko  内核模块                     │
│  ├─ /dev/gyk  ioctl 接口                  │
│  ├─ kprobe 动态解析符号                   │
│  └─ 加载后自动隐藏（list_del + 改名）      │
└───────────────┬──────────────────────────┘
                │ ioctl(KREAD)
┌───────────────▼──────────────────────────┐
│  sysmon APK                               │
│  ├─ dump.cpp   运行时自动发现偏移          │
│  ├─ esp.cpp    遍历玩家、算屏幕坐标        │
│  └─ overlay.cpp 自绘 GLES2 → 透明悬浮窗    │
└──────────────────────────────────────────┘
```

两个关键设计选择：

1. **不用 `process_vm_readv`**：那是用户态 syscall，ACE 内核组件能监控调用栈。
   走内核 `access_process_vm` 后，用户态没有可被 hook 的内存读取调用。
2. **不用 ImGui**：`ImGui` 相关字符串和结构布局是内存扫描的高命中特征。
   自绘只依赖 GLES2 基础 API，内存里没有第三方框架指纹。

---

## 1. 目录结构

```
gy_esp/
├── app/                              Android 应用
│   ├── build.gradle
│   └── src/main/
│       ├── AndroidManifest.xml
│       ├── java/com/gy/esp/
│       │   ├── MainActivity.java     控制面板
│       │   ├── OverlayService.java   透明悬浮窗
│       │   └── NativeBridge.java     JNI 声明
│       └── cpp/
│           ├── CMakeLists.txt
│           ├── main.cpp              JNI 入口 + 渲染循环
│           ├── mem.*                 跨进程内存读写（用户态兜底）
│           ├── kdrv.*                内核模块 ioctl 封装
│           ├── ue4.*                 UE4 对象系统 + W2S
│           ├── dump.*                偏移运行时自动发现
│           ├── esp.*                 玩家遍历与透视数据
│           ├── overlay.*             GLES2 自绘渲染器
│           ├── test_mem.cpp          内存层自测
│           └── test_math.cpp         W2S 数学校验
├── kernel/                           内核模块
│   ├── gy_kdrv.c
│   ├── gy_kdrv.h                     与用户态共享的 ioctl 协议
│   └── Makefile
└── tools/
    ├── build_driver.sh
    └── deploy.sh
```

---

## 2. 编译

### 2.1 应用

```bash
cd gy_esp
export ANDROID_HOME=~/Android/Sdk
export ANDROID_NDK_HOME=$ANDROID_HOME/ndk/26.1.10909125

./gradlew assembleRelease
# 产物 app/build/outputs/apk/release/app-release.apk
```

要求：Gradle 8.x、AGP 8.2、JDK 17、NDK r26。

### 2.2 内核模块 — 这是整个项目最麻烦的一步

模块对内核版本**极其敏感**。`uname -r` 必须与源码树完全一致，否则
`insmod` 会报 `Invalid module format` 或 `Exec format error`。

**第一步：读设备信息**

```bash
adb shell uname -r
# 例：4.19.157-perf-g2f8a9c1b2d3

adb shell "su -c 'zcat /proc/config.gz'" > device.config
grep -E "CONFIG_MODULE_SIG|CONFIG_MODVERSIONS|CONFIG_KPROBES" device.config
```

三个关键项：

| 配置 | 值 | 含义 |
|---|---|---|
| `CONFIG_MODULE_SIG_FORCE` | `y` | 强制签名，未签名模块无法加载，需要 patch 内核或用 KernelSU |
| `CONFIG_MODVERSIONS` | `y` | 需要匹配 `Module.symvers` |
| `CONFIG_KPROBES` | `y` | 驱动依赖 kprobe 解析符号，必须开启 |

**第二步：拿到匹配的内核源码**

优先级从高到低：

1. **厂商开源仓库**（最准）
   - 小米 `github.com/MiCode/Xiaomi_Kernel_OpenSource`，按分支名找机型
   - 一加 `github.com/OnePlusOSS/android_kernel_*`
   - 三星 `opensource.samsung.com`
   - 真我/OPPO `github.com/realme-kernel-opensource`

2. **GKI 通用内核**（Android 12+ 多数机型）
   ```bash
   git clone https://android.googlesource.com/kernel/common -b android13-5.15
   ```
   分支对照：Android 12→`android12-5.10`，13→`android13-5.15`，14→`android14-6.1`

3. **设备上直接扒**（部分机型）
   ```bash
   adb shell "su -c 'dd if=/dev/block/by-name/boot of=/sdcard/boot.img'"
   # 解包 boot.img 拿 kernel，再提取 IKCONFIG
   ```

**第三步：编译**

```bash
export NDK=~/Android/Sdk/ndk/26.1.10909125
export TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64

# 准备内核树
cd /path/to/kernel
make ARCH=arm64 O=out gki_defconfig      # 或厂商 defconfig
make ARCH=arm64 O=out modules_prepare

# 编译模块
cd /path/to/gy_esp/kernel
make KDIR=/path/to/kernel TC=$TC
# 产物 gy_kdrv.ko
```

用 `tools/build_driver.sh` 可以一条命令跑完，参数在脚本头部。

**第四步：签名问题**

如果设备 `CONFIG_MODULE_SIG_FORCE=y`，有两条路：

- 用 **KernelSU** 或 **APatch** 刷入内核，它们 patch 掉了模块验证
- 从设备的 vendor_boot 里提取签名密钥（基本不可能，除非是开发者机型）

绝大多数零售机的 `FORCE` 是 `n`，`modprobe`/`insmod` 能直接加载未签名模块。

### 2.3 编译不出来怎么办 — 用市面现成方案

自己编译失败（源码找不到、版本对不上、签名过不去）时，按这个顺序退：

| 方案 | 说明 | 适用 |
|---|---|---|
| **APatch / KernelPatch** | 内核级 hook 框架，不编译 `.ko`，直接加载 `.kpm` 模块。支 `kallsyms_lookup_name` 和 `inline_hook` | 最推荐，绕开编译问题 |
| **KernelSU / SukiSU-Ultra / KernelSU-Next** | 内核 root 方案，内置模块加载器，自带隐藏 | 已有对应机型 boot 镜像时 |
| **Magisk + Zygisk + Shamiko** | 用户态 root，配 `process_vm_readv` 兜底路径 | 不刷内核时的最低配 |
| **现成 kread 模块** | GitHub 搜 `<机型代号> kread ko` 或 `<kernel version> kread module` | 热门机型常有人编译好 |

用 APatch 时，把 `gy_kdrv.c` 改写成 `.kpm` 模块即可，接口换成
`kpatch` 的 `kp_kallsyms_lookup_name`，逻辑不用动。

驱动打不开（`/dev/gyk` 不存在）时，程序会自动退回
`process_vm_readv` 路径——透视功能不受影响，只是隐蔽性下降。

---

## 3. 部署

```bash
# 1. 装 APK
adb install -r app-release.apk

# 2. 推模块并加载
adb push kernel/gy_kdrv.ko /data/local/tmp/
adb shell "su -c 'insmod /data/local/tmp/gy_kdrv.ko'"

# 3. 确认设备节点
adb shell "su -c 'ls -lZ /dev/gyk'"
# crw------- root root u:object_r:device:s0 /dev/gyk

# 4. 确认模块已隐藏
adb shell "su -c 'cat /proc/modules | grep -i gy'"
# 应该没有任何输出

# 5. 授权并启动
adb shell "su -c 'appops set com.gy.esp SYSTEM_ALERT_WINDOW allow'"
adb shell "am start -n com.gy.esp/.MainActivity"
```

App 里点 START，返回游戏即可。

---

## 4. 偏移维护

**不需要手工维护偏移。** `dump.cpp` 在启动时自动发现：

| 目标 | 发现方法 |
|---|---|
| `GNames` | 扫 `libUE4.so` 可写段，找 `+0x10` 指向的内存首条 `FNameEntry` 为 `"None"` 的结构。UE4 的 `FNamePool` 第 0 号恒为 `"None"`，这是最强特征 |
| `GWorld` | 扫同一批段里的指针，验证其 `+0x10` 指向的类名可解析为 `World` |
| `UWorld::PersistentLevel` | 遍历 `+0x20..+0x100`，找类名为 `Level` 且内部含合法 `Actors` 数组的对象 |
| `ULevel::Actors` | 遍历 `+0x40..+0x300`，找 `{可读指针, 1<count<200000, max>=count}` 的 TArray |
| `AActor::RootComponent` | 在多个 Actor 上统计哪个偏移指向类名含 `Component` 且能读出合理坐标的对象 |
| 相机链 | 从 `PlayerController` 出发，找 `CameraManager`，再在 `+0x100..0xF00` 里按 `FOV∈(10,170)` 且坐标有限来定位 `CameraCache` |

启动日志里会打印全部结果，App 界面下方也能看到。版本大更新后如果某一步失败，
日志会指明卡在哪一环，针对性调整阈值即可。

`esp.h` 的 `EspConfig` 里保留了骨骼索引配置项，骨骼渲染接口
（`Esp::readSkeleton`）已留空——需要 `USkeletalMeshComponent` →
`ComponentSpaceTransforms` 的偏移确认后再填。

---

## 5. 关于 ACE

| 检测面 | 本项目对策 |
|---|---|
| `/proc/<pid>/mem` 访问 | 不使用 |
| `process_vm_readv` 调用栈 | 不使用（有内核模块时） |
| `ptrace` attach | 不使用 |
| `/proc/modules` | 加载后 `list_del` 摘除 |
| `/sys/module/<name>` | `kobject_del` 移除 |
| 内核内存字符串扫描 | 模块名运行时改写为 `nf_nat` |
| `ImGui` 特征 | 不使用，自绘渲染器 |
| 悬浮窗检测 | 无法完全规避，见下 |
| Root / Magisk 痕迹 | 需要 Shamiko + Zygisk 隐藏 |

**悬浮窗是目前最大的暴露面。** `TYPE_APPLICATION_OVERLAY` 窗口在
`dumpsys window` 里可见，ACE 会扫。可选的对策：

- 用 `TYPE_ACCESSIBILITY_OVERLAY`（需要无障碍服务，检测优先级较低）
- 用 SurfaceFlinger 层直接叠加（需要更高权限）
- 接受这个风险

另外建议改包名和类名：把 `com.gy.esp` 换成中性名字，同步修改
`main.cpp` 里所有 `Java_com_gy_esp_NativeBridge_*` 符号，
或者改用 `RegisterNatives` 动态注册（改包名不用改 C++）。

---

## 6. 排错

| 现象 | 原因 | 处理 |
|---|---|---|
| `insmod: Invalid module format` | 内核版本不匹配 | `uname -r` 与源码树必须完全一致 |
| `insmod: Operation not permitted` | 模块签名强制 | 见 2.2 第四步 |
| `/dev/gyk` 不存在但模块已加载 | `misc_register` 失败 | `dmesg | grep -i gyk` 看错误 |
| `attach failed` | 进程名不对 | 确认游戏包名，改 `main.cpp` 的 `kGamePackage` |
| `GNames 未找到` | 扫描范围不够 / 结构变了 | 调大 `dump.cpp` 的 `kMaxScanPerSeg` |
| `GWorld 未找到` | 同上 | 同上，或先确认 `GNames` 已找到 |
| 透视框位置偏移 | 相机 FOV 或 W2S 参数不对 | 看 `test_math.cpp` 的期望值校准 |
| 玩家全部不显示 | `RootComponent` 偏移错 | 看启动日志的 `AActor::RootComponent` 行 |
| 悬浮窗不透明 | EGL 配置缺 ALPHA | 确认 `EGL_ALPHA_SIZE=8` 且 `glClearColor(0,0,0,0)` |

---

## 7. 自测

两个 host 侧测试程序，不依赖设备：

```bash
cd app/src/main/cpp

# W2S 数学：31 项用例
g++ -std=c++17 -O2 test_math.cpp ue4.cpp mem.cpp -o /tmp/tm && /tmp/tm

# 内存层：以宿主进程自身为目标
g++ -std=c++17 -O2 test_mem.cpp mem.cpp -o /tmp/tmem && /tmp/tmem
```

改完 `ue4.cpp` 的投影公式或 `mem.cpp` 的读取逻辑后，先跑这两个，
能挡住大部分低级错误。

---

## 8. 许可

仅供本地环境研究与学习使用。
