/**
 * ================================================================
 *  MiniOS 进程管理模块
 *
 *  负责：
 *   1. 进程控制块 (PCB) 表的生命周期管理
 *   2. 进程状态机转换（含合法性检查）
 *   3. 虚拟地址空间绑定
 *   4. 当前运行进程追踪
 * ================================================================
 */

#include "hal.h"
#include "process.h"
#include "memory.h"
#include "sched.h"

/* ---- 进程表 ---- */
static process_t process_table[MAX_PROCESSES];
static int       next_pid = 0;

/* ---- 当前正在运行的进程 PID ---- */
static int current_pid = -1;

/* ================================================================
 *  内部辅助函数
 * ================================================================ */

/**
 * state_to_string — 将状态枚举转为可读字符串（用于 ps 命令）
 */
static const char *state_to_string(process_state_t state) {
    switch (state) {
        case PROC_UNUSED:  return "UNUSED";
        case PROC_READY:   return "READY";
        case PROC_RUNNING: return "RUNNING";
        case PROC_BLOCKED: return "BLOCKED";
        case PROC_ZOMBIE:  return "ZOMBIE";
        default:           return "UNKNOWN";
    }
}

/**
 * next_free_slot — 在进程表中查找可用槽位
 *
 * 优先复用 ZOMBIE 槽，其次使用 UNUSED 槽。
 * 返回槽位索引，-1 表示表满。
 */
static int next_free_slot(void) {
    /* 优先复用 ZOMBIE */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_ZOMBIE) {
            return i;
        }
    }
    /* 其次用 UNUSED */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_UNUSED) {
            return i;
        }
    }
    return -1;
}

/*
 * shell_main — shell 进程入口 (在 shell 自己的内核栈上执行!)
 * 由 switch_to 恢复上下文后首次执行
 */
void shell_main(void) {
    extern void shell_start(void);
    shell_start();
    /* shell 退出后, 循环 yield, 等待被父进程回收 */
    extern void sched_yield(void);
    while (1) { sched_yield(); }
}

/*
 * process_entry — 普通新进程入口
 * 被 switch_to 首次调度时执行, 在进程自己的内核栈上
 */
void process_entry(void) {
    /* 静默版本: 测试用, 跑一次就阻塞 */
    extern void sched_block(void);
    sched_block();
    extern void sched_yield(void);
    while (1) { sched_yield(); }
}

/*
 * worker_main — 演示用的工作进程入口
 * 在自己的内核栈上循环干活 + yield, 展示真多任务
 */
void worker_main(void) {
    extern int  process_get_current_pid(void);
    extern void sched_yield(void);
    extern void sched_block(void);

    int pid = process_get_current_pid();
    for (int r = 0; r < 3; r++) {
        printf("[worker %d] working round %d/3\n", pid, r + 1);
        for (volatile int j = 0; j < 300000; j++) {}  /* 模拟干活 */
        sched_yield();  /* ★ 让出 CPU */
    }
    printf("[worker %d] all done, exiting\n", pid);
    sched_block();
    while (1) { sched_yield(); }
}

/* ================================================================
 *  进程表初始化
 * ================================================================ */

