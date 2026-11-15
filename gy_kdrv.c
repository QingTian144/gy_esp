/* SPDX-License-Identifier: GPL-2.0 */
/*
 * gy_kdrv.c —— Android 内核态跨进程内存访问模块
 *
 * 设计要点：
 *   1. 不使用 process_vm_readv / ptrace，改走内核 access_process_vm，
 *      用户态不产生任何可被 hook 的调用。
 *   2. access_process_vm 不用 extern 硬链（多数 Android 内核不导出），
 *      改用 register_kprobe 解析符号地址，4.19 ~ 6.6 通用。
 *   3. 加载后从 modules 链表摘除，/proc/modules 不可见；
 *      同时改写 THIS_MODULE->name 规避内核内存特征扫描。
 *
 * 编译：需要目标设备 uname -r 一致的内核源码树，详见 Makefile
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/pid.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/version.h>
#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/vmalloc.h>
#include <linux/cred.h>
#include <linux/uidgid.h>
#include <linux/rcupdate.h>
#include <linux/dcache.h>
#include <linux/capability.h>

#include "gy_kdrv.h"

/* ================================================================
 * 版本兼容层
 * ================================================================ */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
#  define GY_MMAP_READ_LOCK(mm)    down_read(&(mm)->mmap_lock)
#  define GY_MMAP_READ_UNLOCK(mm)  up_read(&(mm)->mmap_lock)
#else
#  define GY_MMAP_READ_LOCK(mm)    down_read(&(mm)->mmap_sem)
#  define GY_MMAP_READ_UNLOCK(mm)  up_read(&(mm)->mmap_sem)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
#  define GY_MODNAME_MAX  sizeof(((struct module *)0)->name)
#else
#  define GY_MODNAME_MAX  MODULE_NAME_LEN
#endif

/* ================================================================
 * 全局
 * ================================================================ */

typedef int (*gy_apv_fn)(struct task_struct *tsk, unsigned long addr,
                         void *buf, int len, unsigned int gup_flags);

static gy_apv_fn  g_apv;              /* access_process_vm */
static int        g_ready;
static struct list_head *g_prev_list; /* 原始链表位置，卸载时恢复 */

/* ================================================================
 * 符号解析：kprobe 路径
 *
 * register_kprobe 内部通过 kallsyms 解析 symbol_name，因此即使
 * kallsyms_lookup_name 未被导出（Android GKI 5.7+ 默认如此），
 * 也能拿到任意非黑名单符号的地址。
 * ================================================================ */

static int gy_kp_pre(struct kprobe *p, struct pt_regs *regs)
{
    return 0;
}

static unsigned long gy_lookup_symbol(const char *name)
{
    struct kprobe kp;
    unsigned long addr = 0;
    int ret;

    if (!name || !*name)
        return 0;

    memset(&kp, 0, sizeof(kp));
    kp.symbol_name = name;
    kp.pre_handler = gy_kp_pre;

    ret = register_kprobe(&kp);
    if (ret < 0)
        return 0;

    addr = (unsigned long)kp.addr;

    /* 必须注销，否则目标函数每次执行都会走一遍 kprobe 处理链 */
    unregister_kprobe(&kp);

    return addr;
}

static int gy_resolve_symbols(void)
{
    g_apv = (gy_apv_fn)gy_lookup_symbol("access_process_vm");

    if (!g_apv) {
        /* 某些厂商内核导出了 kallsyms_lookup_name，作为二级兜底 */
        unsigned long kln = gy_lookup_symbol("kallsyms_lookup_name");
        if (kln) {
            typedef unsigned long (*kln_fn)(const char *);
            kln_fn fn = (kln_fn)kln;
            g_apv = (gy_apv_fn)fn("access_process_vm");
        }
    }

    if (!g_apv)
        return -ENOENT;

    return 0;
}

/* ================================================================
 * task 获取
 * ================================================================ */

static struct task_struct *gy_get_task(u32 pid)
{
    struct task_struct *task = NULL;
    struct pid *vpid;

    if (pid == 0)
        return NULL;

    rcu_read_lock();
    vpid = find_vpid((pid_t)pid);
    if (vpid)
        task = pid_task(vpid, PIDTYPE_PID);
    if (task)
        get_task_struct(task);
    rcu_read_unlock();

    return task;
}

/* ================================================================
 * 跨进程读写
 * ================================================================ */

