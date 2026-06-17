/**
 * ================================================================
 *  MiniOS 内存管理模块
 *
 *  包含三个层次：
 *   1. 物理页分配器 (alloc_page / free_page)  —— 位图管理 16MB 物理 RAM
 *   2. 内核堆分配器 (kmalloc / kfree)          —— 空闲链表 + 线性兜底
 *   3. 虚拟内存管理 (vm_* )                    —— 两级页表，按需映射
 * ================================================================
 */

#include <stdint.h>
#include "hal.h"
#include "memory.h"

/* ===================================================================
 *  第一层：物理页分配器
 * =================================================================== */

/* 16 MB 物理 RAM —— 模拟真实硬件上的物理内存 */
static unsigned char physical_memory[TOTAL_MEMORY_SIZE];

/* 位图：page_used[i] == 1 表示第 i 个物理页已被分配 */
static unsigned char page_used[TOTAL_PAGES];

static int total_pages = TOTAL_PAGES;
static int used_pages  = 0;

/* ===================================================================
 *  第二层：内核堆分配器（空闲链表 + 线性兜底）
 * =================================================================== */

#define KMALLOC_POOL_SIZE  (128 * 1024)        /* 128 KB 堆池 */
#define FREE_BLOCK_MIN     16                  /* 最小空闲块大小（含头部） */

static unsigned char kmalloc_pool[KMALLOC_POOL_SIZE];
static int kmalloc_offset = 0;                 /* 线性分配水位线 */

/*
 * 空闲链表头部结构 —— 放在每个空闲块的开头
 *   size : 本块总大小（含头部）
 *   next : 下一个空闲块，NULL 表示末尾
 */
typedef struct free_block {
    int size;
    struct free_block *next;
} free_block_t;

static free_block_t *free_list = NULL;         /* 空闲链表头指针 */

/* ---------- 统计 ---------- */
static int kmalloc_alloc_count = 0;            /* 累计分配次数 */
static int kmalloc_free_count  = 0;            /* 累计释放次数 */
static int kmalloc_reuse_count = 0;            /* 从空闲链表复用的次数 */

/* ---------- 内部辅助：将地址转换为空闲块头部 ---------- */
static free_block_t *ptr_to_block(void *ptr) {
    return (free_block_t *)((unsigned char *)ptr - sizeof(free_block_t));
}

static void *block_to_ptr(free_block_t *block) {
    return (void *)((unsigned char *)block + sizeof(free_block_t));
}

/**
 * kfree 实现 —— 使用空闲链表回收内存
 *
 * 算法：
 *   1. 检查指针合法性
 *   2. 构造空闲块头部（size + next）
 *   3. 按地址顺序插入空闲链表
 *   4. 尝试与前驱 / 后继块合并（coalesce）
 *
 * 这是 MiniOS 内存管理最大的改进之一：
 *   旧版 kfree 是 no-op，现在真正回收并支持复用。
 */