void process_init(void) {
    memset(process_table, 0, sizeof(process_table));
    next_pid    = 0;
    current_pid = -1;

    /* ---- PID 0: idle 进程 ----
     * idle 是系统的"空闲进程"：当没有其他进程可运行时，调度器选择 idle。
     * 它永远不应该被杀死或阻塞。 */
    int slot = next_free_slot();
    process_table[slot].pid         = next_pid++;
    process_table[slot].parent_pid  = -1;          /* 无父进程 */
    strcpy(process_table[slot].name, "idle");
    process_table[slot].state       = PROC_RUNNING;
    process_table[slot].time_slice  = DEFAULT_TIME_SLICE;
    process_table[slot].total_ticks = 0;
    process_table[slot].priority    = 0;            /* 最低优先级 */
    process_table[slot].kernel_stack = alloc_page();
    process_table[slot].pdir         = vm_create_address_space();
    process_table[slot].vma_list     = NULL;
    process_table[slot].exit_code    = 0;
    memset(&process_table[slot].context, 0, sizeof(process_context_t));
    /* idle 的上下文: 首次被切走时由 switch_to 保存真实值, 这里留空即可 */

    current_pid = 0;   /* idle 是最初的当前进程 */

    /* ---- PID 1: shell 进程 ---- */
    slot = next_free_slot();
    process_table[slot].pid         = next_pid++;
    process_table[slot].parent_pid  = 0;
    strcpy(process_table[slot].name, "shell");
    process_table[slot].state       = PROC_READY;
    process_table[slot].time_slice  = DEFAULT_TIME_SLICE;
    process_table[slot].total_ticks = 0;
    process_table[slot].priority    = 1;
    process_table[slot].kernel_stack = alloc_page();
    process_table[slot].pdir         = vm_create_address_space();
    process_table[slot].vma_list     = NULL;
    process_table[slot].exit_code    = 0;
    memset(&process_table[slot].context, 0, sizeof(process_context_t));
    /* ★ shell 上下文: 首次被调度时跳到 shell_main, 在 shell 自己的栈上 */
    process_table[slot].context.esp =
        (uint32_t)(uintptr_t)process_table[slot].kernel_stack + PAGE_SIZE - 16;
    process_table[slot].context.eip =
        (uint32_t)(uintptr_t)shell_main;

    printf("[MiniOS] process init ok (2 processes created)\n");
}

/* ================================================================
 *  进程创建
 * ================================================================ */

int process_create(const char *name) {
    /* 名称校验 */
    if (name == NULL || strlen(name) == 0) {
        printf("[process] invalid process name\n");
        return -1;
    }

    /* 查找空闲槽位 */
    int slot = next_free_slot();
    if (slot < 0) {
        printf("[process] create failed: process table full\n");
        return -1;
    }

    /* 分配内核栈 */
    void *stack = alloc_page();
    if (stack == NULL) {
        printf("[process] create failed: no memory for kernel stack\n");
        return -1;
    }

    /* 创建虚拟地址空间 */
    page_directory_t *pdir = vm_create_address_space();
    if (pdir == NULL) {
        printf("[process] create failed: no memory for page directory\n");
        free_page(stack);
        return -1;
    }

    /* 填充 PCB */
    process_t *proc = &process_table[slot];
    memset(proc, 0, sizeof(process_t));   /* 清空旧数据 */

    proc->pid         = next_pid++;
    proc->parent_pid  = current_pid;       /* 父进程 = 当前运行进程 */
    strncpy(proc->name, name, PROCESS_NAME_LEN - 1);
    proc->name[PROCESS_NAME_LEN - 1] = '\0';
    proc->state       = PROC_READY;
    proc->time_slice  = sched_get_time_slice();  /* 使用当前配置的时间片 */
    proc->total_ticks = 0;
    proc->priority    = 1;
    proc->kernel_stack = stack;
    proc->pdir         = pdir;
    proc->vma_list     = NULL;
    proc->exit_code    = 0;
    memset(&proc->context, 0, sizeof(process_context_t));

    /* ★ 初始化上下文的栈指针: 指向内核栈顶 */
    proc->context.esp = (uint32_t)(uintptr_t)stack + PAGE_SIZE - 16;
    /* eip = 进程入口: 首次被调度时从这里开始执行 */
    extern void process_entry(void);
    proc->context.eip = (uint32_t)(uintptr_t)process_entry;

    /*
     * 设置用户态标准内存布局 (Demand Paging):
     *   代码段 0x08048000-0x08049000  R+X   4KB
     *   数据段 0x08049000-0x0804A000  R+W   4KB
     *   堆     0x0804A000-0x08050000  R+W   24KB (向上增长)
     *   栈     0xBFFFE000-0xC0000000  R+W   8KB  (向下增长)
     *
     * 这些只是注册 VMA 范围，实际物理页在首次访问(缺页)时才分配。
     */
    vm_area_add(&proc->vma_list, 0x08048000, 0x08049000,
                VM_USER | VM_EXEC);
    vm_area_add(&proc->vma_list, 0x08049000, 0x0804A000,
                VM_USER | VM_WRITABLE);
    vm_area_add(&proc->vma_list, 0x0804A000, 0x08050000,
                VM_USER | VM_WRITABLE);                  /* heap */
    vm_area_add(&proc->vma_list, 0xBFFFE000, 0xC0000000,
                VM_USER | VM_WRITABLE | VM_GROWSDOWN);   /* stack */

    printf("[process] create process ok\n");
    printf("[process] pid  = %d\n", proc->pid);
    printf("[process] name = %s\n", proc->name);
    printf("[process] parent_pid = %d\n", proc->parent_pid);

    /* 将新进程加入调度器的就绪队列 */
    sched_add_process(proc->pid);

    return proc->pid;
}

