#ifndef MINIOS_SYNC_H
#define MINIOS_SYNC_H

#include "process.h"

/**
 * ================================================================
 *  MiniOS 进程同步原语
 *
 *  信号量 (Semaphore)  — Dijkstra 的 P/V 操作
 *  互斥锁 (Mutex)     — 二进制信号量的特化
 *
 *  底层依赖: process_block() / process_wakeup()
 * ================================================================ */

/* ── 信号量 ── */
typedef struct semaphore {
    int value;                          /* 信号量计数值 */
    int wait_queue[MAX_PROCESSES];      /* 等待此信号量的 PID 列表 */
    int wait_count;                     /* 等待进程数 */
} sem_t;

void sem_init(sem_t *sem, int initial_value);
void sem_wait(sem_t *sem);             /* P 操作 — 申请资源 */
void sem_post(sem_t *sem);             /* V 操作 — 释放资源 */
int  sem_get_value(sem_t *sem);        /* 查询当前值 */

/* ── 互斥锁 ── */
typedef struct mutex {
    int locked;                         /* 0=解锁, 1=已锁 */
    int owner_pid;                      /* 持有者 PID (-1 表示无) */
    int wait_queue[MAX_PROCESSES];
    int wait_count;
} mutex_t;

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);
int  mutex_trylock(mutex_t *m);        /* 尝试加锁，失败不阻塞 */

#endif
