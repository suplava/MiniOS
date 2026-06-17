/**
 * ================================================================
 *  MiniOS 硬件抽象层 (HAL)
 *
 *  同一份内核代码 → 两种编译模式, 通过 #ifdef BUILD_QEMU 切换:
 *
 *   本地模式 (默认):    #include 标准 C 库, printf → 终端
 *   QEMU 模式:         自实现 VGA/键盘/PIT, printf → 写显存
 *
 *  所有 .c 文件统一: #include "hal.h" 即可, 不再单独 include
 *  stdio.h / string.h / stdlib.h / time.h
 * ================================================================ */

#ifndef MINIOS_HAL_H
#define MINIOS_HAL_H

/* ═══════════════════════════════════════════════════════════════
 *  QEMU 裸机模式
 * ═══════════════════════════════════════════════════════════════ */
#ifdef BUILD_QEMU

#include <stddef.h>
#include <stdint.h>

/* ── 类型定义 ── */
typedef long clock_t;
#define CLOCKS_PER_SEC  100

/* ── string.h 替代 ── */
void  *memset(void *s, int c, size_t n);
void  *memcpy(void *dest, const void *src, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
char  *strcpy(char *dest, const char *src);
char  *strncpy(char *dest, const char *src, size_t n);
int    strncmp(const char *a, const char *b, size_t n);
size_t strcspn(const char *s, const char *reject);
char  *strchr(const char *s, int c);

/* ── stdio.h 替代 ── */
int   hal_printf(const char *fmt, ...);
int   hal_snprintf(char *buf, size_t size, const char *fmt, ...);
int   hal_putchar(int c);
char *hal_fgets(char *buf, int size, void *unused);

/* ── stdlib.h 替代 ── */
int   atoi(const char *s);

/* ── time.h 替代 ── */
clock_t hal_clock(void);

/* ── 重定向宏: 让 printf/fgets/clock 等直接映射到我们的实现 ── */
#define printf         hal_printf
#define snprintf       hal_snprintf
#define putchar(c)     hal_putchar(c)
#define fgets(b,s,u)   hal_fgets(b,s,u)
#define clock()        hal_clock()
#define stdin          ((void*)0)

/* ── VGA 驱动 ── */
void vga_init(void);

/* ── 键盘 ── */
void kbd_readline(char *buf, int max);

/* ── 公共工具 (QEMU 版是真实函数) ── */
void hal_print(const char *s);
void hal_println(const char *s);

/* ═══════════════════════════════════════════════════════════════
 *  本地模式 — 标准 C 库
 * ═══════════════════════════════════════════════════════════════ */
#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 键盘输入直接走 stdin */
#define kbd_readline(buf, max)  (void)(max); fgets(buf, max, stdin)

/* hal_print / hal_println */
#define hal_print(s)     printf("%s", s)
#define hal_println(s)   printf("%s\n", s)

#endif /* BUILD_QEMU */

#endif /* MINIOS_HAL_H */