/* ================================================================
 *  进程终止
 * ================================================================ */

int process_kill(int pid) {
    /* 保护 idle 进程 */
    if (pid == 0) {
        printf("[process] cannot kill idle process\n");
        return -1;
    }

    /* 查找目标进程 */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t *proc = &process_table[i];

        if (proc->state != PROC_UNUSED && proc->pid == pid) {

            /* 已经是 ZOMBIE → 不重复操作 */
            if (proc->state == PROC_ZOMBIE) {
                printf("[process] pid %d is already zombie\n", pid);
                return -1;
            }

            /* 释放内核栈 */
            if (proc->kernel_stack != NULL) {
                free_page(proc->kernel_stack);
                proc->kernel_stack = NULL;
            }

            /* 销毁虚拟地址空间 */
            if (proc->pdir != NULL) {
                vm_destroy_address_space(proc->pdir);
                proc->pdir = NULL;
            }

            /* 回收 VMA 链表（只释放节点，不释放已映射的物理页——
             * 物理页在 vm_destroy_address_space 中统一释放） */
            if (proc->vma_list != NULL) {
                vm_area_destroy_all(proc->vma_list);
                proc->vma_list = NULL;
            }

            /* 从调度器就绪队列移除（若在其中） */
            sched_remove_process(pid);

            /* 孤儿进程收养：将该进程的所有子进程过继给 idle */
            int orphan_count = 0;
            for (int j = 0; j < MAX_PROCESSES; j++) {
                if (process_table[j].state != PROC_UNUSED &&
                    process_table[j].parent_pid == pid) {
                    process_table[j].parent_pid = 0;  /* 收养给 idle */
                    orphan_count++;
                }
            }
            if (orphan_count > 0) {
                printf("[process] %d orphan(s) of pid=%d adopted by idle\n",
                       orphan_count, pid);
            }

            /* 状态 → ZOMBIE */
            proc->state     = PROC_ZOMBIE;
            proc->exit_code = 0;

            /* 如果父进程是 idle, 自动收尸 (idle 从不调 wait) */
            if (proc->parent_pid == 0) {
                printf("[process] auto-reaping zombie pid=%d (parent=idle)\n",
                       pid);
                proc->pid         = -1;
                proc->state       = PROC_UNUSED;
                proc->parent_pid  = -1;
                proc->exit_code   = 0;
                /* 已清理, 跳过后续唤醒父进程逻辑 */
                if (current_pid == pid) current_pid = 0;
                printf("[process] kill process ok, pid = %d\n", pid);
                return 0;
            }

            /* 唤醒等待此进程的父进程 */
            if (proc->parent_pid >= 0) {
                process_t *parent = process_get_by_pid(proc->parent_pid);
                if (parent && parent->state == PROC_BLOCKED) {
                    printf("[process] waking blocked parent pid=%d\n",
                           proc->parent_pid);
                    process_wakeup(proc->parent_pid);
                    sched_wakeup(proc->parent_pid);
                }
            }

            /* 如果杀死的是当前进程，清除 current_pid */
            if (current_pid == pid) {
                current_pid = 0;  /* 回退到 idle */
            }

            printf("[process] kill process ok, pid = %d\n", pid);
            return 0;
        }
    }

    printf("[process] pid %d not found\n", pid);
    return -1;
}

/* ================================================================
 *  状态操作（供调度器调用）
 * ================================================================ */

