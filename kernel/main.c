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
#include "syscall.h"

#include "memory.h"
#include "process.h"
#include "sched.h"
#include "ramfs.h"
#include "shell.h"
#include "test.h"

int kernel_main(void) {
    /* ---- 第一阶段：基础框架 ---- */
    console_init();
    printk("[MiniOS] boot success\n");
    printk("[MiniOS] kernel_main start\n");

    trap_init();
    timer_init();
    syscall_init();

    /* ---- 第二阶段：内存与进程 ---- */
    memory_init();
    process_init();

    /* ---- 第三阶段：调度器（NEW!） ---- */
    sched_init();

    /* ---- 第四阶段：自检 ---- */
    kernel_test();

    /* ---- 第五阶段：文件系统与 Shell ---- */
    ramfs_init();
    shell_start();

    return 0;
}

int main(void) {
    return kernel_main();
}
