# ═══════════════════════════════════════════════════════════════
#  MiniOS Makefile — 双模式编译
#
#   make            → 本地模式 (minios.exe), VSCode 直接调试
#   make qemu       → QEMU 模式 (minios.iso), 裸机启动
#   make run        → 本地运行
#   make qemu-run   → QEMU 启动 (需要安装 qemu-system-i386)
# ═══════════════════════════════════════════════════════════════

# ── 本地模式 (默认) ──────────────────────────────
CC      = gcc
CFLAGS  = -Iinclude -Wall -Wextra -std=c11
TARGET  = minios

SRC_LOCAL = kernel/main.c \
            kernel/console.c \
            kernel/trap.c \
            kernel/timer.c \
            kernel/syscall.c \
            kernel/memory.c \
            kernel/process.c \
            kernel/sched.c \
            kernel/sync.c \
            kernel/ramfs.c \
            kernel/shell.c \
            kernel/test.c

all: $(TARGET)

$(TARGET): $(SRC_LOCAL)
	$(CC) $(SRC_LOCAL) $(CFLAGS) -o $(TARGET)

run: all
	./$(TARGET)

# ── QEMU 裸机模式 ────────────────────────────────
QEMU_CC      = i686-linux-gnu-gcc
QEMU_CFLAGS  = -DBUILD_QEMU -Iinclude -Wall -Wextra -std=c11 \
               -ffreestanding -nostdlib -m32
QEMU_LDFLAGS = -T linker.ld -ffreestanding -nostdlib -m32

SRC_QEMU = kernel/main.c \
           kernel/hal_qemu.c \
           kernel/console.c \
           kernel/trap.c \
           kernel/timer.c \
           kernel/syscall.c \
           kernel/memory.c \
           kernel/process.c \
           kernel/sched.c \
           kernel/sync.c \
           kernel/ramfs.c \
           kernel/shell.c \
           kernel/test.c

qemu: minios.bin
	@echo "  → minios.bin ready (use 'make qemu-iso' for .iso)"

minios.bin: $(SRC_QEMU) kernel/boot.S kernel/irq.S kernel/switch.S kernel/idt.c linker.ld
	$(QEMU_CC) $(QEMU_CFLAGS) -c kernel/boot.S -o kernel/boot.o
	$(QEMU_CC) $(QEMU_CFLAGS) -c kernel/irq.S -o kernel/irq.o
	$(QEMU_CC) $(QEMU_CFLAGS) -c kernel/switch.S -o kernel/switch.o
	$(QEMU_CC) $(QEMU_CFLAGS) -c $(SRC_QEMU) kernel/idt.c
	$(QEMU_CC) $(QEMU_LDFLAGS) *.o kernel/boot.o kernel/irq.o kernel/switch.o -o $@
	@rm -f *.o kernel/boot.o kernel/irq.o kernel/switch.o

qemu-iso: minios.bin
	@mkdir -p iso/boot/grub
	cp minios.bin iso/boot/
	@echo 'set timeout=0'                > iso/boot/grub/grub.cfg
	@echo 'set default=0'               >> iso/boot/grub/grub.cfg
	@echo 'menuentry "MiniOS" {'        >> iso/boot/grub/grub.cfg
	@echo '    multiboot /boot/minios.bin' >> iso/boot/grub/grub.cfg
	@echo '}'                           >> iso/boot/grub/grub.cfg
	grub-mkrescue -o minios.iso iso/ 2>/dev/null
	@rm -rf iso
	@echo "  → minios.iso ready"

qemu-run: qemu-iso
	qemu-system-i386 -cdrom minios.iso -m 32M -no-reboot -nographic

qemu-run-gui: qemu-iso
	qemu-system-i386 -cdrom minios.iso -m 32M -no-reboot

clean:
	rm -f $(TARGET) $(TARGET).exe minios.bin minios.iso
	rm -rf iso *.o kernel/*.o