/**
 * process_set_state — 带合法性检查的状态转换
 *
 * 合法转换：
 *   READY   → RUNNING  （被调度器选中）
 *   RUNNING → READY    （时间片用完 / yield）
 *   RUNNING → BLOCKED  （等待事件）
 *   BLOCKED → READY    （事件到达 / wakeup）
 *   RUNNING → ZOMBIE   （exit）
 *   READY   → ZOMBIE   （被 kill）
 *   BLOCKED → ZOMBIE   （被 kill）
 */
int process_set_state(int pid, process_state_t new_state) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == pid &&
            process_table[i].state != PROC_UNUSED) {

            process_state_t old = process_table[i].state;

            /* 合法性校验 */
            int valid = 0;
            switch (old) {
                case PROC_READY:
                    valid = (new_state == PROC_RUNNING ||
                             new_state == PROC_BLOCKED ||
                             new_state == PROC_ZOMBIE);
                    break;
                case PROC_RUNNING:
                    valid = (new_state == PROC_READY ||
                             new_state == PROC_BLOCKED ||
                             new_state == PROC_ZOMBIE);
                    break;
                case PROC_BLOCKED:
                    valid = (new_state == PROC_READY ||
                             new_state == PROC_ZOMBIE);
                    break;
                case PROC_ZOMBIE:
                    valid = 0;  /* ZOMBIE 不能再转换 */
                    break;
                case PROC_UNUSED:
                default:
                    valid = 0;
                    break;
            }

            if (!valid) {
                printf("[process] invalid state transition: "
                       "%s → %s (pid=%d)\n",
                       state_to_string(old),
                       state_to_string(new_state), pid);
                return -1;
            }

            process_table[i].state = new_state;
            return 0;
        }
    }
    return -1;  /* pid 未找到 */
}

/**
 * process_block — 将进程设为阻塞状态
 * 只能从 RUNNING 或 READY 转换。
 * 若进程在就绪队列中，会被移除。
 */
int process_block(int pid) {
    /* idle 永远不能被阻塞 */
    if (pid == 0) {
        printf("[process] cannot block idle\n");
        return -1;
    }
    printf("[process] block pid=%d\n", pid);
    sched_remove_process(pid);
    return process_set_state(pid, PROC_BLOCKED);
}

/**
 * process_wakeup — 将阻塞进程唤醒到就绪队列
 * 只能从 BLOCKED 转换。
 */
int process_wakeup(int pid) {
    printf("[process] wakeup pid=%d\n", pid);
    return process_set_state(pid, PROC_READY);
}

/* ================================================================
 *  查询函数
 * ================================================================ */

/**
 * process_get_by_pid — 根据 PID 查找 PCB
 */
process_t *process_get_by_pid(int pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROC_UNUSED &&
            process_table[i].pid == pid) {
            return &process_table[i];
        }
    }
    return NULL;
}

/**
 * process_get_current — 获取当前正在运行的进程
 */
process_t *process_get_current(void) {
    if (current_pid < 0) return NULL;
    return process_get_by_pid(current_pid);
}

/**
 * process_set_current — 设置当前运行进程（由调度器调用）
 */
void process_set_current(int pid) {
    current_pid = pid;
}

/**
 * process_get_current_pid — 获取当前进程 PID
 */
int process_get_current_pid(void) {
    return current_pid;
}

/* ================================================================
 *  fork() / exec() — 类 Unix 进程原语
 * ================================================================ */

/**
 * process_fork  — 创建当前进程的副本（深拷贝版本）
 *
 * 简化实现：深拷贝整个地址空间（所有已映射的物理页），
 * 而非 Linux 的写时复制 (COW)。
 *
 * 返回值：
 *   >0  → 子进程 PID（返回给父进程）
 *   -1  → 失败
 *
 * 调用后：
 *   - 子进程与父进程拥有独立的内存副本
 *   - 子进程状态 = READY
 *   - 子进程继承父进程的 VMA 布局
 */
