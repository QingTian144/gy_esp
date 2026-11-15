# ============================================================
# gy_kdrv —— Android 内核模块构建
#
# 必需条件：
#   1. 目标设备内核源码树（版本必须与设备 uname -r 一致）
#   2. 该源码树已执行过配置，存在 include/config/auto.conf
#   3. 若内核开了 CONFIG_MODVERSIONS，需要对应的 Module.symvers
#
# 用法：
#   make KDIR=~/kernel/msm-4.19 TC=~/android-ndk/toolchains/llvm/prebuilt/linux-x86_64
# ============================================================

obj-m := gy_kdrv.o

# ---- 由外部传入 ----
KDIR          ?= $(error 请指定 KDIR=<内核源码树路径>)
TC            ?= $(error 请指定 TC=<NDK toolchain 目录>)
ARCH          ?= arm64
CROSS_COMPILE ?= $(TC)/bin/aarch64-linux-gnu-

# Android 内核必须用 clang + LLVM 工具链
CC            := $(TC)/bin/clang
LD            := $(TC)/bin/ld.lld
AR            := $(TC)/bin/llvm-ar
NM            := $(TC)/bin/llvm-nm
OBJCOPY       := $(TC)/bin/llvm-objcopy
OBJDUMP       := $(TC)/bin/llvm-objdump
STRIP         := $(TC)/bin/llvm-strip
CLANG_TRIPLE  := aarch64-linux-gnu-

# GKI 内核通常需要这两个
LLVM            ?= 1
LLVM_IAS        ?= 1

MAKE_ARGS := -C $(KDIR) M=$(CURDIR) \
             ARCH=$(ARCH) \
             CROSS_COMPILE=$(CROSS_COMPILE) \
             CC=$(CC) LD=$(LD) AR=$(AR) NM=$(NM) \
             OBJCOPY=$(OBJCOPY) OBJDUMP=$(OBJDUMP) STRIP=$(STRIP) \
             CLANG_TRIPLE=$(CLANG_TRIPLE) \
             LLVM=$(LLVM) LLVM_IAS=$(LLVM_IAS)

.PHONY: all modules clean install info

all: modules

modules:
	@if [ ! -f "$(KDIR)/include/config/auto.conf" ]; then \
		echo "[-] $(KDIR)/include/config/auto.conf 不存在"; \
		echo "    先在内核树里跑一次配置："; \
		echo "    cd $(KDIR) && make ARCH=arm64 <defconfig> && make modules_prepare"; \
		exit 1; \
	fi
	$(MAKE) $(MAKE_ARGS) modules

clean:
	$(MAKE) $(MAKE_ARGS) clean

info:
	@echo "KDIR          = $(KDIR)"
	@echo "ARCH          = $(ARCH)"
	@echo "CROSS_COMPILE = $(CROSS_COMPILE)"
	@echo "CC            = $(CC)"
	@echo "--- 内核版本 ---"
	@if [ -f "$(KDIR)/include/config/kernel.release" ]; then \
		cat $(KDIR)/include/config/kernel.release; \
	else \
		echo "(未生成 kernel.release)"; \
	fi
	@echo "--- 设备实际版本（adbshell 侧执行 uname -r 比对）---"

install:
	@echo "[*] 推送到设备"
	adb push gy_kdrv.ko /data/local/tmp/
	@echo "[*] 加载（需要 root）"
	adb shell "su -c 'insmod /data/local/tmp/gy_kdrv.ko'"
	@echo "[*] 校验设备节点"
	adb shell "su -c 'ls -lZ /dev/gyk'"
