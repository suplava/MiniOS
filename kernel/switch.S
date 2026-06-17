/*
 * 上下文切换 — x86 32 位, 保存/恢复所有通用寄存器 + 栈 + eflags
 *
 * void switch_to(process_context_t *old, process_context_t *new);
 *
 * process_context_t 布局 (每个字段 4 字节):
 *   offset  0 = eip       offset 20 = ecx
 *   offset  4 = esp       offset 24 = edx
 *   offset  8 = eflags    offset 28 = esi
 *   offset 12 = eax       offset 32 = edi
 *   offset 16 = ebx       offset 36 = ebp
 */

.global switch_to
.type switch_to, @function
switch_to:
    pushl %eax
    pushl %ebx
    pushl %ecx
    pushl %edx
    pushl %esi
    pushl %edi
    pushl %ebp

    /* 栈: [ebp,edi,esi,edx,ecx,ebx,eax, ret, old(32), new(36)] */
    movl 32(%esp), %esi        /* esi = old_ctx */
    movl 36(%esp), %edi        /* edi = new_ctx */

    /* 弹出到 old_ctx */
    popl 36(%esi) ; popl 32(%esi) ; popl 28(%esi)
    popl 24(%esi) ; popl 20(%esi) ; popl 16(%esi) ; popl 12(%esi)

    popl 0(%esi)               /* eip */
    pushf ; popl 8(%esi)       /* eflags */

    /* esp: 保存为当前栈指针 (指向 new_ctx 参数) */
    movl %esp, 4(%esi)

    /* ── 切到新栈 ── */
    movl 4(%edi), %esp

    /* ── 恢复新进程 ── */
    pushl 0(%edi)              /* 压入 eip → ret 时弹出 */
    pushl 8(%edi) ; popf       /* eflags */

    movl 12(%edi), %eax
    movl 16(%edi), %ebx
    movl 20(%edi), %ecx
    movl 24(%edi), %edx
    movl 28(%edi), %esi
    movl 36(%edi), %ebp

    pushl 32(%edi)             /* edi 值压栈 */
    popl %edi                  /* 恢复 edi */
    ret                        /* 弹出 eip 并跳转 */
