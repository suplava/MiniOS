#include "console.h"
#include "hal.h"

/*
 * MiniOS Console
 *
 * 说明：
 * 1. 已删除 Windows 平台相关代码：
 *    - windows.h
 *    - SetConsoleOutputCP
 *    - SetConsoleCP
 *    - printf
 *
 * 2. 当前版本使用 COM1 串口输出，适合 QEMU：
 *    qemu-system-i386 ... -serial stdio
 *
 * 3. 上层模块继续使用：
 *    - printk()
 *    - print_int()
 *    - print_line()
 */

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;

#define COM1_PORT 0x3F8

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static int serial_is_transmit_empty(void) {
    return inb(COM1_PORT + 5) & 0x20;
}

static void serial_putchar(char c) {
    if (c == '\n') {
        serial_putchar('\r');
    }

    while (!serial_is_transmit_empty()) {
    }

    outb(COM1_PORT, (uint8_t)c);
}

static void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

void console_init(void) {
    serial_init();
    printk("[MiniOS] console init ok\n");
}

void printk(const char *str) {
    if (str == 0) {
        return;
    }

    while (*str) {
        serial_putchar(*str);
        str++;
    }
}

void print_int(int num) {
    char buf[16];
    int i = 0;
    int negative = 0;

    if (num == 0) {
        serial_putchar('0');
        return;
    }

    if (num < 0) {
        negative = 1;
        num = -num;
    }

    while (num > 0 && i < 15) {
        buf[i++] = (char)('0' + (num % 10));
        num /= 10;
    }

    if (negative) {
        serial_putchar('-');
    }

    while (i > 0) {
        serial_putchar(buf[--i]);
    }
}

void print_line(const char *str) {
    printk(str);
    printk("\n");
}
