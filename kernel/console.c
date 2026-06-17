#include <stdio.h>
#include <windows.h>   // 新增：用于 SetConsoleOutputCP

#include "console.h"

void console_init(void) {
    // 设置控制台输出代码页为 UTF-8，解决 Windows 终端中文乱码
    SetConsoleOutputCP(CP_UTF8);
    // 可选：同时设置输入代码页，方便 fgets 读取中文（如果将来需要）
    SetConsoleCP(CP_UTF8);

    // 原先的 UTF-8 BOM 输出对终端无效，已移除
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