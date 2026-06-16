#ifndef MINIOS_SCHED_H
#define MINIOS_SCHED_H

#include "process.h"

/**
 * ================================================================
 *  MiniOS 调度器 v2.0
 *
 *  算法：RR (轮转调度, 默认) / FCFS (先来先服务)
 *
 *  改进：
 *   1. 支持两种调度算法 (FCFS + RR)
 *   2. 时间片运行时配置 (默认 10 tick)
 *   3. 上下文切换计时 (clock() 精度)
 * ================================================================ */

/* 调度算法 */
typedef enum {
    SCHED_RR   = 0,   /* Round-Robin: 时间片抢占 */
    SCHED_FCFS = 1    /* First-Come-First-Served: 非抢占 */
} sched_algo_t;

/* 初始化 */
void sched_init(void);
void sched_add_process(int pid);
void sched_remove_process(int pid);

/* 核心调度 */
void schedule(void);

/* 时钟中断 */
void sched_tick(void);

/* 进程控制 */
void sched_yield(void);
void sched_block(void);
void sched_wakeup(int pid);

/* 算法切换 */
void sched_set_algo(sched_algo_t algo);
sched_algo_t sched_get_algo(void);
const char *sched_algo_name(void);

/* 时间片配置 */
void sched_set_time_slice(int ticks);
int  sched_get_time_slice(void);

/* 上下文切换计时 (微秒) */
double sched_get_avg_switch_time_us(void);
void   sched_reset_timing(void);

/* 上下文切换 — 寄存器级保存/恢复 ★ */
void switch_context(process_context_t *old_ctx,
                    process_context_t *new_ctx);

/* 查询 */
process_t *sched_get_current(void);
void sched_print_info(void);

#endif
