/**
 * ================================================================
 *  MiniOS RAMFS (内存文件系统)
 *
 *  特点：
 *    - 所有文件存储在内存中，断电即失
 *    - 支持 16 个文件，每个最大 256 字节
 *    - 文件名最长 32 字符
 *
 *  QEMU 移植说明：
 *    - 已删除 stdio.h / string.h
 *    - 已删除 printf()
 *    - 改用 printk() / print_int()
 *    - 内部实现 strlen / strcmp / strcpy / strncpy / memset
 * ================================================================
 */

#include "ramfs.h"
#include "console.h"

static ramfs_file_t file_table[RAMFS_MAX_FILES];

/* ============================================================
 *  基础字符串 / 内存函数
 *  裸机 QEMU 环境下不能依赖标准 C 库
 * ============================================================ */

static int mini_strlen(const char *s) {
    int len = 0;

    if (s == 0) {
        return 0;
    }

    while (s[len] != '\0') {
        len++;
    }

    return len;
}

static int mini_strcmp(const char *a, const char *b) {
    int i = 0;

    if (a == 0 && b == 0) {
        return 0;
    }

    if (a == 0) {
        return -1;
    }

    if (b == 0) {
        return 1;
    }

    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return (unsigned char)a[i] - (unsigned char)b[i];
        }
        i++;
    }

    return (unsigned char)a[i] - (unsigned char)b[i];
}

static void mini_strcpy(char *dst, const char *src) {
    int i = 0;

    if (dst == 0 || src == 0) {
        return;
    }

    while (src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static void mini_strncpy(char *dst, const char *src, int n) {
    int i = 0;

    if (dst == 0 || n <= 0) {
        return;
    }

    if (src == 0) {
        dst[0] = '\0';
        return;
    }

    while (i < n && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    while (i < n) {
        dst[i] = '\0';
        i++;
    }
}

static void mini_memset(void *ptr, int value, int size) {
    unsigned char *p = (unsigned char *)ptr;

    if (ptr == 0 || size <= 0) {
        return;
    }

    for (int i = 0; i < size; i++) {
        p[i] = (unsigned char)value;
    }
}

/* ============================================================
 *  输出辅助函数
 * ============================================================ */

static void print_kv_int(const char *key, int value, const char *suffix) {
    printk(key);
    print_int(value);

    if (suffix != 0) {
        printk(suffix);
    }

    printk("\n");
}

static void print_spaces(int n) {
    for (int i = 0; i < n; i++) {
        printk(" ");
    }
}

/* ============================================================
 *  内部辅助函数
 * ============================================================ */

static int find_file_index(const char *name) {
    if (name == 0) {
        return -1;
    }

    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (file_table[i].used && mini_strcmp(file_table[i].name, name) == 0) {
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
    mini_memset(file_table, 0, sizeof(file_table));

    ramfs_create_file("hello.txt");
    ramfs_write_file("hello.txt", "Hello from MiniOS RAMFS!");

    ramfs_create_file("readme.txt");
    ramfs_write_file("readme.txt", "MiniOS supports mem, ps, run, kill, ls, cat, touch, write and echo.");

    printk("[MiniOS] ramfs init ok\n");
}

int ramfs_create_file(const char *name) {
    if (name == 0 || mini_strlen(name) == 0) {
        printk("[ramfs] invalid file name\n");
        return -1;
    }

    if (mini_strlen(name) >= RAMFS_NAME_LEN) {
        printk("[ramfs] file name too long\n");
        return -1;
    }

    if (find_file_index(name) != -1) {
        printk("[ramfs] file already exists: ");
        printk(name);
        printk("\n");
        return -1;
    }

    int idx = find_free_slot();

    if (idx == -1) {
        printk("[ramfs] create failed: file table full\n");
        return -1;
    }

    file_table[idx].used = 1;
    mini_strcpy(file_table[idx].name, name);
    file_table[idx].content[0] = '\0';
    file_table[idx].size = 0;

    printk("[ramfs] create file ok: ");
    printk(name);
    printk("\n");

    return 0;
}

int ramfs_write_file(const char *name, const char *content) {
    int index = find_file_index(name);

    if (index == -1) {
        printk("[ramfs] file not found: ");

        if (name != 0) {
            printk(name);
        } else {
            printk("(null)");
        }

        printk("\n");
        return -1;
    }

    if (content == 0) {
        content = "";
    }

    mini_strncpy(file_table[index].content, content, RAMFS_CONTENT_SIZE - 1);
    file_table[index].content[RAMFS_CONTENT_SIZE - 1] = '\0';
    file_table[index].size = mini_strlen(file_table[index].content);

    printk("[ramfs] write file ok: ");
    printk(name);
    printk("\n");

    return 0;
}

int ramfs_read_file(const char *name, char *buffer, int buffer_size) {
    int index = find_file_index(name);

    if (index == -1) {
        printk("[ramfs] file not found: ");

        if (name != 0) {
            printk(name);
        } else {
            printk("(null)");
        }

        printk("\n");
        return -1;
    }

    if (buffer == 0 || buffer_size <= 0) {
        printk("[ramfs] invalid read buffer\n");
        return -1;
    }

    mini_strncpy(buffer, file_table[index].content, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';

    return file_table[index].size;
}

int ramfs_delete_file(const char *name) {
    int index = find_file_index(name);

    if (index == -1) {
        printk("[ramfs] file not found: ");

        if (name != 0) {
            printk(name);
        } else {
            printk("(null)");
        }

        printk("\n");
        return -1;
    }

    file_table[index].used = 0;
    mini_memset(file_table[index].name, 0, RAMFS_NAME_LEN);
    mini_memset(file_table[index].content, 0, RAMFS_CONTENT_SIZE);
    file_table[index].size = 0;

    printk("[ramfs] delete file ok: ");
    printk(name);
    printk("\n");

    return 0;
}

void ramfs_print_info(void) {
    int used = 0;
    int total_size = 0;
    int max_size = 0;
    char *max_name = 0;

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

    printk("[RAMFS Filesystem Info]\n");

    print_kv_int("  total files  : ", used, "");
    print_kv_int("  total size   : ", total_size, " bytes");

    printk("  max file     : ");

    if (max_name != 0) {
        printk(max_name);
    } else {
        printk("(none)");
    }

    printk(" (");
    print_int(max_size);
    printk(" bytes)\n");

    print_kv_int("  free slots   : ", RAMFS_MAX_FILES - used, "");
    print_kv_int("  max file size: ", RAMFS_CONTENT_SIZE, " bytes per file");
}

void ramfs_list_files(void) {
    int count = 0;
    int total_bytes = 0;

    printk("NAME                          SIZE\n");

    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (file_table[i].used) {
            int name_len;

            if (file_table[i].name[0] == '\0') {
                continue;
            }

            printk(file_table[i].name);

            name_len = mini_strlen(file_table[i].name);

            if (name_len < 30) {
                print_spaces(30 - name_len);
            } else {
                printk(" ");
            }

            print_int(file_table[i].size);
            printk(" bytes\n");

            count++;
            total_bytes += file_table[i].size;
        }
    }

    if (count == 0) {
        printk("[ramfs] no files\n");
    } else {
        printk("----\n");
        print_int(count);
        printk(" files, ");
        print_int(total_bytes);
        printk(" bytes total\n");
    }
}

void ramfs_print_file(const char *name) {
    int index = find_file_index(name);

    if (index == -1) {
        printk("[ramfs] file not found: ");

        if (name != 0) {
            printk(name);
        } else {
            printk("(null)");
        }

        printk("\n");
        return;
    }

    printk(file_table[index].content);
    printk("\n");
}
