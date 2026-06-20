/* IRQ 中断桩 — 每个 IRQ 一个入口, pushal → 调 C 处理 → popal → iretl */

.macro IRQ_STUB n, handler
.global irq\n\()_stub
irq\n\()_stub:
    pushal
    cld
    call \handler
    popal
    iretl
.endm

IRQ_STUB 0, irq0_handler
IRQ_STUB 1, irq1_handler
IRQ_STUB 2, irq_default
IRQ_STUB 3, irq_default
IRQ_STUB 4, irq_default
IRQ_STUB 5, irq_default
IRQ_STUB 6, irq_default
IRQ_STUB 7, irq_default
IRQ_STUB 8, irq_default
IRQ_STUB 9, irq_default
IRQ_STUB 10, irq_default
IRQ_STUB 11, irq_default
IRQ_STUB 12, irq_default
IRQ_STUB 13, irq_default
IRQ_STUB 14, irq_default
IRQ_STUB 15, irq_default