static int gy_read_proc(u32 pid, u64 addr, void *kbuf, u32 size)
{
    struct task_struct *task;
    int ret;

    if (!g_apv)
        return -ENOSYS;

    task = gy_get_task(pid);
    if (!task)
        return -ESRCH;

    ret = g_apv(task, (unsigned long)addr, kbuf, (int)size, 0);
    put_task_struct(task);

    return (ret == (int)size) ? 0 : -EFAULT;
}

static int gy_write_proc(u32 pid, u64 addr, void *kbuf, u32 size)
{
    struct task_struct *task;
    int ret;

    if (!g_apv)
        return -ENOSYS;

    task = gy_get_task(pid);
    if (!task)
        return -ESRCH;

    ret = g_apv(task, (unsigned long)addr, kbuf, (int)size, FOLL_WRITE);
    put_task_struct(task);

    return (ret == (int)size) ? 0 : -EFAULT;
}

/* ================================================================
 * 模块基址：遍历 mm->mmap / maple tree
 * ================================================================ */

static u64 gy_module_base(u32 pid, const char *name)
{
    struct task_struct *task;
    struct mm_struct *mm;
    struct vm_area_struct *vma;
    u64 base = 0;

    task = gy_get_task(pid);
    if (!task)
        return 0;

    mm = get_task_mm(task);
    put_task_struct(task);

    if (!mm)
        return 0;

    GY_MMAP_READ_LOCK(mm);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
    {
        VMA_ITERATOR(vmi, mm, 0);
        for_each_vma(vmi, vma) {
            const char *fn;

            if (!vma->vm_file)
                continue;

            fn = vma->vm_file->f_path.dentry->d_name.name;
            if (!fn || !strstr(fn, name))
                continue;

            if (!base || vma->vm_start < base)
                base = vma->vm_start;
        }
    }
#else
    for (vma = mm->mmap; vma; vma = vma->vm_next) {
        const char *fn;

        if (!vma->vm_file)
            continue;

        fn = vma->vm_file->f_path.dentry->d_name.name;
        if (!fn || !strstr(fn, name))
            continue;

        if (!base || vma->vm_start < base)
            base = vma->vm_start;
    }
#endif

    GY_MMAP_READ_UNLOCK(mm);
    mmput(mm);

    return base;
}

/* ================================================================
 * 进程存活检查
 * ================================================================ */

static int gy_probe_pid(u32 pid)
{
    struct task_struct *task;
    struct mm_struct *mm;
    int ok = 0;

    task = gy_get_task(pid);
    if (!task)
        return -ESRCH;

    mm = get_task_mm(task);
    put_task_struct(task);

    if (!mm)
        return -EINVAL;

    ok = 1;
    mmput(mm);

    return ok ? 0 : -EINVAL;
}

/* ================================================================
 * LKM 自隐藏
 * ================================================================ */

static void gy_hide_self(void)
{
    /* 1. 从全局 modules 链表摘除 —— /proc/modules 立即消失 */
    if (!list_empty(&THIS_MODULE->list)) {
        g_prev_list = THIS_MODULE->list.prev;
        list_del_init(&THIS_MODULE->list);
    }

    /* 2. 从 sysfs 摘除 —— /sys/module/<name> 不再出现 */
    if (THIS_MODULE->mkobj.kobj.sd) {
        kobject_del(&THIS_MODULE->mkobj.kobj);
        THIS_MODULE->mkobj.kobj.sd = NULL;
    }

    /* 3. 改写模块名，规避内核内存字符串扫描 */
    memset(THIS_MODULE->name, 0, GY_MODNAME_MAX);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    strscpy(THIS_MODULE->name, "nf_nat_helper", GY_MODNAME_MAX);
#else
    strncpy(THIS_MODULE->name, "nf_nat", GY_MODNAME_MAX - 1);
#endif
}

static void gy_show_self(void)
{
    if (g_prev_list && list_empty(&THIS_MODULE->list)) {
        list_add(&THIS_MODULE->list, g_prev_list);
        g_prev_list = NULL;
    }
}

/* ================================================================
 * ioctl
 * ================================================================ */

