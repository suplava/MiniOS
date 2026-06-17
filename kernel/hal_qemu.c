/**
 * ================================================================
 *  MiniOS QEMU 硬件抽象层实现
 *
 *  仅在 BUILD_QEMU 模式下编译, 提供:
 *    - VGA 文本模式驱动 (80×25, 彩色)
 *    - 键盘轮询输入 (端口 0x60/0x64)
 *    - PIT 定时器 (8254, 端口 0x40-0x43)
 *    - 迷你 libc (memset, memcpy, str*, atoi, printf)
 * ================================================================ */

#ifdef BUILD_QEMU

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include "hal.h"

/* ═══════════════════════════════════════════════════════════════
 *  VGA 文本模式驱动
 *
 *  显存基址: 0xB8000 (彩色文本模式)
 *  每字符 2 字节: [ASCII(1B), 属性(1B)]
 *  属性: 高4位=背景色, 低4位=前景色
 *  屏幕: 80 列 × 25 行
 * ═══════════════════════════════════════════════════════════════ */

#define VGA_BASE   ((volatile uint16_t *)0xB8000)
#define VGA_COLS   80
#define VGA_ROWS   25

static int vga_row = 0;
static int vga_col = 0;
static uint8_t vga_color = 0x07;  /* 黑底白字 */

void vga_init(void) {
    /* 清屏: 写空格到每个位置 */
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        VGA_BASE[i] = (uint16_t)' ' | ((uint16_t)vga_color << 8);
    }
    vga_row = 0;
    vga_col = 0;
}

static void vga_scroll(void) {
    /* 向上滚一行 */
    for (int r = 1; r < VGA_ROWS; r++) {
        for (int c = 0; c < VGA_COLS; c++) {
            VGA_BASE[(r - 1) * VGA_COLS + c] =
                VGA_BASE[r * VGA_COLS + c];
        }
    }
    /* 最后一行清空 */
    for (int c = 0; c < VGA_COLS; c++) {
        VGA_BASE[(VGA_ROWS - 1) * VGA_COLS + c] =
            (uint16_t)' ' | ((uint16_t)vga_color << 8);
    }
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    vga_color = (bg << 4) | (fg & 0x0F);
}

int hal_putchar(int c) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\b') {
        if (vga_col > 0) vga_col--;
    } else if (c == '\t') {
        vga_col = (vga_col + 4) & ~3;
    } else {
        VGA_BASE[vga_row * VGA_COLS + vga_col] =
            (uint16_t)((unsigned char)c) | ((uint16_t)vga_color << 8);
        vga_col++;
    }

    if (vga_col >= VGA_COLS) {
        vga_col = 0;
        vga_row++;
    }
    if (vga_row >= VGA_ROWS) {
        vga_scroll();
        vga_row = VGA_ROWS - 1;
    }
    return (unsigned char)c;
}

void vga_write(const char *str) {
    while (*str) hal_putchar(*str++);
}


/* ═══════════════════════════════════════════════════════════════
 *  迷你 printf (支持 %s %d %c %p %x %08x %%)
 * ═══════════════════════════════════════════════════════════════ */

static void print_dec(char **p, int n) {
    if (n < 0) { *(*p)++ = '-'; n = -n; }
    char tmp[12]; int i = 0;
    if (n == 0) tmp[i++] = '0';
    while (n > 0) { tmp[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) *(*p)++ = tmp[--i];
}

static void print_hex(char **p, unsigned int n, int width) {
    char tmp[10]; int i = 0;
    if (n == 0) tmp[i++] = '0';
    while (n > 0) {
        int d = n & 0xF;
        tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        n >>= 4;
    }
    while (i < width) tmp[i++] = '0';
    while (i > 0) *(*p)++ = tmp[--i];
}

static int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    char *p = buf;
    char *end = buf + size - 1;
    const char *f = fmt;

    while (*f && p < end) {
        if (*f != '%') { *p++ = *f++; continue; }
        f++;
        int width = 0;
        while (*f >= '0' && *f <= '9') { width = width * 10 + (*f - '0'); f++; }

        switch (*f) {
        case 's': {
            char *s = va_arg(ap, char *);
            if (!s) s = "(null)";
            while (*s && p < end) *p++ = *s++;
            break;
        }
        case 'd': case 'i':
            print_dec(&p, va_arg(ap, int));
            break;
        case 'x': case 'p':
            if (width == 0) width = 8;
            if (*f == 'p') { *p++ = '0'; *p++ = 'x'; }
            print_hex(&p, va_arg(ap, unsigned int), width);
            break;
        case 'c':
            *p++ = (char)va_arg(ap, int);
            break;
        case '%':
            *p++ = '%';
            break;
        case 'l':
            f++; /* skip 'l' in %ld etc, treat as int */
            if (*f == 'd' || *f == 'u' || *f == 'x')
                print_dec(&p, va_arg(ap, long));
            break;
        default:
            *p++ = '%'; *p++ = *f;
            break;
        }
        f++;
    }
    *p = '\0';
    return (int)(p - buf);
}

