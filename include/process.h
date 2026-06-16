#ifndef MINIOS_PROCESS_H
#define MINIOS_PROCESS_H

#include <stdint.h>
#include "memory.h"       /* page_directory_t */

/* ================================================================
 * 进程管理常量
 * ================================================================ */
#define MAX_PROCESSES      16
#define PROCESS_NAME_LEN   32
#define DEFAULT_TIME_SLICE 10          /* 默认时间片（时钟滴答数） */

/* ================================================================
 * 进程状态枚举
 *
 * 状态转换图：
 *   UNUSED ──→ READY ──→ RUNNING ──→ ZOMBIE
 *                 ↑          │
 *                 │    BLOCKED ── (wakeup) → READY
 *                 └──────────┘
 * ================================================================ */
typedef enum {
    PROC_UNUSED  = 0,   /* 空槽位 */
    PROC_READY,         /* 就绪：等待调度 */
    PROC_RUNNING,       /* 运行中：正在占用 CPU */
    PROC_BLOCKED,       /* 阻塞：等待某事件（如 I/O） */
    PROC_ZOMBIE         /* 僵尸：已退出，等待父进程回收 */
} process_state_t;

/* ================================================================
 * 进程上下文 — 模拟 CPU 寄存器快照
 *
 * 真实 OS 中这些值保存在内核栈上；
 * MiniOS 中显式存储在 PCB 内，方便教学理解。
 * ================================================================ */
typedef struct process_context {
    uint32_t eip;       /* 指令指针 */
    uint32_t esp;       /* 栈指针 */
    uint32_t eflags;    /* 标志寄存器 */
    uint32_t eax;       /* 通用寄存器 */
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;       /* 帧指针 */
} process_context_t;

/* ================================================================
 * 进程控制块 (PCB)
 * ================================================================ */
typedef struct process {
    /* ---- 基本信息 ---- */
    int               pid;              /* 进程 ID */
    int               parent_pid;       /* 父进程 ID（-1 表示无父进程） */
    char              name[PROCESS_NAME_LEN];
    process_state_t   state;

    /* ---- 调度信息 ---- */
    int               time_slice;       /* 剩余时间片 */
    int               total_ticks;      /* 累计使用的 CPU 滴答数 */
    int               priority;         /* 优先级（0=最高，数值越大越低） */

    /* ---- 内存信息 ---- */
    void             *kernel_stack;     /* 内核栈（1 页） */
    page_directory_t *pdir;            /* 虚拟地址空间（页目录） */
    vm_area_t        *vma_list;        /* 用户态 VMA 链表 (缺页处理依据) */

    /* ---- 上下文 ---- */
    process_context_t context;          /* 保存的 CPU 寄存器 */

    /* ---- 退出信息 ---- */
    int               exit_code;        /* 退出码（仅 ZOMBIE 状态有效） */
} process_t;

/* ================================================================
 * 进程管理 API
 * ================================================================ */

/* 生命周期 */
void process_init(void);
int  process_create(const char *name);
int  process_kill(int pid);

/* ★ fork / exec — 类 Unix 进程原语 */
int  process_fork(void);                   /* 复制当前进程 */
int  process_exec(const char *name);       /* 替换当前进程映像 */

/* ★ wait / waitpid — 等待子进程退出 */
int  process_wait(int *exit_code);          /* 等待任意子进程 */
int  process_waitpid(int pid, int *exit_code); /* 等待特定子进程 */

/* 状态操作（供调度器调用） */
int  process_set_state(int pid, process_state_t new_state);
int  process_block(int pid);
int  process_wakeup(int pid);

/* 查询 */
process_t *process_get_by_pid(int pid);
process_t *process_get_current(void);
void       process_set_current(int pid);
int        process_get_current_pid(void);

/* 显示 */
void       process_print_list(void);
void       process_print_detail(int pid);

#endif
