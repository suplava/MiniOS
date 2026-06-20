/**
 * ================================================================
 *  MiniOS 系统调用处理
 *
 *  系统调用是用户程序请求内核服务的唯一入口。
 *  在真实 OS 中通过 int 0x80 / sysenter 触发；
 *  在 MiniOS 中通过函数调用模拟。
 *
 *  改进点：
 *   - SYS_YIELD  → 调用 sched_yield() 真正让出 CPU
 *   - SYS_EXIT   → 调用 process_kill() 终止当前进程
 *   - SYS_GETPID → 返回当前进程的真实 PID
 * ================================================================ */

#include "hal.h"
#include "console.h"
#include "trap.h"
#include "syscall.h"
#include "sched.h"
#include "process.h"

static int syscall_ready = 0;

void syscall_init(void) {
    syscall_ready = 1;
    printk("[MiniOS] syscall init ok\n");
}

/**
 * syscall_handler — 系统调用分发器
 *
 * @sysno  系统调用号 (SYS_WRITE=1, SYS_GETPID=2, SYS_YIELD=3, SYS_EXIT=4)
 * @text   对于 SYS_WRITE：要输出的字符串
 * @arg2   预留参数 2
 * @arg3   预留参数 3
 * @return 因调用而异
 */
int syscall_handler(int sysno, const char *text, int arg2, int arg3) {
    (void)arg2;
    (void)arg3;

    if (!syscall_ready) {
        printk("[syscall] syscall module not ready\n");
        return -1;
    }

    trap_handler(TRAP_SYSCALL, "syscall enter");

    /* ---- SYS_WRITE: 输出字符串 ---- */
    if (sysno == SYS_WRITE) {
        if (text != NULL) {
            printk(text);
        }
        return 0;
    }

    /* ---- SYS_GETPID: 返回当前进程 PID ---- */
    if (sysno == SYS_GETPID) {
        int pid = process_get_current_pid();
        return pid;
    }

    /* ---- SYS_YIELD: 主动让出 CPU ---- */
    if (sysno == SYS_YIELD) {
        printk("[syscall] yield called — switching to next process\n");
        sched_yield();   /* 触发真正的调度切换 */
        return 0;
    }

    /* ---- SYS_EXIT: 终止当前进程 ---- */
    if (sysno == SYS_EXIT) {
        int pid = process_get_current_pid();
        printk("[syscall] exit called — terminating current process\n");

        if (pid > 0) {   /* 不允许杀死 idle */
            process_kill(pid);
            /* 进程被杀后，触发调度 */
            schedule();
        }
        return 0;
    }

    /* ---- SYS_FORK: 复制当前进程 ---- */
    if (sysno == SYS_FORK) {
        printk("[syscall] fork called\n");
        int child_pid = process_fork();
        return child_pid;
    }

    /* ---- SYS_EXEC: 替换进程映像 ---- */
    if (sysno == SYS_EXEC) {
        printk("[syscall] exec called\n");
        int ret = process_exec(text);
        return ret;
    }

    /* ---- SYS_WAIT: 等待子进程 ---- */
    if (sysno == SYS_WAIT) {
        printk("[syscall] wait called\n");
        int child = process_wait(NULL);
        return child;
    }

    /* ---- 未知系统调用 ---- */
    printk("[syscall] unknown syscall\n");
    return -1;
}
