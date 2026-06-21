/**
 * ================================================================
 *  MiniOS 内核入口
 *
 *  初始化顺序（严格按依赖关系排列）：
 *   1. console     — 最早，后续模块需要输出
 *   2. trap        — 中断/异常处理框架
 *   3. timer       — 定时器（调度器的时钟源）
 *   4. syscall     — 系统调用框架
 *   5. memory      — 物理内存 + 虚拟内存
 *   6. process     — 进程管理（依赖内存分配）
 *   7. sched       — 调度器（依赖进程管理）
 *   8. kernel_test — 自检（本地模式自动执行，QEMU 模式通过 shell 手动执行）
 *   9. ramfs       — 内存文件系统
 *  10. shell       — 交互式命令行
 * ================================================================ */

#include "hal.h"
#include "console.h"
#include "trap.h"
#include "timer.h"

#ifdef BUILD_QEMU
extern void idt_init(void);
#endif

#include "syscall.h"
#include "memory.h"
#include "process.h"
#include "sched.h"
#include "ramfs.h"
#include "shell.h"
#include "test.h"

int kernel_main(unsigned long magic, unsigned long mbi) {
#ifndef BUILD_QEMU
    setbuf(stdout, NULL);  /* unbuffered for pipe mode */
#endif
    console_init();
    printk("[MiniOS] boot success\n");

#ifdef BUILD_QEMU
    {
        extern void memory_detect(unsigned long magic, unsigned long mbi);
        printf("[MiniOS] Multiboot: magic=%d mbi=%d\n",
               (int)magic, (int)(uintptr_t)mbi);
        memory_detect(magic, mbi);
    }
#endif

    printk("[MiniOS] kernel_main start\n");

    trap_init();

#ifdef BUILD_QEMU
    idt_init();   /* 装 IDT + 初始化 PIC, 暂不开中断 */
#endif

    timer_init();
    syscall_init();
    memory_init();
    process_init();
    sched_init();

#ifndef BUILD_QEMU
    /*
     * 本地模式：
     * 自动运行完整自检，方便在 VSCode / Windows 终端验证功能。
     */
    if (getenv("MINIOS_VIZ") == NULL)
        kernel_test();
    else
        printk("[MiniOS] VIZ mode: skip auto self-test\n");
#else
    /*
     * QEMU 模式：
     * 先不要开机自动跑完整自检。
     * 之前 QEMU 在调度性能测试 10.4 附近退出，导致无法进入 Shell。
     * 所以 QEMU 下优先进入交互式 Shell，后续通过 shell 的 test 命令手动运行自检。
     */
    printk("[MiniOS] QEMU mode: skip auto self-test, enter shell directly\n");
#endif

    ramfs_init();

    printk("[MiniOS] shell start\n");
    shell_start();

    /*
     * shell_start 正常情况下是主循环，不会返回。
     * 如果意外返回，则让内核停在这里，避免直接退出。
     */
    printk("[MiniOS] shell returned, halt\n");

    while (1) {
#ifdef BUILD_QEMU
        __asm__ volatile ("hlt");
#endif
    }

    return 0;
}

int main(void) {
    return kernel_main(0, 0);  /* 本地模式: 无 Multiboot */
}
