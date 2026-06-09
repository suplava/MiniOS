#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "memory.h"

static unsigned char physical_memory[TOTAL_MEMORY_SIZE];
static unsigned char page_used[TOTAL_PAGES];

static int total_pages = TOTAL_PAGES;
static int used_pages = 0;

#define KMALLOC_POOL_SIZE (128 * 1024)
static unsigned char kmalloc_pool[KMALLOC_POOL_SIZE];
static int kmalloc_offset = 0;

void memory_init(void) {
    memset(page_used, 0, sizeof(page_used));
    used_pages = 0;
    kmalloc_offset = 0;

    printf("[MiniOS] memory init ok\n");
}

void *alloc_page(void) {
    for (int i = 0; i < TOTAL_PAGES; i++) {
        if (page_used[i] == 0) {
            page_used[i] = 1;
            used_pages++;
            return &physical_memory[i * PAGE_SIZE];
        }
    }

    return NULL;
}

void free_page(void *page) {
    if (page == NULL) {
        return;
    }

    uintptr_t base = (uintptr_t)physical_memory;
    uintptr_t addr = (uintptr_t)page;

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
}

void *kmalloc(int size) {
    if (size <= 0) {
        return NULL;
    }

    if (kmalloc_offset + size > KMALLOC_POOL_SIZE) {
        return NULL;
    }

    void *ptr = &kmalloc_pool[kmalloc_offset];
    kmalloc_offset += size;

    return ptr;
}

void kfree(void *ptr) {
    /*
     * 当前是简化版 kmalloc。
     * 为了保证演示稳定，kfree 暂时不真正回收。
     * 后续可以扩展为空闲链表分配器。
     */
    (void)ptr;
}

int get_total_pages(void) {
    return total_pages;
}

int get_used_pages(void) {
    return used_pages;
}

int get_free_pages(void) {
    return total_pages - used_pages;
}

void memory_print_info(void) {
    printf("[Memory Info]\n");
    printf("total memory: %d KB\n", TOTAL_MEMORY_SIZE / 1024);
    printf("page size   : %d KB\n", PAGE_SIZE / 1024);
    printf("total pages : %d\n", get_total_pages());
    printf("used pages  : %d\n", get_used_pages());
    printf("free pages  : %d\n", get_free_pages());
    printf("kmalloc used: %d bytes\n", kmalloc_offset);
}