static long gy_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct gy_req req;
    void *kbuf = NULL;
    long ret = 0;

    if (!g_ready)
        return -EAGAIN;

    if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
        return -EFAULT;

    if (req.size > GY_MAX_XFER)
        return -EINVAL;

    req.name[GY_MAX_NAME - 1] = '\0';

    if (req.size) {
        kbuf = kzalloc(req.size, GFP_KERNEL);
        if (!kbuf)
            return -ENOMEM;
    }

    switch (cmd) {

    case GY_KREAD:
        if (!kbuf) { ret = -EINVAL; break; }
        ret = gy_read_proc(req.pid, req.addr, kbuf, req.size);
        if (ret == 0) {
            if (copy_to_user((void __user *)(uintptr_t)req.ubuf, kbuf, req.size))
                ret = -EFAULT;
            else
                req.result = req.size;
        }
        break;

    case GY_KWRITE:
        if (!kbuf) { ret = -EINVAL; break; }
        if (copy_from_user(kbuf, (void __user *)(uintptr_t)req.ubuf, req.size)) {
            ret = -EFAULT;
            break;
        }
        ret = gy_write_proc(req.pid, req.addr, kbuf, req.size);
        if (ret == 0)
            req.result = req.size;
        break;

    case GY_MODBASE:
        req.result64 = gy_module_base(req.pid, (const char *)req.name);
        req.result   = (u32)req.result64;
        if (!req.result64)
            ret = -ENOENT;
        break;

    case GY_KSYM:
        req.result64 = (u64)gy_lookup_symbol((const char *)req.name);
        req.result   = (u32)req.result64;
        if (!req.result64)
            ret = -ENOENT;
        break;

    case GY_KMEM_READ:
        /* 只允许读内核空间地址，防止被用来读用户态制造混乱 */
        if (!kbuf) { ret = -EINVAL; break; }
        if (req.addr < 0xFFFF000000000000ULL && req.addr < 0x40000000ULL) {
            ret = -EINVAL;
            break;
        }
        if (!virt_addr_valid((void *)(uintptr_t)req.addr) &&
            !is_vmalloc_addr((void *)(uintptr_t)req.addr)) {
            ret = -EFAULT;
            break;
        }
        memcpy(kbuf, (const void *)(uintptr_t)req.addr, req.size);
        if (copy_to_user((void __user *)(uintptr_t)req.ubuf, kbuf, req.size))
            ret = -EFAULT;
        else
            req.result = req.size;
        break;

    case GY_PROBE:
        ret = gy_probe_pid(req.pid);
        req.result = (ret == 0) ? 1 : 0;
        break;

    case GY_VERSION:
        req.result   = GY_DRV_VERSION;
        req.result64 = GY_DRV_VERSION;
        break;

    default:
        ret = -ENOTTY;
        break;
    }

    kfree(kbuf);

    if (copy_to_user((void __user *)arg, &req, sizeof(req)))
        ret = -EFAULT;

    return ret;
}

static int gy_open(struct inode *inode, struct file *file)
{
    /* 只有 root / CAP_SYS_ADMIN 能打开设备 */
    if (!capable(CAP_SYS_ADMIN) && current_uid().val != 0)
        return -EPERM;
    return 0;
}

static int gy_release(struct inode *inode, struct file *file)
{
    return 0;
}

static const struct file_operations gy_fops = {
    .owner          = THIS_MODULE,
    .open           = gy_open,
    .release        = gy_release,
    .unlocked_ioctl = gy_ioctl,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
    .compat_ioctl   = gy_ioctl,
#endif
};

static struct miscdevice gy_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = GY_DEV_NAME,
    .fops  = &gy_fops,
    .mode  = 0600,
};

/* ================================================================
 * 模块入口
 * ================================================================ */

static int __init gy_init(void)
{
    int ret;

    ret = gy_resolve_symbols();
    if (ret)
        return ret;

    ret = misc_register(&gy_misc);
    if (ret) {
        g_apv = NULL;
        return ret;
    }

    g_ready = 1;

    /* 注册完成后再隐藏，避免注册过程出错时留下孤儿 */
    gy_hide_self();

    return 0;
}

static void __exit gy_exit(void)
{
    g_ready = 0;

    gy_show_self();          /* 恢复链表，让 module core 能正常清理 */
    misc_deregister(&gy_misc);

    g_apv = NULL;
}

module_init(gy_init);
module_exit(gy_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("gy");
MODULE_DESCRIPTION("cross-process memory access");
MODULE_VERSION("1.1");