int process_fork(void) {
    process_t *parent = process_get_current();
    if (parent == NULL) {
        printf("[fork] no current process\n");
        return -1;
    }

    /* 查找空闲槽位 */
    int slot = next_free_slot();
    if (slot < 0) {
        printf("[fork] process table full\n");
        return -1;
    }

    /* 为子进程创建新的页目录 */
    page_directory_t *child_pdir = vm_create_address_space();
    if (child_pdir == NULL) {
        printf("[fork] no memory for child page directory\n");
        return -1;
    }

    /* 为子进程分配内核栈 */
    void *child_stack = alloc_page();
    if (child_stack == NULL) {
        printf("[fork] no memory for child kernel stack\n");
        vm_destroy_address_space(child_pdir);
        return -1;
    }

    /*
     * 深拷贝父进程地址空间：
     *   遍历父进程页目录 → 对每个已映射的物理页：
     *     1. 分配新物理页
     *     2. 复制数据
     *     3. 映射到子进程页目录的相同虚拟地址
     */
    for (int i = 0; i < 1024; i++) {
        if (parent->pdir->entries[i].present) {
            uint32_t table_phys = parent->pdir->entries[i].table_addr << 12;
            page_table_t *parent_table =
                (page_table_t *)vm_index_to_phys(table_phys);

            for (int j = 0; j < 1024; j++) {
                if (parent_table->entries[j].present) {
                    uint32_t vaddr = (i << 22) | (j << 12);
                    uint32_t pfn = parent_table->entries[j].page_addr;

                    /* 分配新物理页 */
                    void *new_page = alloc_page();
                    if (new_page == NULL) {
                        printf("[fork] out of memory during copy\n");
                        vm_destroy_address_space(child_pdir);
                        free_page(child_stack);
                        return -1;
                    }

                    /* 复制页内容 */
                    void *old_page = &((unsigned char*)0)[0]; /* placeholder */
                    old_page = vm_index_to_phys(pfn * PAGE_SIZE);
                    memcpy(new_page, old_page, PAGE_SIZE);

                    /* 映射到子进程 */
                    uint32_t flags = VM_PRESENT;
                    if (parent_table->entries[j].writable)
                        flags |= VM_WRITABLE;
                    if (parent_table->entries[j].user)
                        flags |= VM_USER;

                    vm_map_page(child_pdir, vaddr,
                                vm_phys_to_index(new_page), flags);
                }
            }
        }
    }

    /* 填充子进程 PCB */
    process_t *child = &process_table[slot];
    memset(child, 0, sizeof(process_t));

    child->pid         = next_pid++;
    child->parent_pid  = parent->pid;
    snprintf(child->name, PROCESS_NAME_LEN, "%.24s_child",
             parent->name);  /* 截断父名称以确保不超过 32 字节 */
    child->state       = PROC_READY;
    child->time_slice  = DEFAULT_TIME_SLICE;
    child->total_ticks = 0;
    child->priority    = parent->priority;
    child->kernel_stack = child_stack;
    child->pdir         = child_pdir;
    child->exit_code    = 0;

    /* 深拷贝 VMA 链表 */
    child->vma_list = NULL;
    vm_area_t *vma = parent->vma_list;
    while (vma) {
        vm_area_add(&child->vma_list, vma->start, vma->end, vma->flags);
        vma = vma->next;
    }

    /* 复制父进程的 CPU 上下文 */
    memcpy(&child->context, &parent->context,
           sizeof(process_context_t));

    /* 加入就绪队列 */
    sched_add_process(child->pid);

    printf("[fork] child pid=%d created from parent pid=%d (%s)\n",
           child->pid, parent->pid, parent->name);
    printf("[fork] deep-copied address space + VMA list\n");

    return child->pid;  /* 返回子进程 PID 给父进程 */
}

/**
 * process_exec  — 替换当前进程映像
 *
 * 模拟 exec() 系统调用：
 *   1. 销毁旧的虚拟地址空间（保留 PCB）
 *   2. 创建新的虚拟地址空间
 *   3. 注册标准 VMA 布局
 *   4. 重置 CPU 上下文
 *
 * 注意：在真实 OS 中，exec 还会加载 ELF 文件的代码/数据段。
 *       此处简化为重建 VMA 布局，具体加载逻辑后续扩展。
 *
 * 返回值：0 成功，-1 失败
 */