void kfree(void *ptr) {
    if (ptr == NULL) return;

    /* 地址必须在 kmalloc_pool 范围内 */
    unsigned char *cptr = (unsigned char *)ptr;
    if (cptr < kmalloc_pool || cptr >= kmalloc_pool + KMALLOC_POOL_SIZE) {
        printf("[kfree] warning: pointer out of pool range\n");
        return;
    }

    /* 8 字节对齐检查 */
    if (((uintptr_t)ptr & 0x7) != 0) {
        printf("[kfree] warning: unaligned pointer\n");
        return;
    }

    /* 构造空闲块头部 */
    free_block_t *block = ptr_to_block(ptr);

    /*
     * 重复释放检查：遍历 free_list 确认该块不在其中。
     */
    free_block_t *cur = free_list;
    while (cur) {
        if (cur == block) {
            printf("[kfree] warning: double free detected\n");
            return;
        }
        cur = cur->next;
    }

    /*
     * 使用 kmalloc 写入的块大小（存储在头部 size 字段中）。
     * kmalloc 在分配时已将 needed（含头部 + 对齐后的用户大小）存入。
     *
     * 防御：如果 size 异常（可能是旧数据或野指针），回退到空闲链表推测。
     */
    int block_size = block->size;
    if (block_size <= 0 || block_size > KMALLOC_POOL_SIZE) {
        /* 头部 size 无效 —— 通过空闲链表或 bump 边界推测 */
        free_block_t *succ = NULL;
        {
            free_block_t *c = free_list;
            while (c) {
                if ((uintptr_t)c > (uintptr_t)block) {
                    if (!succ || (uintptr_t)c < (uintptr_t)succ) succ = c;
                }
                c = c->next;
            }
        }
        if (succ) {
            block_size = (int)((uintptr_t)succ - (uintptr_t)block);
        } else {
            block_size = (int)((uintptr_t)(kmalloc_pool + kmalloc_offset)
                               - (uintptr_t)block);
        }
        if (block_size <= 0 || block_size > KMALLOC_POOL_SIZE) {
            printf("[kfree] warning: invalid block size, skipping\n");
            return;
        }
    }

    /*
     * 检查是否可以直接回退 bump allocator 水位线：
     * 如果该块是最后一个被分配的（紧挨着 kmalloc_offset），
     * 且空闲链表为空，直接回退即可，无需插入空闲链表。
     */
    if (kmalloc_offset > 0 &&
        (uintptr_t)(kmalloc_pool + kmalloc_offset) ==
        (uintptr_t)block + block_size) {
        if (free_list == NULL) {
            kmalloc_offset = (int)((uintptr_t)block - (uintptr_t)kmalloc_pool);
            kmalloc_free_count++;
            return;
        }
    }

    block->size = block_size;
    block->next = NULL;

    /* ---------- 按地址升序插入空闲链表 ---------- */
    free_block_t **indirect = &free_list;
    while (*indirect && (uintptr_t)(*indirect) < (uintptr_t)block) {
        indirect = &(*indirect)->next;
    }
    block->next = *indirect;
    *indirect = block;

    /* ---------- 与后继合并 ---------- */
    if (block->next &&
        (uintptr_t)block + block->size == (uintptr_t)block->next) {
        block->size += block->next->size;
        block->next = block->next->next;
    }

    /* ---------- 与前驱合并 ---------- */
    {
        free_block_t *prev = free_list;
        while (prev && prev != block) {
            if ((uintptr_t)prev + prev->size == (uintptr_t)block) {
                prev->size += block->size;
                prev->next = block->next;
                break;
            }
            prev = prev->next;
        }
    }

    kmalloc_free_count++;
}

/**
 * kmalloc 实现 —— 先查空闲链表，不够再线性分配
 *
 * 算法：
 *   1. size <= 0 → NULL
 *   2. size 对齐到 8 字节
 *   3. 在 free_list 中 first-fit 搜索
 *   4. 若找到合适的空闲块 → 从链表中取出（若远大于需求则分裂）
 *   5. 若找不到 → 回退到线性 bump allocator
 *
 * 注意：分配时在用户指针前预留 sizeof(free_block_t) 作为块头部，
 *       这样 kfree 时可以直接读取块大小。
 */
void *kmalloc(int size) {
    if (size <= 0) return NULL;

    /* 8 字节对齐 */
    int aligned = (size + 7) & ~7;
    int needed  = aligned + sizeof(free_block_t);  /* 含头部 */

    /* ---------- first-fit 在空闲链表中查找 ---------- */
    free_block_t **indirect = &free_list;
    while (*indirect) {
        free_block_t *blk = *indirect;
        if (blk->size >= needed) {
            /* 找到了：从链表中移除 */
            *indirect = blk->next;

            /* 如果剩余空间足够大，分裂 */
            int remaining = blk->size - needed;
            if (remaining >= FREE_BLOCK_MIN) {
                free_block_t *new_blk = (free_block_t *)((unsigned char *)blk + needed);
                new_blk->size = remaining;
                new_blk->next = blk->next;   /* 此时 blk->next 可能已过时，
                                                用 *indirect（更新后的链表后继）*/
                /* 重新插入新空闲块（按地址排序） */
                free_block_t **ins = &free_list;
                while (*ins && (uintptr_t)(*ins) < (uintptr_t)new_blk) {
                    ins = &(*ins)->next;
                }
                new_blk->next = *ins;
                *ins = new_blk;

                blk->size = needed;
            }

            kmalloc_alloc_count++;
            kmalloc_reuse_count++;

            /* 初始化块头部 */
            blk->size = needed;
            blk->next = NULL;
            return block_to_ptr(blk);
        }
        indirect = &(*indirect)->next;
    }

    /* ---------- 空闲链表无合适块，回退到线性分配 ---------- */
    if (kmalloc_offset + needed > KMALLOC_POOL_SIZE) {
        printf("[kmalloc] out of memory (pool exhausted, "
               "offset=%d, needed=%d)\n", kmalloc_offset, needed);
        return NULL;
    }

    free_block_t *blk = (free_block_t *)&kmalloc_pool[kmalloc_offset];
    kmalloc_offset += needed;

    blk->size = needed;
    blk->next = NULL;

    kmalloc_alloc_count++;
    return block_to_ptr(blk);
}

