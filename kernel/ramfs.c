/**
 * ================================================================
 *  MiniOS RAMFS (内存文件系统)
 *
 *  特点：
 *    - 所有文件存储在内存中，断电即失
 *    - 支持 16 个文件，每个最大 256 字节
 *    - 文件名最长 32 字符
 *
 *  优化说明：
 *    - 统一使用 find_free_slot() 消除重复代码
 *    - find_file_index() 增加 NULL 防御
 *    - list 增加空文件名防御检查
 *    - write 使用 strncpy 防止 256 字节边界溢出
 * ================================================================ */
#include <stdio.h>
#include <string.h>
#include "ramfs.h"

static ramfs_file_t file_table[RAMFS_MAX_FILES];

/* ============================================================
 *  内部辅助函数
 * ============================================================ */

static int find_file_index(const char *name) {
    if (name == NULL) return -1;                      // [OPT] 防御
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (file_table[i].used && strcmp(file_table[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_free_slot(void) {
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!file_table[i].used) {
            return i;
        }
    }
    return -1;
}

/* ============================================================
 *  公开函数
 * ============================================================ */

void ramfs_init(void) {
    memset(file_table, 0, sizeof(file_table));

    ramfs_create_file("hello.txt");
    ramfs_write_file("hello.txt", "Hello from MiniOS RAMFS!");

    ramfs_create_file("readme.txt");
    ramfs_write_file("readme.txt", "MiniOS supports mem, ps, run, kill, ls, cat, touch, write and echo.");

    printf("[MiniOS] ramfs init ok\n");
}

int ramfs_create_file(const char *name) {
    if (name == NULL || strlen(name) == 0) {
        printf("[ramfs] invalid file name\n");
        return -1;
    }

    if (strlen(name) >= RAMFS_NAME_LEN) {
        printf("[ramfs] file name too long\n");
        return -1;
    }

    if (find_file_index(name) != -1) {
        printf("[ramfs] file already exists: %s\n", name);
        return -1;
    }

    // [OPT] 使用 find_free_slot() 替代手写循环
    int idx = find_free_slot();
    if (idx == -1) {
        printf("[ramfs] create failed: file table full\n");
        return -1;
    }

    file_table[idx].used = 1;
    strcpy(file_table[idx].name, name);
    file_table[idx].content[0] = '\0';
    file_table[idx].size = 0;

    printf("[ramfs] create file ok: %s\n", name);
    return 0;
}

int ramfs_write_file(const char *name, const char *content) {
    int index = find_file_index(name);

    if (index == -1) {
        printf("[ramfs] file not found: %s\n", name);
        return -1;
    }

    if (content == NULL) {
        content = "";
    }

    // [OPT] 安全复制，留 1 字节给 '\0'，杜绝缓冲区溢出
    strncpy(file_table[index].content, content, RAMFS_CONTENT_SIZE - 1);
    file_table[index].content[RAMFS_CONTENT_SIZE - 1] = '\0';
    file_table[index].size = (int)strlen(file_table[index].content);

    printf("[ramfs] write file ok: %s\n", name);
    return 0;
}

int ramfs_read_file(const char *name, char *buffer, int buffer_size) {
    int index = find_file_index(name);

    if (index == -1) {
        printf("[ramfs] file not found: %s\n", name);
        return -1;
    }

    if (buffer == NULL || buffer_size <= 0) {
        printf("[ramfs] invalid read buffer\n");
        return -1;
    }

    strncpy(buffer, file_table[index].content, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';

    return file_table[index].size;
}

int ramfs_delete_file(const char *name) {
    int index = find_file_index(name);

    if (index == -1) {
        printf("[ramfs] file not found: %s\n", name);
        return -1;
    }

    // [OPT] 彻底清空所有字段（更安全）
    file_table[index].used = 0;
    memset(file_table[index].name, 0, RAMFS_NAME_LEN);
    memset(file_table[index].content, 0, RAMFS_CONTENT_SIZE);
    file_table[index].size = 0;

    printf("[ramfs] delete file ok: %s\n", name);
    return 0;
}

void ramfs_print_info(void) {
    int used = 0;
    int total_size = 0;
    int max_size = 0;
    char *max_name = NULL;

    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (file_table[i].used) {
            used++;
            total_size += file_table[i].size;

            if (file_table[i].size > max_size) {
                max_size = file_table[i].size;
                max_name = file_table[i].name;
            }
        }
    }

    printf("[RAMFS Filesystem Info]\n");
    printf("  total files  : %d\n", used);
    printf("  total size   : %d bytes\n", total_size);
    printf("  max file     : %s (%d bytes)\n",
           max_name ? max_name : "(none)",
           max_size);
    printf("  free slots   : %d\n", RAMFS_MAX_FILES - used);
    printf("  max file size: %d bytes per file\n", RAMFS_CONTENT_SIZE);
}

void ramfs_list_files(void) {
    int count = 0;
    int total_bytes = 0;

    printf("NAME                           SIZE\n");

    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (file_table[i].used) {
            // [OPT] 防御：如果文件名为空，跳过（防崩溃）
            if (file_table[i].name[0] == '\0') {
                continue;
            }
            printf("%-30s %d bytes\n", file_table[i].name, file_table[i].size);
            count++;
            total_bytes += file_table[i].size;
        }
    }

    if (count == 0) {
        printf("[ramfs] no files\n");
    } else {
        printf("----\n");
        printf("%d files, %d bytes total\n", count, total_bytes);
    }
}

void ramfs_print_file(const char *name) {
    int index = find_file_index(name);

    if (index == -1) {
        printf("[ramfs] file not found: %s\n", name);
        return;
    }

    printf("%s\n", file_table[index].content);
}