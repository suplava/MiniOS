#include "console.h"
#include "trap.h"
#include "timer.h"
#include "syscall.h"

#include "memory.h"
#include "process.h"
#include "ramfs.h"
#include "shell.h"

int kernel_main(void) {
    console_init();

    printk("[MiniOS] boot success\n");
    printk("[MiniOS] kernel_main start\n");

    trap_init();
    timer_init();
    syscall_init();

    memory_init();
    process_init();
    ramfs_init();

    shell_start();

    return 0;
}

int main(void) {
    return kernel_main();
}