int process_exec(const char *name) {
    process_t *proc = process_get_current();
    if (proc == NULL) {
        printf("[exec] no current process\n");
        return -1;
    }

    /* 更新进程名 */
    if (name != NULL && strlen(name) > 0) {
        strncpy(proc->name, name, PROCESS_NAME_LEN - 1);
        proc->name[PROCESS_NAME_LEN - 1] = '\0';
    }

    /* 1. 销毁旧地址空间 */
    if (proc->pdir != NULL) {
        vm_destroy_address_space(proc->pdir);
        proc->pdir = NULL;
    }

    /* 2. 销毁旧 VMA 链表 */
    if (proc->vma_list != NULL) {
        vm_area_destroy_all(proc->vma_list);
        proc->vma_list = NULL;
    }

    /* 3. 创建新地址空间 */
    proc->pdir = vm_create_address_space();
    if (proc->pdir == NULL) {
        printf("[exec] failed to create new address space\n");
        return -1;
    }

    /* 4. 注册标准 VMA 布局 */
    vm_area_add(&proc->vma_list, 0x08048000, 0x08049000,
                VM_USER | VM_EXEC);
    vm_area_add(&proc->vma_list, 0x08049000, 0x0804A000,
                VM_USER | VM_WRITABLE);
    vm_area_add(&proc->vma_list, 0x0804A000, 0x08050000,
                VM_USER | VM_WRITABLE);
    vm_area_add(&proc->vma_list, 0xBFFFE000, 0xC0000000,
                VM_USER | VM_WRITABLE | VM_GROWSDOWN);

    /* 5. 重置 CPU 上下文 */
    memset(&proc->context, 0, sizeof(process_context_t));
    proc->context.eip = 0x08048000;  /* 代码段入口 */
    proc->context.esp = 0xC0000000;  /* 栈顶 */
    proc->state        = PROC_READY;
    proc->time_slice   = DEFAULT_TIME_SLICE;
    proc->total_ticks  = 0;
    proc->exit_code    = 0;

    printf("[exec] process pid=%d now running '%s'\n",
           proc->pid, proc->name);
    printf("[exec] new VMA layout: code/data/heap/stack registered\n");

    return 0;
}

/* ================================================================
 *  wait() / waitpid() — 等待子进程退出
 * ================================================================ */

/**
 * process_wait  — 等待任意一个子进程退出
 *
 * 1. 遍历进程表，查找当前进程的 ZOMBIE 子进程
 * 2. 有 → 收集退出码，清除 PCB 槽位（UNUSED），返回子进程 PID
 * 3. 无 ZOMBIE 但有活着的子进程 → 阻塞自身等待
 * 4. 无任何子进程 → 返回 -1
 */
int process_wait(int *exit_code) {
    process_t *current = process_get_current();
    if (current == NULL) return -1;

    /* 查找 ZOMBIE 子进程 */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t *proc = &process_table[i];
        if (proc->state == PROC_ZOMBIE &&
            proc->parent_pid == current->pid) {

            int child_pid = proc->pid;

            if (exit_code) *exit_code = proc->exit_code;

            printf("[wait] parent pid=%d collected zombie child "
                   "pid=%d (exit_code=%d)\n",
                   current->pid, child_pid, proc->exit_code);

            /* 清理 PCB 槽位 */
            proc->pid   = -1;
            proc->state = PROC_UNUSED;
            proc->parent_pid = -1;
            proc->exit_code  = 0;

            return child_pid;
        }
    }

    /* 检查是否有活着的子进程 */
    int has_children = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROC_UNUSED &&
            process_table[i].state != PROC_ZOMBIE &&
            process_table[i].parent_pid == current->pid) {
            has_children = 1;
            break;
        }
    }

    if (has_children) {
        /* 有活着的子进程但都不是 ZOMBIE → 阻塞等待 */
        printf("[wait] pid=%d has living children, blocking...\n",
               current->pid);
        process_block(current->pid);
        schedule();
        /* 被唤醒后重试一次（真实 OS 会循环） */
        for (int i = 0; i < MAX_PROCESSES; i++) {
            process_t *proc = &process_table[i];
            if (proc->state == PROC_ZOMBIE &&
                proc->parent_pid == current->pid) {
                int child_pid = proc->pid;
                if (exit_code) *exit_code = proc->exit_code;
                printf("[wait] pid=%d collected zombie child "
                       "pid=%d after wakeup\n",
                       current->pid, child_pid);
                proc->pid   = -1;
                proc->state = PROC_UNUSED;
                proc->parent_pid = -1;
                proc->exit_code  = 0;
                return child_pid;
            }
        }
    }

    printf("[wait] pid=%d has no children\n", current->pid);
    return -1;
}

