#include "console.h"
#include "trap.h"
#include "syscall.h"

static int syscall_ready = 0;

void syscall_init(void) {
    syscall_ready = 1;
    printk("[MiniOS] syscall init ok\n");
}

int syscall_handler(int sysno, const char *text, int arg2, int arg3) {
    (void)arg2;
    (void)arg3;

    if (!syscall_ready) {
        printk("[syscall] syscall module not ready\n");
        return -1;
    }

    trap_handler(TRAP_SYSCALL, "syscall enter");

    if (sysno == SYS_WRITE) {
        if (text != 0) {
            printk(text);
        }
        return 0;
    }

    if (sysno == SYS_GETPID) {
        return 1;
    }

    if (sysno == SYS_YIELD) {
        printk("[syscall] yield called\n");
        return 0;
    }

    if (sysno == SYS_EXIT) {
        printk("[syscall] exit called\n");
        return 0;
    }

    printk("[syscall] unknown syscall\n");
    return -1;
}
