/**
 * IDT (中断描述符表) 初始化 — x86 32 位
 * IRQ0 = 定时器滴答, IRQ1 = 键盘
 */

#ifdef BUILD_QEMU

#include <stdint.h>

/* 端口 I/O */
static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(p));
}

/* IRQ 入口桩 (定义在 irq.S) */
extern void irq0_stub(void);
extern void irq1_stub(void);
extern void irq2_stub(void), irq3_stub(void), irq4_stub(void);
extern void irq5_stub(void), irq6_stub(void), irq7_stub(void);
extern void irq8_stub(void), irq9_stub(void), irq10_stub(void);
extern void irq11_stub(void), irq12_stub(void), irq13_stub(void);
extern void irq14_stub(void), irq15_stub(void);

/* IDT 条目 */
struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[48];

static void idt_set(int n, void *handler) {
    uint32_t addr = (uint32_t)handler;
    idt[n].base_lo = addr & 0xFFFF;
    idt[n].base_hi = (addr >> 16) & 0xFFFF;
    idt[n].sel     = 0x08;   /* 内核代码段 (QEMU 默认 GDT) */
    idt[n].zero    = 0;
    idt[n].flags   = 0x8E;   /* 32 位中断门, ring0 */
}

/* ── IRQ 处理函数 (C 层) ── */
extern unsigned long pit_ticks;
extern void sched_tick(void);

void irq0_handler(void) {
    pit_ticks++;
    outb(0x20, 0x20);   /* EOI 主 PIC */
    sched_tick();
}

void irq1_handler(void) {
    /* 读键盘扫描码清 buffer (用内联 I/O 函数) */
    { uint8_t dummy; __asm__ volatile ("inb %1, %0" : "=a"(dummy) : "Nd"(0x60)); (void)dummy; }
    outb(0x20, 0x20);
}

void irq_default(void) {
    outb(0x20, 0x20);   /* EOI */
}

void idt_init(void) {
    /* 设置 48 个中断门 (0x00-0x2F) */
    void *stubs[] = {
        irq0_stub, irq1_stub, irq2_stub, irq3_stub,
        irq4_stub, irq5_stub, irq6_stub, irq7_stub,
        irq8_stub, irq9_stub, irq10_stub, irq11_stub,
        irq12_stub, irq13_stub, irq14_stub, irq15_stub,
    };

    for (int i = 0; i < 48; i++) {
        if (i >= 0x20 && i < 0x30)
            idt_set(i, stubs[i - 0x20]);  /* IRQ0-15 映射到 0x20-0x2F */
        else
            idt_set(i, irq15_stub);       /* 其他: 吃掉 (不触发故障) */
    }

    /* 加载 IDT */
    struct idt_ptr ptr;
    ptr.limit = sizeof(idt) - 1;
    ptr.base  = (uint32_t)&idt;
    __asm__ volatile ("lidt %0" : : "m"(ptr));

    /* PIC 重映射: IRQ0-7→0x20, IRQ8-15→0x28 */
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);

    /* 只启用 IRQ0 (定时器) */
    outb(0x21, 0xFE);
    outb(0xA1, 0xFF);
}

void idt_enable(void) {
    __asm__ volatile ("sti");
}

#endif /* BUILD_QEMU */