/* ===================================================================
 *  第三层：虚拟内存管理
 *
 *  采用 x86 风格的两级页表：
 *    虚拟地址 (32 bits)
 *    ├─ PDE 索引 [31:22] ─ 页目录中的偏移 (1024 项)
 *    ├─ PTE 索引 [21:12] ─ 页表中的偏移   (1024 项)
 *    └─ 页内偏移 [11: 0] ─ 物理页内的字节偏移
 *
 *  每个进程拥有独立的 page_directory_t，由进程管理模块分配。
 * =================================================================== */

static int vm_map_count   = 0;   /* 累计映射次数 */
static int vm_unmap_count = 0;   /* 累计解除映射次数 */

/**
 * phys_to_index  — 将物理指针转换为 physical_memory 数组中的索引
 * index_to_phys — 将索引转换回物理指针
 *
 * 这两个函数是虚拟内存子系统的基础：
 *   PDE.table_addr 和 PTE.page_addr 存储的都是物理页号（右移 12 位），
 *   通过 index_to_phys(pde.table_addr << 12) 可以拿回真实指针。
 */
uint32_t vm_phys_to_index(void *phys_ptr) {
    if (phys_ptr == NULL) return 0;
    uintptr_t offset = (uintptr_t)phys_ptr - (uintptr_t)physical_memory;
    return (uint32_t)offset;
}

void *vm_index_to_phys(uint32_t index) {
    if (index >= TOTAL_MEMORY_SIZE) return NULL;
    return &physical_memory[index];
}

/* ---------- 内部：从物理页号转换到指针 ---------- */
static void *pfn_to_ptr(uint32_t pfn) {
    return &physical_memory[pfn * PAGE_SIZE];
}

void vm_init(void) {
    vm_map_count   = 0;
    vm_unmap_count = 0;
    printf("[MiniOS] virtual memory init ok\n");
}

/**
 * vm_create_address_space  — 为一个新进程创建独立的虚拟地址空间
 *
 * 返回值：一个 page_directory_t 指针（指向新分配的物理页）
 * 内部会清零所有 PDE，表示所有虚拟地址初始都未映射。
 * 调用者负责后续用 vm_destroy_address_space 释放。
 */
page_directory_t *vm_create_address_space(void) {
    /* 分配一页存放页目录 */
    page_directory_t *pdir = (page_directory_t *)alloc_page();
    if (pdir == NULL) {
        printf("[vm] create_address_space failed: no free page\n");
        return NULL;
    }

    /* 清零 PDE 数组 —— 所有条目都未 present */
    memset(pdir, 0, sizeof(page_directory_t));
    return pdir;
}

/**
 * vm_map_page  — 将虚拟页映射到物理页
 *
 * @pdir   进程的页目录
 * @vaddr  虚拟地址（只使用页对齐部分，低 12 位忽略）
 * @paddr  物理地址（同样只使用页对齐部分）
 * @flags  权限标志 (VM_PRESENT | VM_WRITABLE | VM_USER | VM_EXEC)
 *
 * 流程：
 *  1. 在页目录中找到或创建 PDE → 指向一个页表
 *  2. 在页表中设置 PTE → 指向物理页 + 权限
 *
 * 返回值：0 成功，-1 失败
 */
