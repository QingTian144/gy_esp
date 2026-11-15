#!/bin/bash
# 编译 gy_kdrv.ko
#
# 用法:
#   KDIR=/path/to/kernel ./build_driver.sh
#   KDIR=/path/to/kernel NDK=/path/to/ndk ./build_driver.sh
set -e

KDIR="${KDIR:-}"
NDK="${NDK:-${ANDROID_NDK_HOME:-$ANDROID_HOME/ndk/26.1.10909125}}"
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
KERNEL_DIR="$(cd "$(dirname "$0")/../kernel" && pwd)"

if [ -z "$KDIR" ]; then
    echo "[-] 必须指定 KDIR=<内核源码树>"
    echo "    例: KDIR=~/kernel/msm-4.19 $0"
    exit 1
fi

if [ ! -f "$KDIR/include/config/auto.conf" ]; then
    echo "[-] $KDIR 还没配置过"
    echo "    先执行:"
    echo "      cd $KDIR"
    echo "      make ARCH=arm64 O=out gki_defconfig"
    echo "      make ARCH=arm64 O=out modules_prepare"
    exit 1
fi

if [ ! -x "$TC/bin/clang" ]; then
    echo "[-] 找不到 clang: $TC/bin/clang"
    echo "    设置 NDK=<Android NDK 路径>"
    exit 1
fi

echo "[*] KDIR = $KDIR"
echo "[*] TC   = $TC"
if [ -f "$KDIR/include/config/kernel.release" ]; then
    echo "[*] 内核 = $(cat "$KDIR/include/config/kernel.release")"
fi
echo "[*] 设备 = $(adb shell uname -r 2>/dev/null || echo '未连接')"
echo

cd "$KERNEL_DIR"
make KDIR="$KDIR" TC="$TC" modules

if [ -f gy_kdrv.ko ]; then
    echo
    echo "[+] 编译成功: $KERNEL_DIR/gy_kdrv.ko ($(stat -c%s gy_kdrv.ko) bytes)"
    echo "[+] file: $(file gy_kdrv.ko 2>/dev/null || true)"
else
    echo "[-] 编译失败，没有产出 .ko"
    exit 1
fi
