#include <stdio.h>
#include <string.h>
#include "ramfs.h"

static ramfs_file_t file_table[RAMFS_MAX_FILES];

static int find_file_index(const char *name) {
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (file_table[i].used && strcmp(file_table[i].name, name) == 0) {
            return i;
        }
    }

    return -1;
}

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

    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!file_table[i].used) {
            file_table[i].used = 1;
            strcpy(file_table[i].name, name);
            file_table[i].content[0] = '\0';
            file_table[i].size = 0;

            printf("[ramfs] create file ok: %s\n", name);
            return 0;
        }
    }

    printf("[ramfs] create failed: file table full\n");
    return -1;
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

    file_table[index].used = 0;
    file_table[index].name[0] = '\0';
    file_table[index].content[0] = '\0';
    file_table[index].size = 0;

    printf("[ramfs] delete file ok: %s\n", name);
    return 0;
}

void ramfs_list_files(void) {
    int count = 0;

    printf("NAME                           SIZE\n");

    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (file_table[i].used) {
            printf("%-30s %d bytes\n", file_table[i].name, file_table[i].size);
            count++;
        }
    }

    if (count == 0) {
        printf("[ramfs] no files\n");
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
