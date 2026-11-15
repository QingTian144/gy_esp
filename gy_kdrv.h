/* SPDX-License-Identifier: GPL-2.0 */
/*
 * gy_kdrv.h —— 内核模块与用户态共享的 ioctl 协议
 *
 * 设备节点 /dev/gyk
 * 所有请求走同一个 gy_req，用 cmd 区分动作。
 */
#ifndef _GY_KDRV_H
#define _GY_KDRV_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/ioctl.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
#endif

#define GY_DEV_NAME     "gyk"
#define GY_MAX_XFER     (4u * 1024u * 1024u)   /* 单次最大传输 4MB */
#define GY_MAX_NAME     128

/* ---- ioctl 命令 ---- */
#define GY_MAGIC        'G'

/* 读目标进程内存：addr 远程地址，ubuf 本地缓冲 */
#define GY_KREAD        _IOWR(GY_MAGIC, 1, struct gy_req)
/* 写目标进程内存 */
#define GY_KWRITE       _IOWR(GY_MAGIC, 2, struct gy_req)
/* 取目标进程某个 .so 的基址：ubuf 指向模块名，返回写入 result */
#define GY_MODBASE      _IOWR(GY_MAGIC, 3, struct gy_req)
/* 解析内核符号地址：ubuf 指向符号名，返回写入 result */
#define GY_KSYM         _IOWR(GY_MAGIC, 4, struct gy_req)
/* 读内核虚拟地址（调试用，仅限已映射的内核空间） */
#define GY_KMEM_READ    _IOWR(GY_MAGIC, 5, struct gy_req)
/* 查询目标进程是否存活 + mm 是否可用 */
#define GY_PROBE        _IOWR(GY_MAGIC, 6, struct gy_req)
/* 驱动版本 */
#define GY_VERSION      _IOR(GY_MAGIC, 7, struct gy_req)

#define GY_DRV_VERSION  0x00010002u

struct gy_req {
    uint32_t pid;       /* 目标 pid（内核符号/内核内存操作可填 0） */
    uint32_t flags;     /* 见 GY_F_* */
    uint64_t addr;      /* 远程地址 / 结果地址 */
    uint64_t ubuf;      /* 用户态缓冲区指针 */
    uint32_t size;      /* 长度 */
    uint32_t result;    /* 返回值：字节数 / 地址低 32 位 / 状态码 */
    uint64_t result64;  /* 返回值：64 位地址 */
    uint8_t  name[GY_MAX_NAME]; /* 模块名 / 符号名 */
};

/* flags */
#define GY_F_FORCE      0x1u   /* 忽略 pid 白名单 */
#define GY_F_NOCACHE    0x2u   /* 模块基址不吃缓存 */

#endif /* _GY_KDRV_H */
