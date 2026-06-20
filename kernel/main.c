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
 *   8. kernel_test — 自检（所有模块就绪后）
 *   9. ramfs       — 内存文件系统
 *  10. shell       — 交互式命令行（最后启动，进入主循环）
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
    idt_init();   /* 装 IDT + 初始化 PIC, 不开中断 */
#endif

    timer_init();
    syscall_init();
    memory_init();
    process_init();
    sched_init();

    /* 自检 (在 idle 上下文中运行) */
    kernel_test();

    ramfs_init();

#ifdef BUILD_QEMU
    /*
     * ★ 开启真上下文切换, 然后 idle yield → shell ★
     */
    extern void sched_enable_real_switch(void);
    sched_enable_real_switch();

    printk("[MiniOS] starting shell via real context switch...\n");
    extern void sched_add_process(int pid);
    sched_add_process(1);     /* shell 入队 */
    extern void sched_yield(void);
    sched_yield();            /* idle → shell (真 switch_to!) */
    /* idle 循环: shell 退出后或者无人可调度时在这里 */
    printk("[MiniOS] idle loop\n");
    while (1) { sched_yield(); }
#else
    /* 本地模式: 单线程, 直接调用 shell */
    shell_start();
#endif

    return 0;
}

int main(void) {
    return kernel_main(0, 0);  /* 本地模式: 无 Multiboot */
}