int vm_map_page(page_directory_t *pdir, uint32_t vaddr,
                uint32_t paddr, uint32_t flags) {
    if (pdir == NULL) return -1;

    uint32_t pde_idx = PDE_INDEX(vaddr);   /* 高 10 位 */
    uint32_t pte_idx = PTE_INDEX(vaddr);   /* 中 10 位 */

    uint32_t pfn = paddr / PAGE_SIZE;       /* 物理页号 */

    /* 确保页表存在 */
    if (!(pdir->entries[pde_idx].present)) {
        /* 从物理内存分配一页作为页表 */
        page_table_t *table = (page_table_t *)alloc_page();
        if (table == NULL) {
            printf("[vm] map_page failed: no free page for page table\n");
            return -1;
        }
        memset(table, 0, sizeof(page_table_t));

        /* 设置 PDE：指向新页表 */
        uint32_t table_index = vm_phys_to_index(table);
        pdir->entries[pde_idx].present    = 1;
        pdir->entries[pde_idx].writable   = 1;
        pdir->entries[pde_idx].user       = (flags & VM_USER) ? 1 : 0;
        pdir->entries[pde_idx].table_addr = table_index >> 12;  /* 4KB 对齐 */
    }

    /* 获取页表指针 */
    uint32_t table_phys = pdir->entries[pde_idx].table_addr << 12;
    page_table_t *table = (page_table_t *)vm_index_to_phys(table_phys);

    /* 设置 PTE */
    table->entries[pte_idx].present   = (flags & VM_PRESENT) ? 1 : 0;
    table->entries[pte_idx].writable  = (flags & VM_WRITABLE) ? 1 : 0;
    table->entries[pte_idx].user      = (flags & VM_USER) ? 1 : 0;
    table->entries[pte_idx].page_addr = pfn;

    vm_map_count++;
    return 0;
}

/**
 * vm_unmap_page  — 解除虚拟页的映射 + 空页表回收
 *
 * 解除 PTE 映射后, 检查整个页表是否为空 (1024 个 PTE 全未 present)。
 * 若为空 → 释放该页表物理页 + 清除对应 PDE。
 */
int vm_unmap_page(page_directory_t *pdir, uint32_t vaddr) {
    if (pdir == NULL) return -1;

    uint32_t pde_idx = PDE_INDEX(vaddr);
    uint32_t pte_idx = PTE_INDEX(vaddr);

    if (!(pdir->entries[pde_idx].present)) {
        return -1;  /* 页表不存在，无法解除映射 */
    }

    uint32_t table_phys = pdir->entries[pde_idx].table_addr << 12;
    page_table_t *table = (page_table_t *)vm_index_to_phys(table_phys);

    if (!(table->entries[pte_idx].present)) {
        return -1;  /* 该页本来就没映射 */
    }

    /* 清除 PTE */
    table->entries[pte_idx].present   = 0;
    table->entries[pte_idx].writable  = 0;
    table->entries[pte_idx].user      = 0;
    table->entries[pte_idx].page_addr = 0;

    vm_unmap_count++;

    /* ★ 空页表回收: 检查此页表是否完全为空 */
    int table_empty = 1;
    for (int j = 0; j < 1024; j++) {
        if (table->entries[j].present) {
            table_empty = 0;
            break;
        }
    }

    if (table_empty) {
        /* 释放页表物理页 */
        free_page(table);

        /* 清除 PDE */
        pdir->entries[pde_idx].present    = 0;
        pdir->entries[pde_idx].writable   = 0;
        pdir->entries[pde_idx].user       = 0;
        pdir->entries[pde_idx].table_addr = 0;

        printf("[vm] recycled empty page table (PDE[%d])\n", pde_idx);
    }

    return 0;
}

/**
 * vm_translate  — 虚拟地址 → 物理地址转换（MMU 模拟 v2.0）
 *
 * ★ 改进: 当 vma_list != NULL 且翻译失败时, 自动调用缺页处理。
 *
 * @pdir      进程的页目录
 * @vma_list  进程的 VMA 链表 (NULL = 纯翻译模式, 不触发缺页)
 * @vaddr     虚拟地址
 * @is_write  是否写访问 (1=写, 0=读)
 *
 * 返回值：物理地址, 失败返回 0。
 *
 * 流程:
 *   查 PDE → 查 PTE → present?
 *     YES → 返回物理地址
 *     NO  → vma_list? → 调 vm_handle_page_fault → 重试 → 返回
 *                      → NULL?    → 返回 0 (页故障)
 */
