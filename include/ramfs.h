#ifndef MINIOS_RAMFS_H
#define MINIOS_RAMFS_H

#define RAMFS_MAX_FILES 16
#define RAMFS_NAME_LEN 32
#define RAMFS_CONTENT_SIZE 256

typedef struct ramfs_file {
    int used;
    char name[RAMFS_NAME_LEN];
    char content[RAMFS_CONTENT_SIZE];
    int size;
} ramfs_file_t;

void ramfs_init(void);

int ramfs_create_file(const char *name);
int ramfs_write_file(const char *name, const char *content);
int ramfs_read_file(const char *name, char *buffer, int buffer_size);
int ramfs_delete_file(const char *name);

void ramfs_list_files(void);
void ramfs_print_file(const char *name);

#endif
