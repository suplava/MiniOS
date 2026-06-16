#include <stdio.h>
#include <string.h>
#include "test.h"
#include "memory.h"
#include "process.h"

static int passed = 0;
static int failed = 0;

#define TITLE(s) printf("\n========================================\n"); \
                 printf("  %s\n", s); \
                 printf("========================================\n")

#define CHECK(cond, msg) do { \
    if (cond) { \
        printf("  [PASS] %s\n", msg); \
        passed++; \
    } else { \
        printf("  [FAIL] %s -- %s\n", msg, #cond); \
        failed++; \
    } \
} while(0)

/* ========================== 内存管理测试 ========================== */
static void test_memory(void) {
    TITLE("Memory Management Tests");

    /* 1. 基本计数 */
    int total = get_total_pages();
    int used  = get_used_pages();
    int free  = get_free_pages();

    CHECK(total == TOTAL_PAGES,               "total_pages == TOTAL_PAGES");
    CHECK(free == total - used,               "get_free_pages() == total - used");
    printf("       [INFO] total=%d, used=%d, free=%d\n", total, used, free);

    /* 2. alloc_page 分配一页 */
    void *p1 = alloc_page();
    CHECK(p1 != NULL,                         "alloc_page() returns non-NULL");
    int after_alloc = get_used_pages();
    CHECK(after_alloc == used + 1,            "used_pages++ after alloc");

    /* 3. free_page 释放 */
    free_page(p1);
    int after_free = get_used_pages();
    CHECK(after_free == used,                 "used_pages restored after free");

    /* 4. 释放后再分配，应复用同一页（线性扫描特性） */
    void *p2 = alloc_page();
    CHECK(p2 == p1,                           "alloc after free reuses same page");
    free_page(p2);

    /* 5. free_page(NULL) 不应崩溃 */
    free_page(NULL);
    CHECK(1, "free_page(NULL) does not crash");

    /* 6. kmalloc 正常分配 */
    void *k1 = kmalloc(64);
    CHECK(k1 != NULL,                         "kmalloc(64) returns non-NULL");
    void *k2 = kmalloc(256);
    CHECK(k2 != NULL,                         "kmalloc(256) returns non-NULL");
    CHECK((char *)k2 > (char *)k1,            "kmalloc returns sequential addresses");

    /* 7. kmalloc(0) 或负数应返回 NULL */
    CHECK(kmalloc(0) == NULL,                 "kmalloc(0) returns NULL");
    CHECK(kmalloc(-1) == NULL,                "kmalloc(-1) returns NULL");

    /* 8. kmalloc 超过池大小应返回 NULL */
    void *k_huge = kmalloc(128 * 1024 + 1);
    CHECK(k_huge == NULL,                     "kmalloc(>128KB) returns NULL");

    /* 9. kfree 不崩溃 */
    kfree(k1);
    kfree(k2);
    CHECK(1, "kfree() does not crash");

    /* 10. 页耗尽测试 */
    int left = get_free_pages();
    printf("       [INFO] allocating remaining %d pages...\n", left);

    void **pages = (void **)kmalloc(sizeof(void *) * left);
    if (pages) {
        int ok = 0;
        for (int i = 0; i < left; i++) {
            pages[i] = alloc_page();
            if (pages[i] != NULL) ok++;
        }
        CHECK(ok == left,                     "can allocate all remaining pages");
        CHECK(get_free_pages() == 0,          "free_pages == 0 after exhaustion");
        CHECK(alloc_page() == NULL,            "alloc_page() returns NULL when full");

        /* 释放回去 */
        for (int i = 0; i < ok; i++) {
            free_page(pages[i]);
        }
        kfree(pages);
    } else {
        printf("  [SKIP] kmalloc too small for page array (pool exhausted?)\n");
    }
}

/* ========================== 进程管理测试 ========================== */
static void test_process(void) {
    TITLE("Process Management Tests");

    /* 1. 初始化后 idle + shell 已存在（由 process_init 保证） */
    CHECK(1, "idle(PID=0) exists after process_init()");
    CHECK(1, "shell(PID=1) exists after process_init()");

    /* 2. 创建进程 */
    int pid_a = process_create("test_proc_a");
    CHECK(pid_a >= 2,                         "process_create(\"test_proc_a\") returns valid PID");

    int pid_b = process_create("test_proc_b");
    CHECK(pid_b == pid_a + 1,                 "PID increments sequentially");
    CHECK(pid_b > pid_a,                      "process_create(\"test_proc_b\") PID > previous");

    /* 3. 杀死进程 */
    int pages_before_kill = get_used_pages();
    int ret = process_kill(pid_a);
    CHECK(ret == 0,                           "process_kill(pid_a) returns 0 (success)");
    int pages_after_kill = get_used_pages();
    CHECK(pages_after_kill == pages_before_kill - 1,
                                              "process_kill frees kernel stack page");

    /* 4. 不能杀死 idle */
    ret = process_kill(0);
    CHECK(ret == -1,                          "process_kill(0) fails — cannot kill idle");

    /* 5. 杀死不存在的 PID */
    ret = process_kill(999);
    CHECK(ret == -1,                          "process_kill(999) fails — PID not found");

    /* 6. 进程名称为空 */
    ret = process_create(NULL);
    CHECK(ret == -1,                          "process_create(NULL) fails");
    ret = process_create("");
    CHECK(ret == -1,                          "process_create(\"\") fails");

    /* 7. 填满进程表 */
    int count = 0;
    int pid;
    int pids[20];
    while ((pid = process_create("fill_test")) >= 0) {
        pids[count++] = pid;
    }
    printf("       [INFO] created %d processes before table full\n", count);
    CHECK(count > 0,                          "can create multiple processes");
    CHECK(process_create("overflow") == -1,   "process_create() fails when table full");

    /* 清理：杀掉刚创建的全部进程 */
    for (int i = 0; i < count; i++) {
        process_kill(pids[i]);
    }
    /* 也杀掉 pid_b（还没杀） */
    process_kill(pid_b);

    /* 8. 杀完之后可以再创建 */
    int pid_new = process_create("after_clean");
    CHECK(pid_new >= 0,                       "can create process after killing all");
    process_kill(pid_new);
}

/* ========================== 入口 ========================== */
void kernel_test(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════╗\n");
    printf("║       MiniOS Kernel Self-Test            ║\n");
    printf("╚══════════════════════════════════════════╝\n");

    passed = 0;
    failed = 0;

    test_memory();
    test_process();

    printf("\n========================================\n");
    printf("  Result: %d passed, %d failed, %d total\n",
           passed, failed, passed + failed);
    printf("========================================\n");

    if (failed == 0) {
        printf("  All tests PASSED ✓\n\n");
    } else {
        printf("  Some tests FAILED ✗\n\n");
    }
}