/**
 * process_waitpid  — 等待特定子进程退出
 *
 * @pid       目标子进程 PID
 * @exit_code 输出参数：子进程退出码
 *
 * 返回 PID 表示成功，-1 失败。
 */
int process_waitpid(int pid, int *exit_code) {
    process_t *proc = process_get_by_pid(pid);
    if (proc == NULL) {
        printf("[waitpid] pid=%d not found\n", pid);
        return -1;
    }

    process_t *current = process_get_current();
    if (current == NULL || proc->parent_pid != current->pid) {
        printf("[waitpid] pid=%d is not a child of pid=%d\n",
               pid, current->pid);
        return -1;
    }

    if (proc->state == PROC_ZOMBIE) {
        if (exit_code) *exit_code = proc->exit_code;
        printf("[waitpid] collected zombie pid=%d (exit=%d)\n",
               pid, proc->exit_code);
        proc->pid   = -1;
        proc->state = PROC_UNUSED;
        proc->parent_pid = -1;
        proc->exit_code  = 0;
        return pid;
    }

    /* 子进程还没死 → 阻塞等待 */
    printf("[waitpid] pid=%d still alive, blocking...\n", pid);
    process_block(current->pid);
    schedule();

    /* 被唤醒后重试 */
    proc = process_get_by_pid(pid);
    if (proc && proc->state == PROC_ZOMBIE) {
        if (exit_code) *exit_code = proc->exit_code;
        printf("[waitpid] collected zombie pid=%d after wakeup\n", pid);
        proc->pid   = -1;
        proc->state = PROC_UNUSED;
        proc->parent_pid = -1;
        proc->exit_code  = 0;
        return pid;
    }

    return -1;
}

/* ================================================================
 *  显示函数
 * ================================================================ */

void process_print_list(void) {
    printf("PID   PPID  STATE      TIME_SLICE   PRI   TICKS   NAME\n");
    printf("----  ----  ---------  ----------   ---   -----   ----\n");

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROC_UNUSED) {
            printf("%-5d %-5d %-10s %-12d %-5d %-6d %s",
                   process_table[i].pid,
                   process_table[i].parent_pid,
                   state_to_string(process_table[i].state),
                   process_table[i].time_slice,
                   process_table[i].priority,
                   process_table[i].total_ticks,
                   process_table[i].name);

            /* 标记当前进程 */
            if (process_table[i].pid == current_pid) {
                printf("  <-- current");
            }
            printf("\n");
        }
    }
}

/**
 * process_print_detail — 打印单个进程的详细信息
 */
void process_print_detail(int pid) {
    process_t *proc = process_get_by_pid(pid);
    if (proc == NULL) {
        printf("[process] pid %d not found\n", pid);
        return;
    }

    printf("==== Process %d Detail ====\n", pid);
    printf("  name        : %s\n", proc->name);
    printf("  state       : %s\n", state_to_string(proc->state));
    printf("  parent_pid  : %d\n", proc->parent_pid);
    printf("  time_slice  : %d\n", proc->time_slice);
    printf("  total_ticks : %d\n", proc->total_ticks);
    printf("  priority    : %d\n", proc->priority);
    printf("  exit_code   : %d\n", proc->exit_code);
    printf("  kernel_stack: %p\n", proc->kernel_stack);
    printf("  pdir        : %p\n", (void *)proc->pdir);
    printf("  context.eip : 0x%08x\n", proc->context.eip);
    printf("  context.esp : 0x%08x\n", proc->context.esp);
}