uint32_t vm_translate(page_directory_t *pdir, vm_area_t *vma_list,
                       uint32_t vaddr, int is_write) {
    if (pdir == NULL) return 0;

    uint32_t pde_idx  = PDE_INDEX(vaddr);
    uint32_t pte_idx  = PTE_INDEX(vaddr);
    uint32_t offset   = PAGE_OFFSET(vaddr);

    /* PDE 不存在 → 页故障 */
    if (!(pdir->entries[pde_idx].present)) {
        goto page_fault;
    }

    /* 获取页表 */
    {
        uint32_t table_phys = pdir->entries[pde_idx].table_addr << 12;
        page_table_t *table = (page_table_t *)vm_index_to_phys(table_phys);

        /* PTE 不存在 → 页故障 */
        if (!(table->entries[pte_idx].present)) {
            goto page_fault;
        }

        /* 构造物理地址 */
        uint32_t phys_page = table->entries[pte_idx].page_addr * PAGE_SIZE;
        return phys_page + offset;
    }

page_fault:
    /* 如果提供了 VMA 链表, 自动尝试缺页处理 */
    if (vma_list != NULL) {
        uint32_t page_aligned = vaddr & ~(PAGE_SIZE - 1);
        pf_result_t pf = vm_handle_page_fault(pdir, vma_list,
                                               page_aligned, is_write);
        if (pf == PF_HANDLED) {
            /* 映射成功, 重新翻译 (不递归, 直接重走翻译逻辑) */
            /* 此时 PDE 和 PTE 应该都已 present */
            pde_idx = PDE_INDEX(vaddr);
            pte_idx = PTE_INDEX(vaddr);
            if (pdir->entries[pde_idx].present) {
                uint32_t table_phys =
                    pdir->entries[pde_idx].table_addr << 12;
                page_table_t *table =
                    (page_table_t *)vm_index_to_phys(table_phys);
                if (table->entries[pte_idx].present) {
                    uint32_t phys_page =
                        table->entries[pte_idx].page_addr * PAGE_SIZE;
                    return phys_page + offset;
                }
            }
        }
    }

    return 0;  /* 真正的页故障 */
}

/**
 * vm_destroy_address_space  — 销毁进程的整个虚拟地址空间
 *
 * 遍历页目录，释放所有存在的页表及其映射的物理页。
 * 最后释放页目录本身。
 */
void vm_destroy_address_space(page_directory_t *pdir) {
    if (pdir == NULL) return;

    for (int i = 0; i < 1024; i++) {
        if (pdir->entries[i].present) {
            /* 获取页表 */
            uint32_t table_phys = pdir->entries[i].table_addr << 12;
            page_table_t *table = (page_table_t *)vm_index_to_phys(table_phys);

            /* 遍历页表，释放所有已映射的物理页 */
            for (int j = 0; j < 1024; j++) {
                if (table->entries[j].present) {
                    uint32_t pfn = table->entries[j].page_addr;
                    void *phys_page = pfn_to_ptr(pfn);
                    free_page(phys_page);
                }
            }

            /* 释放页表本身 */
            free_page(table);
        }
    }

    /* 释放页目录 */
    free_page(pdir);
}

/**
 * vm_print_stats  — 打印某个进程的虚拟地址空间使用情况
 */
void vm_print_stats(page_directory_t *pdir) {
    if (pdir == NULL) {
        printf("[vm] no address space\n");
        return;
    }

    int used_tables = 0;
    int mapped_pages = 0;

    for (int i = 0; i < 1024; i++) {
        if (pdir->entries[i].present) {
            used_tables++;
            uint32_t table_phys = pdir->entries[i].table_addr << 12;
            page_table_t *table = (page_table_t *)vm_index_to_phys(table_phys);
            for (int j = 0; j < 1024; j++) {
                if (table->entries[j].present) {
                    mapped_pages++;
                }
            }
        }
    }

    printf("[VM Stats]\n");
    printf("  page tables used : %d / 1024\n", used_tables);
    printf("  pages mapped     : %d\n", mapped_pages);
    printf("  total map ops    : %d\n", vm_map_count);
    printf("  total unmap ops  : %d\n", vm_unmap_count);
}

/* ===================================================================
 *  第一层 + 第二层：原有 API（与旧版兼容）
 * =================================================================== */

/**
 * vm_area_create  — 创建一个 VMA 节点
 *
 * 通过 kmalloc 分配小内存（约 24 字节），不占用物理页。
 * 返回的节点需要手动链入进程的 vma_list。
 */
vm_area_t *vm_area_create(uint32_t start, uint32_t end, uint32_t flags) {
    vm_area_t *vma = (vm_area_t *)kmalloc(sizeof(vm_area_t));
    if (vma == NULL) {
        printf("[vm] vm_area_create failed: kmalloc returned NULL\n");
        return NULL;
    }
    vma->start = start;
    vma->end   = end;
    vma->flags = flags;
    vma->next  = NULL;
    return vma;
}

