/**
 * ================================================================
 *  MiniOS 定时器模块
 *
 *  模拟硬件定时器中断。
 *  每次 timer_tick() 调用代表一个时钟滴答。
 *
 *  改进点：
 *   - timer_tick() 调用 sched_tick() 实现抢占式调度
 *   - 新增 timer_start_periodic() 用于周期性触发（shell 演示用）
 * ================================================================ */

#include <stdio.h>
#include "console.h"
#include "trap.h"
#include "timer.h"
#include "sched.h"

static int ticks = 0;

void timer_init(void) {
    ticks = 0;
    printk("[MiniOS] timer init ok\n");
}

/**
 * timer_tick — 时钟中断模拟
 *
 * 每个滴答：
 *   1. ticks 计数器递增
 *   2. 触发陷阱处理（记录中断）
 *   3. ★ 通知调度器 —— 这是抢占式多任务的核心
 *
 * 调用者：shell 的命令循环或其他模拟中断源。
 */
void timer_tick(void) {
    ticks++;

    /* 周期性输出（每 5 个滴答输出一次，避免刷屏） */
    if (ticks % 5 == 0) {
        printf("[timer] tick %d\n", ticks);
    }

    trap_handler(TRAP_TIMER, "timer tick");

    /*
     * 通知调度器：一个时钟滴答已过去。
     * 调度器会递减当前进程的时间片，必要时触发抢占。
     */
    sched_tick();
}

int timer_get_ticks(void) {
    return ticks;
}