int hal_printf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    for (int i = 0; i < len; i++) hal_putchar(buf[i]);
    return len;
}

int hal_snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return len;
}


/* ═══════════════════════════════════════════════════════════════
 *  键盘轮询驱动 (无中断, 纯轮询)
 *
 *  端口 0x64: 状态寄存器
 *    bit 0 = 1 → 有数据可读
 *  端口 0x60: 数据寄存器 (scan code)
 *
 *  简化: 只处理 US QWERTY 键盘的基本按键
 * ═══════════════════════════════════════════════════════════════ */

/* 键盘扫描码 → ASCII 映射 (US QWERTY, 无 shift) */
static const char kbd_us[128] = {
    0,  0, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    0, 'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
    'z','x','c','v','b','n','m',',','.','/', 0, 0, 0, ' ', 0,
    /* 余下均为 0 */
};

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

int kbd_poll(void) {
    return (inb(0x64) & 1);
}

char kbd_getchar(void) {
    while (!kbd_poll()) { /* 忙等 */ }
    uint8_t sc = inb(0x60);
    /* 只处理 press (bit 7 == 0), 忽略 release (bit 7 == 1) */
    if (sc & 0x80) return 0;
    if (sc < 128) return kbd_us[sc];
    return 0;
}

void kbd_readline(char *buf, int max) {
    int i = 0;
    while (i < max - 1) {
        char c = kbd_getchar();
        if (c == 0) continue;
        if (c == '\n') { buf[i++] = '\n'; buf[i] = '\0'; break; }
        if (c == '\b') { if (i > 0) i--; continue; }
        buf[i++] = c;
        hal_putchar(c);  /* 回声 */
    }
    hal_putchar('\n');
    buf[max - 1] = '\0';
}

char *hal_fgets(char *buf, int size, void *unused) {
    (void)unused;
    kbd_readline(buf, size);
    return buf;
}


/* ═══════════════════════════════════════════════════════════════
 *  PIT 8254 定时器 (无中断版本: 手动轮询)
 *
 *  端口:
 *    0x40 = 通道 0 数据
 *    0x43 = 命令寄存器
 *
 *  QEMU 裸机中, 可启用 IRQ0 中断实现精确计时。
 *  当前简化版用 PIT 计数寄存器读当前值 (模式 2, rate generator)。
 *  每 1ms 大约 1193 次计数。
 * ═══════════════════════════════════════════════════════════════ */

#define PIT_FREQ  1193180

static unsigned long pit_ticks = 0;

void pit_init(unsigned int hz) {
    uint16_t divisor = (uint16_t)(PIT_FREQ / hz);
    /* outb 0x43: 通道0, 低字节+高字节, 模式2 (rate generator) */
    outb(0x43, 0x34);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    pit_ticks = 0;
}

void pit_handler(void) {
    pit_ticks++;
    /* 调用调度器的 tick 处理 */
    extern void sched_tick(void);
    sched_tick();
}

unsigned long pit_get_ticks(void) {
    return pit_ticks;
}

clock_t hal_clock(void) {
    return (clock_t)pit_ticks;
}


/* ═══════════════════════════════════════════════════════════════
 *  迷你 libc 实现
 * ═══════════════════════════════════════════════════════════════ */

void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dest;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (*s++) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

size_t strcspn(const char *s, const char *reject) {
    size_t n = 0;
    while (*s) {
        for (const char *r = reject; *r; r++)
            if (*s == *r) return n;
        s++; n++;
    }
    return n;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return NULL;
}

int atoi(const char *s) {
    int n = 0, sign = 1;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; }
    return n * sign;
}


/* ═══════════════════════════════════════════════════════════════
 *  公共工具
 * ═══════════════════════════════════════════════════════════════ */

void hal_print(const char *s) { vga_write(s); }
void hal_println(const char *s) { vga_write(s); hal_putchar('\n'); }

#endif /* BUILD_QEMU */
