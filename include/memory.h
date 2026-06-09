#ifndef MINIOS_MEMORY_H
#define MINIOS_MEMORY_H

#define PAGE_SIZE 4096
#define TOTAL_MEMORY_SIZE (16 * 1024 * 1024)
#define TOTAL_PAGES (TOTAL_MEMORY_SIZE / PAGE_SIZE)

void memory_init(void);

void *alloc_page(void);
void free_page(void *page);

void *kmalloc(int size);
void kfree(void *ptr);

int get_total_pages(void);
int get_used_pages(void);
int get_free_pages(void);

void memory_print_info(void);

#endif
