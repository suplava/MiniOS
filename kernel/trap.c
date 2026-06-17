#include "console.h"
#include "trap.h"

static int trap_ready = 0;

void trap_init(void) {
    trap_ready = 1;
    printk("[MiniOS] trap init ok\n");
}

void trap_handler(int trap_no, const char *message) {
    if (!trap_ready) {
        printk("[trap] trap module not ready\n");
        return;
    }

    printk("[trap] caught trap: ");

    if (trap_no == TRAP_TIMER) {
        printk("timer interrupt");
    } else if (trap_no == TRAP_FAULT) {
        printk("fault exception");
    } else if (trap_no == TRAP_SYSCALL) {
        printk("system call");
    } else if (trap_no == TRAP_ILLEGAL) {
        printk("illegal instruction exception");
    } else if (trap_no == TRAP_NULLPTR) {
        printk("null pointer access exception");
    } else {
        printk("unknown trap");
    }

    printk(" | message: ");

    if (message != 0) {
        printk(message);
    } else {
        printk("none");
    }

    printk("\n");
}

void trap_test_fault(void) {
    trap_handler(TRAP_FAULT, "simulated fault for demo");
}

void trap_test_illegal(void) {
    trap_handler(TRAP_ILLEGAL, "invalid machine code executed");
}
void trap_test_nullptr(void) {
    trap_handler(TRAP_NULLPTR, "write access to null address");
}