/**
 * vm_area_destroy_all  — 释放整个 VMA 链表
 *
 * 遍历链表并用 kfree 回收每个节点。
 * 注意：VMA 只是"元数据"，不负责释放映射的物理页。
 */
void vm_area_destroy_all(vm_area_t *head) {
    vm_area_t *cur = head;
    while (cur) {
        vm_area_t *next = cur->next;
        kfree(cur);
        cur = next;
    }
}

/**
 * vm_area_find  — 在 VMA 链表中查找包含 vaddr 的区域
 *
 * 返回第一个 start <= vaddr < end 的 VMA，未找到返回 NULL。
 */
vm_area_t *vm_area_find(vm_area_t *head, uint32_t vaddr) {
    vm_area_t *cur = head;
    while (cur) {
        if (vaddr >= cur->start && vaddr < cur->end) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

/**
 * vm_area_add  — 向 VMA 链表添加一个新区域（保持地址升序）
 *
 * 返回 0 成功，-1 失败。
 */
int vm_area_add(vm_area_t **head, uint32_t start,
                uint32_t end, uint32_t flags) {
    vm_area_t *vma = vm_area_create(start, end, flags);
    if (vma == NULL) return -1;

    /* 按地址升序插入 */
    if (*head == NULL || start < (*head)->start) {
        vma->next = *head;
        *head = vma;
    } else {
        vm_area_t *cur = *head;
        while (cur->next && cur->next->start < start) {
            cur = cur->next;
        }
        vma->next = cur->next;
        cur->next = vma;
    }
    return 0;
}

/**
 * vm_handle_page_fault  — ★ 缺页处理核心 ★
 *
 * 模拟 OS 的缺页中断处理程序：
 *   1. 在 VMA 链表中查找 vaddr 是否合法
 *   2. 若 VM_GROWSDOWN 且接近栈底 → 自动扩展栈 VMA
 *   3. 分配新的物理页 + 清零（模拟 OS 的安全清零）
 *   4. 调用 vm_map_page 建立映射
 *
 * @pdir      进程的页目录
 * @vma_list  进程的 VMA 链表
 * @vaddr     触发缺页的虚拟地址（自动页对齐）
 * @is_write  1=写访问, 0=读访问（用于权限检查，当前简化未强制执行）
 *
 * 返回值：PF_HANDLED / PF_SEGFAULT / PF_NOMEM
 *
 * 这就是 "按需分页 (Demand Paging)" 的核心实现：
 *   - 进程启动时只注册 VMA，不分配物理页
 *   - 首次访问触发缺页 → 此时才真正分配物理页
 *   - 栈区域自动向下增长（在 64KB 范围内）
 */
pf_result_t vm_handle_page_fault(page_directory_t *pdir,
                                  vm_area_t *vma_list,
                                  uint32_t vaddr,
                                  int is_write) {
    (void)is_write;  /* 权限检查预留 */

    if (pdir == NULL || vma_list == NULL) {
        return PF_SEGFAULT;
    }

    uint32_t page_aligned = vaddr & ~(PAGE_SIZE - 1);  /* 页对齐 */

    /* 先检查是否已经被映射 (传 NULL 避免递归缺页) */
    if (vm_translate(pdir, NULL, page_aligned, 0) != 0) {
        return PF_HANDLED;  /* 已映射，无需处理 */
    }

    /* 查找包含此地址的 VMA */
    vm_area_t *vma = vm_area_find(vma_list, page_aligned);

    /*
     * 栈自动增长 (VM_GROWSDOWN):
     *   若 vaddr 在 VMA 下方但在 64KB 范围内，且 VMA 标记为 VM_GROWSDOWN，
     *   则扩展 VMA 的 start 到 page_aligned。
     */
    if (vma == NULL) {
        /* 遍历所有 VM_GROWSDOWN 的 VMA，检查是否在可扩展范围内 */
        vm_area_t *cur = vma_list;
        while (cur) {
            if ((cur->flags & VM_GROWSDOWN) &&
                page_aligned >= cur->start - (64 * 1024) &&
                page_aligned < cur->start) {
                /* 栈自动向下扩展 */
                printf("[vm] stack growth: 0x%08x → 0x%08x\n",
                       cur->start, page_aligned);
                cur->start = page_aligned;
                vma = cur;
                break;
            }
            cur = cur->next;
        }
    }

    if (vma == NULL) {
        printf("[vm] page fault: segfault at 0x%08x (not in any VMA)\n",
               page_aligned);
        return PF_SEGFAULT;
    }

    /* 分配物理页 */
    void *phys_page = alloc_page();
    if (phys_page == NULL) {
        printf("[vm] page fault: out of memory at 0x%08x\n", page_aligned);
        return PF_NOMEM;
    }

    /* 清零新页 — 安全要求：不能让进程看到旧数据 */
    memset(phys_page, 0, PAGE_SIZE);

    /* 建立映射 */
    uint32_t paddr = vm_phys_to_index(phys_page);
    uint32_t flags = VM_PRESENT;
    if (vma->flags & VM_WRITABLE) flags |= VM_WRITABLE;
    if (vma->flags & VM_USER)    flags |= VM_USER;

    int ret = vm_map_page(pdir, page_aligned, paddr, flags);
    if (ret != 0) {
        free_page(phys_page);
        printf("[vm] page fault: vm_map_page failed at 0x%08x\n",
               page_aligned);
        return PF_NOMEM;
    }

    printf("[vm] demand paging: 0x%08x → phys_page allocated & mapped\n",
           page_aligned);

    vm_map_count++;  /* 计入统计 */
    return PF_HANDLED;
}

/* ===================================================================
 *  原有 API（与旧版兼容）
 * =================================================================== */

void memory_init(void) {
    memset(page_used, 0, sizeof(page_used));
    used_pages = 0;

    kmalloc_offset = 0;
    free_list = NULL;
    kmalloc_alloc_count = 0;
    kmalloc_free_count  = 0;
    kmalloc_reuse_count = 0;

    vm_map_count   = 0;
    vm_unmap_count = 0;

    printf("[MiniOS] memory init ok\n");
}

void *alloc_page(void) {
    /* 线性扫描位图 —— O(n)，后续可优化为伙伴系统 */
    for (int i = 0; i < TOTAL_PAGES; i++) {
        if (page_used[i] == 0) {
            page_used[i] = 1;
            used_pages++;
            return &physical_memory[i * PAGE_SIZE];
        }
    }
    return NULL;  /* 物理内存耗尽 */
}

void free_page(void *page) {
    if (page == NULL) return;

    uintptr_t base = (uintptr_t)physical_memory;
    uintptr_t addr = (uintptr_t)page;

    /* 范围检查：指针必须在 physical_memory 数组之内 */
    if (addr < base || addr >= base + TOTAL_MEMORY_SIZE) {
        printf("[memory] invalid page address\n");
        return;
    }

    int index = (int)((addr - base) / PAGE_SIZE);

    if (index < 0 || index >= TOTAL_PAGES) {
        printf("[memory] invalid page index\n");
        return;
    }

    if (page_used[index] == 1) {
        page_used[index] = 0;
        used_pages--;
    }
    /* 如果 page_used[index] == 0，说明重复释放 —— 静默忽略 */
}

int get_total_pages(void) { return total_pages; }
int get_used_pages(void)  { return used_pages; }
int get_free_pages(void)  { return total_pages - used_pages; }

void memory_print_info(void) {
    printf("[Memory Info]\n");
    printf("total memory : %d KB\n", TOTAL_MEMORY_SIZE / 1024);
    printf("page size    : %d bytes\n", PAGE_SIZE);
    printf("total pages  : %d\n", get_total_pages());
    printf("used pages   : %d\n", get_used_pages());
    printf("free pages   : %d\n", get_free_pages());
    printf("kmalloc pool : %d KB\n", KMALLOC_POOL_SIZE / 1024);
    printf("kmalloc used : %d bytes\n", kmalloc_offset);
    printf("kmalloc alloc: %d times\n", kmalloc_alloc_count);
    printf("kmalloc free : %d times\n", kmalloc_free_count);
    printf("kmalloc reuse: %d times\n", kmalloc_reuse_count);

    /* 空闲链表统计 */
    int free_blocks = 0;
    int free_total  = 0;
    free_block_t *cur = free_list;
    while (cur) {
        free_blocks++;
        free_total += cur->size;
        cur = cur->next;
    }
    printf("free blocks  : %d (total %d bytes)\n", free_blocks, free_total);
}
