#!/bin/bash
# 推送并加载内核模块 + 安装 APK
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KO="$ROOT/kernel/gy_kdrv.ko"
APK="$ROOT/app/build/outputs/apk/release/app-release.apk"

echo "=== 设备检查 ==="
adb wait-for-device
echo "内核版本: $(adb shell uname -r)"
echo "架构    : $(adb shell getprop ro.product.cpu.abi)"
echo "设备    : $(adb shell getprop ro.product.model)"
echo

if [ ! -f "$KO" ]; then
    echo "[-] 找不到 $KO，先跑 build_driver.sh"
    exit 1
fi

echo "=== 加载内核模块 ==="
adb push "$KO" /data/local/tmp/gy_kdrv.ko
adb shell "su -c 'rmmod gy_kdrv 2>/dev/null; insmod /data/local/tmp/gy_kdrv.ko'" || {
    echo "[-] insmod 失败，看 dmesg:"
    adb shell "su -c 'dmesg | tail -30'"
    exit 1
}

echo "=== 校验设备节点 ==="
adb shell "su -c 'ls -lZ /dev/gyk'" || echo "[!] /dev/gyk 不存在"

echo "=== 校验模块已隐藏 ==="
HIDDEN=$(adb shell "su -c 'cat /proc/modules | grep -c gy_kdrv'" | tr -d '\r')
if [ "$HIDDEN" = "0" ]; then
    echo "[+] /proc/modules 中不可见"
else
    echo "[!] 模块仍可见，隐藏未生效"
fi

if [ -f "$APK" ]; then
    echo "=== 安装 APK ==="
    adb install -r "$APK"
    adb shell "appops set com.gy.esp SYSTEM_ALERT_WINDOW allow" 2>/dev/null || true
    echo "[+] 完成，手动启动 App 并点 START"
else
    echo "[!] 未找到 APK，跳过安装"
fi
