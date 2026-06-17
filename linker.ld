/**
 * MiniOS 链接脚本 (i386-elf, 裸机)
 *
 * 入口: _start (boot.S)
 * 布局: multiboot 头放在最前面 8KB 内
 */

ENTRY(_start)

SECTIONS
{
    /* 内核从 1MB 开始 (避开 BIOS/显存区域) */
    . = 1M;

    /* Multiboot header 必须在最前面 */
    .multiboot : {
        *(.multiboot)
    }

    /* 代码 + 只读数据 */
    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    /* 已初始化数据 */
    .data : {
        *(.data)
        *(.data.*)
    }

    /* 未初始化数据 (BSS) */
    .bss : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }
}
