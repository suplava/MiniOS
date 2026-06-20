/**
 * MiniOS Multiboot 引导扇区
 *
 * 告诉 GRUB/QEMU 如何加载我们的内核。
 * Multiboot 规范: OS 镜像前 8KB 内必须有 magic header。
 */

.set MAGIC,    0x1BADB002
.set FLAGS,    0x00000003       /* bit0=页对齐, bit1=内存信息 */
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot, "a"
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.section .text
.global _start
.type _start, @function
_start:
    /* 设置内核栈 (16KB) */
    movl $stack_top, %esp

    /* 重置 EFLAGS */
    pushl $0
    popf

    /* 传递 Multiboot 信息给 kernel_main(magic, info) */
    pushl %ebx       /* arg2: multiboot_info 指针 */
    pushl %eax       /* arg1: multiboot magic (0x2BADB002) */
    call kernel_main
    addl $8, %esp

    /* 永远不该到这里 */
    cli
1:  hlt
    jmp 1b

.section .bss, "aw", @nobits
.align 16
stack_bottom:
    .skip 16384   /* 16 KB 内核栈 */
stack_top:
