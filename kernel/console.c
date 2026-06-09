#include <stdio.h>
#include "console.h"

void console_init(void) {
    printk("[MiniOS] console init ok\n");
}

void printk(const char *str) {
    printf("%s", str);
}

void print_int(int num) {
    printf("%d", num);
}

void print_line(const char *str) {
    printf("%s\n", str);
}
