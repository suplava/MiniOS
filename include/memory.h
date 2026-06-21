#ifndef MINIOS_MEMORY_H
#define MINIOS_MEMORY_H

#include <stdint.h>

/* ================================================================
 * 物理内存常量
 * ================================================================ */
#define PAGE_SIZE            4096
#define TOTAL_MEMORY_SIZE    (16 * 1024 * 1024)   /* 16 MB 编译上限, 实际由 Multiboot 探测 */
#define TOTAL_PAGES          (TOTAL_MEMORY_SIZE / PAGE_SIZE)  /* 4096 页 */

/* ================================================================
 * 虚拟内存 —— 页表 / 页目录 标志位
 * ================================================================ */
#define VM_PRESENT  0x01   /* 页存在 */
#define VM_WRITABLE 0x02   /* 可写 */
#define VM_USER     0x04   /* 用户态可访问 */
#define VM_EXEC     0x08   /* 可执行 */

/*
 * 32 位虚拟地址拆分：
 *   bits 31..22  → PDE 索引 (1024 项)
 *   bits 21..12  → PTE 索引 (1024 项)
 *   bits 11..0   → 页内偏移 (4 KB)
 */
#define PDE_INDEX(vaddr)  (((vaddr) >> 22) & 0x3FF)
#define PTE_INDEX(vaddr)  (((vaddr) >> 12) & 0x3FF)
#define PAGE_OFFSET(vaddr) ((vaddr) & 0xFFF)

/* ================================================================
 * 页目录项 (PDE)
 *   - present    : 该页表是否存在
 *   - writable   : 该 4 MB 区域是否可写
 *   - user       : 用户态访问权限
 *   - table_addr : 页表物理地址 (已右移 12 位，天然 4KB 对齐)
 * ================================================================ */
typedef struct pde {
    uint32_t present    : 1;
    uint32_t writable   : 1;
    uint32_t user       : 1;
    uint32_t reserved   : 9;
    uint32_t table_addr : 20;
} pde_t;

/* ================================================================
 * 页表项 (PTE)
 *   - present   : 该物理页是否存在
 *   - writable  : 该 4 KB 页是否可写
 *   - user      : 用户态访问权限
 *   - page_addr : 物理页地址 (已右移 12 位)
 * ================================================================ */
typedef struct pte {
    uint32_t present   : 1;
    uint32_t writable  : 1;
    uint32_t user      : 1;
    uint32_t reserved  : 9;
    uint32_t page_addr : 20;
} pte_t;

/*
 * 页目录: 1024 个 PDE，每个 4 字节 → 正好 4096 字节 = 1 页
 * 页  表: 1024 个 PTE，每个 4 字节 → 正好 4096 字节 = 1 页
 */
typedef struct page_directory {
    pde_t entries[1024];
} page_directory_t;

typedef struct page_table {
    pte_t entries[1024];
} page_table_t;

/* ================================================================
 * 虚拟内存区域 (VMA) — 描述进程地址空间中的一段映射
 *
 * 每个进程有一个 VMA 链表，记录哪些虚拟地址范围是合法的。
 * 缺页处理 (page fault) 时遍历此链表决定是否应该分配物理页。
 *
 *   典型布局:
 *     0x08048000 ─ 代码段 (.text)   R+X
 *     0x08049000 ─ 数据段 (.data)   R+W
 *     0x0804A000 ─ 堆 (heap)       R+W  向上增长
 *     0xBFFFE000 ─ 栈 (stack)      R+W  向下增长 (VM_GROWSDOWN)
 * ================================================================ */

/* VMA 权限标志（与 PDE/PTE 的 VM_* 通用） */
#define VM_GROWSDOWN  0x10   /* 栈：缺页时向下扩展 */

typedef struct vm_area {
    uint32_t          start;    /* 起始虚拟地址 (页对齐) */
    uint32_t          end;      /* 结束虚拟地址 (页对齐) */
    uint32_t          flags;    /* VM_PRESENT | VM_WRITABLE | VM_USER | VM_GROWSDOWN */
    struct vm_area   *next;     /* 链表下一节点 */
} vm_area_t;

/* 缺页处理结果 */
typedef enum {
    PF_HANDLED  = 0,   /* 缺页已处理，页已映射 */
    PF_SEGFAULT = -1,  /* 非法地址 → 段错误 */
    PF_NOMEM    = -2   /* 物理内存耗尽 */
} pf_result_t;

/* VMA 链表操作 */
vm_area_t   *vm_area_create(uint32_t start, uint32_t end, uint32_t flags);
void         vm_area_destroy_all(vm_area_t *head);
vm_area_t   *vm_area_find(vm_area_t *head, uint32_t vaddr);
int          vm_area_add(vm_area_t **head, uint32_t start,
                         uint32_t end, uint32_t flags);

/* 缺页处理 (Demand Paging) */
pf_result_t  vm_handle_page_fault(page_directory_t *pdir,
                                   vm_area_t *vma_list,
                                   uint32_t vaddr,
                                   int is_write);

/* ================================================================
 * 物理内存管理 API
 * ================================================================ */
void  memory_init(void);

void *alloc_page(void);
void  free_page(void *page);

void *kmalloc(int size);
void  kfree(void *ptr);

int   get_total_pages(void);
int   get_used_pages(void);
int   get_free_pages(void);

void  memory_print_info(void);

/* ================================================================
 * 虚拟内存管理 API
 * ================================================================ */
void                vm_init(void);
page_directory_t   *vm_create_address_space(void);
int                 vm_map_page(page_directory_t *pdir, uint32_t vaddr,
                                uint32_t paddr, uint32_t flags);
int                 vm_unmap_page(page_directory_t *pdir, uint32_t vaddr);
uint32_t            vm_translate(page_directory_t *pdir,
                                 vm_area_t *vma_list,
                                 uint32_t vaddr, int is_write);
void                vm_destroy_address_space(page_directory_t *pdir);
void                vm_print_stats(page_directory_t *pdir);

/* 虚拟 → 物理 地址辅助宏 */
uint32_t            vm_phys_to_index(void *phys_ptr);
void               *vm_index_to_phys(uint32_t index);

#endif
