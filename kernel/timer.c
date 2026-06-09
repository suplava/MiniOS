#include "console.h"
#include "trap.h"
#include "timer.h"

static int ticks = 0;

void timer_init(void) {
    ticks = 0;
    printk("[MiniOS] timer init ok\n");
}

void timer_tick(void) {
    ticks++;

    printk("[timer] tick ");
    print_int(ticks);
    printk("\n");

    trap_handler(TRAP_TIMER, "timer tick");
}

int timer_get_ticks(void) {
    return ticks;
}

