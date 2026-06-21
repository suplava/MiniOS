/**
 * ================================================================
 *  MiniOS RAMFS v2
 *
 *  支持能力：
 *   - 最大 128 个文件/目录节点
 *   - 单文件最大 64KB
 *   - 支持根目录、多级目录
 *   - 支持绝对路径与相对路径
 *   - 支持 mkdir / cd / pwd / ls
 *   - 支持 touch / write / cat / rm
 *   - 支持 open / read / write / close / seek 文件描述符表
 *   - 支持 mkfs/format 清空重建
 *
 *  说明：
 *   - 该文件系统为教学型 RAMFS，数据保存在内存中，断电即失。
 *   - 为适配 QEMU 裸机环境，不依赖 stdio.h / string.h。
 * ================================================================
 */

#include "ramfs.h"
#include "console.h"
#include "hal.h"

static ramfs_node_t node_table[RAMFS_MAX_NODES];
static ramfs_fd_t fd_table[RAMFS_MAX_FD];
static int cwd_index = 0;

/* ============================================================
 *  裸机基础字符串 / 内存函数
 * ============================================================ */

static int mini_strlen(const char *s) {
    int len = 0;
    if (s == 0) return 0;
    while (s[len] != '\0') len++;
    return len;
}

static int mini_strcmp(const char *a, const char *b) {
    int i = 0;

    if (a == 0 && b == 0) return 0;
    if (a == 0) return -1;
    if (b == 0) return 1;

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

    if (dst == 0) return;

    if (src == 0) {
        dst[0] = '\0';
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

    if (dst == 0 || n <= 0) return;

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

    if (ptr == 0 || size <= 0) return;

    for (int i = 0; i < size; i++) {
        p[i] = (unsigned char)value;
    }
}

static void mini_memcpy(char *dst, const char *src, int n) {
    if (dst == 0 || src == 0 || n <= 0) return;

    for (int i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}

/* ============================================================
 *  输出辅助
 * ============================================================ */

static void print_kv_int(const char *key, int value, const char *suffix) {
    printk(key);
    print_int(value);
    if (suffix != 0) printk(suffix);
    printk("\n");
}

static void print_spaces(int n) {
    for (int i = 0; i < n; i++) printk(" ");
}

static void print_node_type(int type) {
    if (type == RAMFS_NODE_DIR) {
        printk("<DIR>");
    } else {
        printk("FILE ");
    }
}

/* ============================================================
 *  节点辅助
 * ============================================================ */

static int find_free_node(void) {
    for (int i = 0; i < RAMFS_MAX_NODES; i++) {
        if (!node_table[i].used) return i;
    }

    return -1;
}

static int find_child(int parent, const char *name) {
    if (parent < 0 || parent >= RAMFS_MAX_NODES || name == 0) return -1;
    if (!node_table[parent].used || node_table[parent].type != RAMFS_NODE_DIR) return -1;

    for (int i = 0; i < RAMFS_MAX_NODES; i++) {
        if (node_table[i].used &&
            node_table[i].parent == parent &&
            mini_strcmp(node_table[i].name, name) == 0) {
            return i;
        }
    }

    return -1;
}

static int dir_is_empty(int dir_index) {
    for (int i = 0; i < RAMFS_MAX_NODES; i++) {
        if (node_table[i].used && node_table[i].parent == dir_index) {
            return 0;
        }
    }

    return 1;
}

/* ============================================================
 *  路径解析
 * ============================================================ */

static int read_component(const char *path, int *pos, char *name) {
    int i = 0;

    if (path == 0 || pos == 0 || name == 0) return 0;

    while (path[*pos] == '/') {
        (*pos)++;
    }

    if (path[*pos] == '\0') {
        name[0] = '\0';
        return 0;
    }

    while (path[*pos] != '\0' && path[*pos] != '/') {
        if (i < RAMFS_NAME_LEN - 1) {
            name[i++] = path[*pos];
        }
        (*pos)++;
    }

    name[i] = '\0';
    return 1;
}

static int ramfs_resolve_path(const char *path) {
    int cur;
    int pos;
    char comp[RAMFS_NAME_LEN];

    if (path == 0 || mini_strlen(path) == 0) {
        return cwd_index;
    }

    if (path[0] == '/') {
        cur = 0;
        pos = 1;
    } else {
        cur = cwd_index;
        pos = 0;
    }

    if (path[0] == '/' && path[1] == '\0') {
        return 0;
    }

    while (read_component(path, &pos, comp)) {
        if (mini_strcmp(comp, ".") == 0) {
            continue;
        }

        if (mini_strcmp(comp, "..") == 0) {
            if (cur != 0) cur = node_table[cur].parent;
            continue;
        }

        cur = find_child(cur, comp);
        if (cur == -1) {
            return -1;
        }
    }

    return cur;
}

static void trim_trailing_slash(char *path) {
    int len = mini_strlen(path);

    while (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }
}

static int ramfs_resolve_parent(const char *path, char *name_out) {
    char tmp[RAMFS_PATH_LEN];
    int len;
    int slash = -1;

    if (path == 0 || name_out == 0 || mini_strlen(path) == 0) {
        return -1;
    }

    mini_strncpy(tmp, path, RAMFS_PATH_LEN - 1);
    tmp[RAMFS_PATH_LEN - 1] = '\0';
    trim_trailing_slash(tmp);

    len = mini_strlen(tmp);
    if (len == 0) return -1;

    for (int i = len - 1; i >= 0; i--) {
        if (tmp[i] == '/') {
            slash = i;
            break;
        }
    }

    if (slash == -1) {
        mini_strncpy(name_out, tmp, RAMFS_NAME_LEN - 1);
        name_out[RAMFS_NAME_LEN - 1] = '\0';
        return cwd_index;
    }

    if (slash == 0) {
        if (tmp[1] == '\0') return -1;
        mini_strncpy(name_out, tmp + 1, RAMFS_NAME_LEN - 1);
        name_out[RAMFS_NAME_LEN - 1] = '\0';
        return 0;
    }

    tmp[slash] = '\0';
    mini_strncpy(name_out, tmp + slash + 1, RAMFS_NAME_LEN - 1);
    name_out[RAMFS_NAME_LEN - 1] = '\0';

    return ramfs_resolve_path(tmp);
}

static void ramfs_print_full_path_rec(int index) {
    if (index == 0) {
        printk("/");
        return;
    }

    if (node_table[index].parent != 0) {
        ramfs_print_full_path_rec(node_table[index].parent);
    } else {
        printk("/");
    }

    printk(node_table[index].name);

    if (node_table[index].type == RAMFS_NODE_DIR) {
        printk("/");
    }
}

/* ============================================================
 *  格式化 / 初始化
 * ============================================================ */

void ramfs_format(void) {
    mini_memset(node_table, 0, sizeof(node_table));
    mini_memset(fd_table, 0, sizeof(fd_table));

    node_table[0].used = 1;
    node_table[0].type = RAMFS_NODE_DIR;
    mini_strcpy(node_table[0].name, "/");
    node_table[0].parent = 0;
    node_table[0].size = 0;

    cwd_index = 0;

    ramfs_create_file("/hello.txt");
    ramfs_write_file("/hello.txt", "Hello from MiniOS RAMFS v2!");

    ramfs_create_file("/readme.txt");
    ramfs_write_file("/readme.txt", "RAMFS v2 supports mkdir, cd, pwd, path, fd, seek and mkfs.");
}

void ramfs_init(void) {
    ramfs_format();
    printk("[MiniOS] ramfs init ok\n");
}

/* ============================================================
 *  目录操作
 * ============================================================ */

int ramfs_mkdir(const char *path) {
    char name[RAMFS_NAME_LEN];
    int parent;
    int idx;

    parent = ramfs_resolve_parent(path, name);

    if (parent == -1) {
        printk("[ramfs] mkdir failed: invalid parent path\n");
        return -1;
    }

    if (node_table[parent].type != RAMFS_NODE_DIR) {
        printk("[ramfs] mkdir failed: parent is not directory\n");
        return -1;
    }

    if (mini_strlen(name) == 0 || mini_strlen(name) >= RAMFS_NAME_LEN) {
        printk("[ramfs] mkdir failed: invalid directory name\n");
        return -1;
    }

    if (find_child(parent, name) != -1) {
        printk("[ramfs] mkdir failed: node already exists\n");
        return -1;
    }

    idx = find_free_node();
    if (idx == -1) {
        printk("[ramfs] mkdir failed: node table full\n");
        return -1;
    }

    node_table[idx].used = 1;
    node_table[idx].type = RAMFS_NODE_DIR;
    node_table[idx].parent = parent;
    node_table[idx].size = 0;
    mini_strcpy(node_table[idx].name, name);

    printk("[ramfs] mkdir ok: ");
    printk(path);
    printk("\n");
    printf("[VIZ]{\"type\":\"ramfs_mkdir\",\"path\":\"%s\"}\n", path);

    return 0;
}

int ramfs_chdir(const char *path) {
    int idx = ramfs_resolve_path(path);

    if (idx == -1) {
        printk("[ramfs] cd failed: path not found\n");
        return -1;
    }

    if (node_table[idx].type != RAMFS_NODE_DIR) {
        printk("[ramfs] cd failed: not a directory\n");
        return -1;
    }

    cwd_index = idx;
    return 0;
}

void ramfs_pwd(void) {
    ramfs_print_full_path_rec(cwd_index);
    printk("\n");
}

/* ============================================================
 *  文件 CRUD
 * ============================================================ */

int ramfs_create_file(const char *path) {
    char name[RAMFS_NAME_LEN];
    int parent;
    int idx;

    parent = ramfs_resolve_parent(path, name);

    if (parent == -1) {
        printk("[ramfs] create failed: invalid parent path\n");
        return -1;
    }

    if (node_table[parent].type != RAMFS_NODE_DIR) {
        printk("[ramfs] create failed: parent is not directory\n");
        return -1;
    }

    if (mini_strlen(name) == 0 || mini_strlen(name) >= RAMFS_NAME_LEN) {
        printk("[ramfs] create failed: invalid file name\n");
        return -1;
    }

    if (find_child(parent, name) != -1) {
        printk("[ramfs] file already exists: ");
        printk(path);
        printk("\n");
        return -1;
    }

    idx = find_free_node();
    if (idx == -1) {
        printk("[ramfs] create failed: node table full\n");
        return -1;
    }

    node_table[idx].used = 1;
    node_table[idx].type = RAMFS_NODE_FILE;
    node_table[idx].parent = parent;
    node_table[idx].size = 0;
    mini_strcpy(node_table[idx].name, name);
    node_table[idx].content[0] = '\0';

    printk("[ramfs] create file ok: ");
    printk(path);
    printk("\n");
    printf("[VIZ]{\"type\":\"ramfs_create\",\"file\":\"%s\"}\n", path);

    return 0;
}

int ramfs_write_file(const char *path, const char *content) {
    int idx = ramfs_resolve_path(path);
    int len;

    if (idx == -1) {
        printk("[ramfs] file not found: ");
        printk(path ? path : "(null)");
        printk("\n");
        return -1;
    }

    if (node_table[idx].type != RAMFS_NODE_FILE) {
        printk("[ramfs] write failed: not a file\n");
        return -1;
    }

    if (content == 0) content = "";

    len = mini_strlen(content);
    if (len >= RAMFS_CONTENT_SIZE) {
        len = RAMFS_CONTENT_SIZE - 1;
    }

    mini_strncpy(node_table[idx].content, content, len);
    node_table[idx].content[len] = '\0';
    node_table[idx].size = len;

    printk("[ramfs] write file ok: ");
    printk(path);
    printk("\n");
    printf("[VIZ]{\"type\":\"ramfs_write\",\"file\":\"%s\",\"size\":%d}\n", path, node_table[idx].size);

    return 0;
}

int ramfs_read_file(const char *path, char *buffer, int buffer_size) {
    int idx = ramfs_resolve_path(path);
    int len;

    if (idx == -1) {
        printk("[ramfs] file not found: ");
        printk(path ? path : "(null)");
        printk("\n");
        return -1;
    }

    if (node_table[idx].type != RAMFS_NODE_FILE) {
        printk("[ramfs] read failed: not a file\n");
        return -1;
    }

    if (buffer == 0 || buffer_size <= 0) {
        printk("[ramfs] invalid read buffer\n");
        return -1;
    }

    len = node_table[idx].size;
    if (len > buffer_size - 1) {
        len = buffer_size - 1;
    }

    mini_memcpy(buffer, node_table[idx].content, len);
    buffer[len] = '\0';

    return len;
}

int ramfs_delete_file(const char *path) {
    int idx = ramfs_resolve_path(path);

    if (idx == -1) {
        printk("[ramfs] file not found: ");
        printk(path ? path : "(null)");
        printk("\n");
        return -1;
    }

    if (idx == 0) {
        printk("[ramfs] cannot delete root directory\n");
        return -1;
    }

    if (node_table[idx].type == RAMFS_NODE_DIR && !dir_is_empty(idx)) {
        printk("[ramfs] rm failed: directory not empty\n");
        return -1;
    }

    node_table[idx].used = 0;
    node_table[idx].type = 0;
    node_table[idx].parent = 0;
    node_table[idx].size = 0;
    mini_memset(node_table[idx].name, 0, RAMFS_NAME_LEN);
    mini_memset(node_table[idx].content, 0, RAMFS_CONTENT_SIZE);

    printk("[ramfs] delete ok: ");
    printk(path);
    printk("\n");
    printf("[VIZ]{\"type\":\"ramfs_delete\",\"file\":\"%s\"}\n", path);

    return 0;
}

void ramfs_print_file(const char *path) {
    int idx = ramfs_resolve_path(path);

    if (idx == -1) {
        printk("[ramfs] file not found: ");
        printk(path ? path : "(null)");
        printk("\n");
        return;
    }

    if (node_table[idx].type != RAMFS_NODE_FILE) {
        printk("[ramfs] cat failed: not a file\n");
        return;
    }

    printk(node_table[idx].content);
    printk("\n");
}

/* ============================================================
 *  列表 / 信息
 * ============================================================ */

void ramfs_list_path(const char *path) {
    int dir;
    int count = 0;
    int total_bytes = 0;

    if (path == 0 || mini_strlen(path) == 0) {
        dir = cwd_index;
    } else {
        dir = ramfs_resolve_path(path);
    }

    if (dir == -1) {
        printk("[ramfs] ls failed: path not found\n");
        return;
    }

    if (node_table[dir].type != RAMFS_NODE_DIR) {
        printk("[ramfs] ls failed: not a directory\n");
        return;
    }

    printk("NAME                          TYPE   SIZE\n");

    for (int i = 0; i < RAMFS_MAX_NODES; i++) {
        if (node_table[i].used && node_table[i].parent == dir && i != 0) {
            int name_len = mini_strlen(node_table[i].name);

            printk(node_table[i].name);

            if (node_table[i].type == RAMFS_NODE_DIR) {
                printk("/");
                name_len++;
            }

            if (name_len < 30) {
                print_spaces(30 - name_len);
            } else {
                printk(" ");
            }

            print_node_type(node_table[i].type);
            printk(" ");

            if (node_table[i].type == RAMFS_NODE_FILE) {
                print_int(node_table[i].size);
                printk(" bytes");
                total_bytes += node_table[i].size;
            } else {
                printk("-");
            }

            printk("\n");
            count++;
        }
    }

    if (count == 0) {
        printk("[ramfs] empty directory\n");
    } else {
        printk("----\n");
        print_int(count);
        printk(" node(s), ");
        print_int(total_bytes);
        printk(" bytes total\n");
    }
}

void ramfs_list_files(void) {
    ramfs_list_path(0);
}

void ramfs_print_info(void) {
    int used = 0;
    int dirs = 0;
    int files = 0;
    int total_size = 0;
    int max_size = 0;
    const char *max_name = 0;
    int used_fd = 0;

    for (int i = 0; i < RAMFS_MAX_NODES; i++) {
        if (node_table[i].used) {
            used++;

            if (node_table[i].type == RAMFS_NODE_DIR) {
                dirs++;
            } else if (node_table[i].type == RAMFS_NODE_FILE) {
                files++;
                total_size += node_table[i].size;

                if (node_table[i].size > max_size) {
                    max_size = node_table[i].size;
                    max_name = node_table[i].name;
                }
            }
        }
    }

    for (int i = 0; i < RAMFS_MAX_FD; i++) {
        if (fd_table[i].used) used_fd++;
    }

    printk("[RAMFS v2 Filesystem Info]\n");
    print_kv_int("  total nodes  : ", RAMFS_MAX_NODES, "");
    print_kv_int("  used nodes   : ", used, "");
    print_kv_int("  free nodes   : ", RAMFS_MAX_NODES - used, "");
    print_kv_int("  directories  : ", dirs, "");
    print_kv_int("  files        : ", files, "");
    print_kv_int("  total size   : ", total_size, " bytes");

    printk("  max file     : ");
    printk(max_name ? max_name : "(none)");
    printk(" (");
    print_int(max_size);
    printk(" bytes)\n");

    print_kv_int("  max file size: ", RAMFS_CONTENT_SIZE, " bytes per file");
    print_kv_int("  fd capacity  : ", RAMFS_MAX_FD, "");
    print_kv_int("  fd used      : ", used_fd, "");
}

/* ============================================================
 *  fd 表
 * ============================================================ */

static int find_free_fd(void) {
    for (int i = 0; i < RAMFS_MAX_FD; i++) {
        if (!fd_table[i].used) return i;
    }

    return -1;
}

int ramfs_open(const char *path, int flags) {
    int idx = ramfs_resolve_path(path);
    int fd;

    if (idx == -1) {
        printk("[ramfs] open failed: file not found\n");
        return -1;
    }

    if (node_table[idx].type != RAMFS_NODE_FILE) {
        printk("[ramfs] open failed: not a file\n");
        return -1;
    }

    fd = find_free_fd();
    if (fd == -1) {
        printk("[ramfs] open failed: fd table full\n");
        return -1;
    }

    fd_table[fd].used = 1;
    fd_table[fd].node_index = idx;
    fd_table[fd].offset = 0;
    fd_table[fd].flags = flags;

    printk("[ramfs] open ok: fd=");
    print_int(fd);
    printk("\n");

    return fd;
}

int ramfs_close(int fd) {
    if (fd < 0 || fd >= RAMFS_MAX_FD || !fd_table[fd].used) {
        printk("[ramfs] close failed: invalid fd\n");
        return -1;
    }

    fd_table[fd].used = 0;
    fd_table[fd].node_index = -1;
    fd_table[fd].offset = 0;
    fd_table[fd].flags = 0;

    printk("[ramfs] close ok: fd=");
    print_int(fd);
    printk("\n");

    return 0;
}

int ramfs_read(int fd, char *buf, int len) {
    int node;
    int remain;
    int n;

    if (fd < 0 || fd >= RAMFS_MAX_FD || !fd_table[fd].used) {
        printk("[ramfs] read failed: invalid fd\n");
        return -1;
    }

    if (buf == 0 || len <= 0) {
        printk("[ramfs] read failed: invalid buffer\n");
        return -1;
    }

    node = fd_table[fd].node_index;

    if (node < 0 || node >= RAMFS_MAX_NODES || !node_table[node].used) {
        printk("[ramfs] read failed: invalid node\n");
        return -1;
    }

    remain = node_table[node].size - fd_table[fd].offset;
    if (remain < 0) remain = 0;

    n = len;
    if (n > remain) n = remain;

    mini_memcpy(buf, node_table[node].content + fd_table[fd].offset, n);
    buf[n] = '\0';
    fd_table[fd].offset += n;

    return n;
}

int ramfs_write(int fd, const char *buf, int len) {
    int node;
    int off;
    int n;

    if (fd < 0 || fd >= RAMFS_MAX_FD || !fd_table[fd].used) {
        printk("[ramfs] write failed: invalid fd\n");
        return -1;
    }

    if (buf == 0 || len <= 0) {
        printk("[ramfs] write failed: invalid buffer\n");
        return -1;
    }

    node = fd_table[fd].node_index;
    if (node < 0 || node >= RAMFS_MAX_NODES || !node_table[node].used) {
        printk("[ramfs] write failed: invalid node\n");
        return -1;
    }

    off = fd_table[fd].offset;
    if (off < 0) off = 0;
    if (off >= RAMFS_CONTENT_SIZE - 1) {
        printk("[ramfs] write failed: no space\n");
        return -1;
    }

    n = len;
    if (off + n >= RAMFS_CONTENT_SIZE) {
        n = RAMFS_CONTENT_SIZE - 1 - off;
    }

    mini_memcpy(node_table[node].content + off, buf, n);
    fd_table[fd].offset = off + n;

    if (fd_table[fd].offset > node_table[node].size) {
        node_table[node].size = fd_table[fd].offset;
    }

    node_table[node].content[node_table[node].size] = '\0';

    return n;
}

int ramfs_seek(int fd, int offset) {
    int node;

    if (fd < 0 || fd >= RAMFS_MAX_FD || !fd_table[fd].used) {
        printk("[ramfs] seek failed: invalid fd\n");
        return -1;
    }

    node = fd_table[fd].node_index;

    if (offset < 0) offset = 0;
    if (offset > node_table[node].size) offset = node_table[node].size;

    fd_table[fd].offset = offset;

    printk("[ramfs] seek ok: fd=");
    print_int(fd);
    printk(", offset=");
    print_int(offset);
    printk("\n");

    return offset;
}

void ramfs_fdtest(void) {
    char buf[128];
    int fd;
    int n;

    printk("[ramfs] fdtest start\n");

    ramfs_create_file("/fdtest.txt");
    fd = ramfs_open("/fdtest.txt", RAMFS_O_RDWR);

    if (fd < 0) {
        printk("[ramfs] fdtest failed: open\n");
        return;
    }

    n = ramfs_write(fd, "abcdef", 6);
    printk("[ramfs] write bytes: ");
    print_int(n);
    printk("\n");

    ramfs_seek(fd, 0);

    n = ramfs_read(fd, buf, 3);
    printk("[ramfs] read bytes : ");
    print_int(n);
    printk("\n");

    printk("[ramfs] read data  : ");
    printk(buf);
    printk("\n");

    ramfs_seek(fd, 3);
    ramfs_write(fd, "XYZ", 3);

    ramfs_seek(fd, 0);
    ramfs_read(fd, buf, 127);

    printk("[ramfs] final file : ");
    printk(buf);
    printk("\n");

    ramfs_close(fd);

    printk("[ramfs] fdtest done\n");
}
