#ifndef MINIOS_RAMFS_H
#define MINIOS_RAMFS_H

#define RAMFS_MAX_NODES        128
#define RAMFS_MAX_FILES        RAMFS_MAX_NODES
#define RAMFS_MAX_FD           32
#define RAMFS_NAME_LEN         32
#define RAMFS_PATH_LEN         128
#define RAMFS_CONTENT_SIZE     (64 * 1024)

#define RAMFS_O_RDONLY         0x1
#define RAMFS_O_WRONLY         0x2
#define RAMFS_O_RDWR           0x3

typedef enum ramfs_node_type {
    RAMFS_NODE_FILE = 1,
    RAMFS_NODE_DIR  = 2
} ramfs_node_type_t;

typedef struct ramfs_node {
    int used;
    int type;
    char name[RAMFS_NAME_LEN];
    int parent;
    int size;
    char content[RAMFS_CONTENT_SIZE];
} ramfs_node_t;

typedef struct ramfs_fd {
    int used;
    int node_index;
    int offset;
    int flags;
} ramfs_fd_t;

void ramfs_init(void);
void ramfs_format(void);

/* 兼容原有接口 */
int ramfs_create_file(const char *path);
int ramfs_write_file(const char *path, const char *content);
int ramfs_read_file(const char *path, char *buffer, int buffer_size);
int ramfs_delete_file(const char *path);
void ramfs_print_info(void);
void ramfs_list_files(void);
void ramfs_list_path(const char *path);
void ramfs_print_file(const char *path);

/* 新增目录与路径接口 */
int ramfs_mkdir(const char *path);
int ramfs_chdir(const char *path);
void ramfs_pwd(void);

/* 新增文件描述符接口 */
int ramfs_open(const char *path, int flags);
int ramfs_close(int fd);
int ramfs_read(int fd, char *buf, int len);
int ramfs_write(int fd, const char *buf, int len);
int ramfs_seek(int fd, int offset);

/* 演示用 */
void ramfs_fdtest(void);

#endif
