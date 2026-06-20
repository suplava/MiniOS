#include "console.h"

/*
 * MiniOS Console
 *
 * 双模式说明：
 * 1. 本地模式：
 *    make / make run
 *    使用 printf / getchar，方便在 Windows / VSCode 终端调试。
 *
 * 2. QEMU 模式：
 *    make qemu / make qemu-run
 *    Makefile 会加入 -DBUILD_QEMU。
 *    使用 COM1 串口输出，适合 qemu-system-i386 -nographic。
 */

#ifdef BUILD_QEMU

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

static int serial_received(void) {
    return inb(COM1_PORT + 5) & 0x01;
}

static void serial_putchar(char c) {
    if (c == '\n') {
        serial_putchar('\r');
    }

    while (!serial_is_transmit_empty()) {
    }

    outb(COM1_PORT, (uint8_t)c);
}

static char serial_getchar(void) {
    while (!serial_received()) {
    }

    return (char)inb(COM1_PORT);
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

void console_putchar(char c) {
    serial_putchar(c);
}

char console_getchar(void) {
    return serial_getchar();
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
    unsigned int n;

    if (num == 0) {
        serial_putchar('0');
        return;
    }

    if (num < 0) {
        serial_putchar('-');
        n = (unsigned int)(-num);
    } else {
        n = (unsigned int)num;
    }

    while (n > 0 && i < 15) {
        buf[i++] = (char)('0' + (n % 10));
        n /= 10;
    }

    while (i > 0) {
        serial_putchar(buf[--i]);
    }
}

void print_line(const char *str) {
    printk(str);
    printk("\n");
}

#else

#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

void console_init(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    printk("[MiniOS] console init ok\n");
}

void console_putchar(char c) {
    putchar(c);
    fflush(stdout);
}

char console_getchar(void) {
    int c = getchar();

    if (c == EOF) {
        return 0;
    }

    return (char)c;
}

void printk(const char *str) {
    if (str == 0) {
        return;
    }

    printf("%s", str);
    fflush(stdout);
}

void print_int(int num) {
    printf("%d", num);
    fflush(stdout);
}

void print_line(const char *str) {
    if (str == 0) {
        printf("\n");
    } else {
        printf("%s\n", str);
    }

    fflush(stdout);
}

#endif
