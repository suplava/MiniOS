# MiniOS 本地模式一键编译运行 (VSCode / Windows)
# 注意: 不编译 hal_qemu.c, 那是 QEMU 专用的

gcc -Iinclude `
    kernel/main.c `
    kernel/console.c `
    kernel/trap.c `
    kernel/timer.c `
    kernel/syscall.c `
    kernel/memory.c `
    kernel/process.c `
    kernel/sched.c `
    kernel/sync.c `
    kernel/ramfs.c `
    kernel/shell.c `
    kernel/test.c `
    kernel/userprog.c `
    -o minios.exe -Wall -Wextra -std=c11

if ($?) { ./minios.exe }
