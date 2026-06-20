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
    } else if (trap_no == TRAP_DIVZERO) {   // 新增分支
        printk("division by zero exception");
    } else if (trap_no == TRAP_OVERFLOW) {
        printk("integer overflow");
    } else if (trap_no == TRAP_BREAKPOINT) {
        printk("breakpoint (int3)");
    } else if (trap_no == TRAP_SINGLE_STEP) {
        printk("single step / trace");
    } else if (trap_no == TRAP_ALIGNMENT) {
        printk("misaligned memory access");
    } else if (trap_no == TRAP_STACK) {
        printk("stack error (overflow/fault)");
    } else if (trap_no == TRAP_PROTECTION) {
        printk("general protection fault");
    } else if (trap_no == TRAP_FPU) {
        printk("floating point exception");
    }else if (trap_no == TRAP_PAGEFAULT_READ) {
        printk("page fault (read access violation)");
    } else if (trap_no == TRAP_PAGEFAULT_WRITE) {
        printk("page fault (write access violation)");
    }else {
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
void trap_test_divzero(void) {
    trap_handler(TRAP_DIVZERO, "simulated division by zero");
}

void trap_test_overflow(void) {
    trap_handler(TRAP_OVERFLOW, "simulated integer overflow");
}

void trap_test_breakpoint(void) {
    trap_handler(TRAP_BREAKPOINT, "simulated breakpoint hit");
}

void trap_test_singlestep(void) {
    trap_handler(TRAP_SINGLE_STEP, "simulated single step trap");
}

void trap_test_alignment(void) {
    trap_handler(TRAP_ALIGNMENT, "simulated misaligned address access");
}

void trap_test_stack(void) {
    trap_handler(TRAP_STACK, "simulated stack overflow");
}

void trap_test_protection(void) {
    trap_handler(TRAP_PROTECTION, "simulated general protection violation");
}

void trap_test_fpu(void) {
    trap_handler(TRAP_FPU, "simulated fpu exception (division by zero in fpu)");
}
void trap_test_pf_read(void) {
    trap_handler(TRAP_PAGEFAULT_READ, "attempted to read from protected page");
}
void trap_test_pf_write(void) {
    trap_handler(TRAP_PAGEFAULT_WRITE, "attempted to write to read-only page");
}