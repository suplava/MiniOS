/**
 * ================================================================
 *  MiniOS MiniExec 用户程序加载器
 *
 *  功能：
 *   - 在 RAMFS 的 /bin 目录中安装简化用户程序描述文件
 *   - 支持 exec /bin/hello、exec echo hello 等命令
 *   - 支持 argc / argv 参数传递
 *   - 支持 5 个以上简化用户程序
 *   - 提供 uprint / ugetpid 等简化系统调用封装
 *
 *  说明：
 *   - 这是教学型 MiniExec 加载器，不是真正 ELF 加载器。
 *   - 程序文件内容采用 APP:<name> 格式，例如 APP:hello。
 * ================================================================
 */

#include "hal.h"
#include "userprog.h"
#include "ramfs.h"
#include "syscall.h"
#include "process.h"

/* ============================================================
 *  基础工具函数
 * ============================================================ */

static int up_strlen(const char *s) {
    int n = 0;
    if (s == 0) return 0;
    while (s[n] != '\0') n++;
    return n;
}

static int up_strcmp(const char *a, const char *b) {
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

static int up_starts_with(const char *s, const char *prefix) {
    int i = 0;

    if (s == 0 || prefix == 0) return 0;

    while (prefix[i] != '\0') {
        if (s[i] != prefix[i]) return 0;
        i++;
    }

    return 1;
}

static void up_strcpy_limit(char *dst, const char *src, int max) {
    int i = 0;

    if (dst == 0 || max <= 0) return;

    if (src == 0) {
        dst[0] = '\0';
        return;
    }

    while (src[i] != '\0' && i < max - 1) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static void up_append_limit(char *dst, const char *src, int max) {
    int d = up_strlen(dst);
    int s = 0;

    if (dst == 0 || src == 0 || max <= 0) return;

    while (src[s] != '\0' && d < max - 1) {
        dst[d++] = src[s++];
    }

    dst[d] = '\0';
}

static int up_atoi(const char *s) {
    int value = 0;
    int i = 0;

    if (s == 0) return 0;

    while (s[i] == ' ') i++;

    while (s[i] >= '0' && s[i] <= '9') {
        value = value * 10 + (s[i] - '0');
        i++;
    }

    return value;
}

static void make_exec_path(const char *input, char *out) {
    if (input == 0 || out == 0) return;

    /*
     * 支持三种写法：
     *   exec /bin/hello
     *   exec bin/hello
     *   exec hello
     */
    if (input[0] == '/') {
        up_strcpy_limit(out, input, USERPROG_PATH_LEN);
    } else if (up_starts_with(input, "bin/")) {
        up_strcpy_limit(out, "/", USERPROG_PATH_LEN);
        up_append_limit(out, input, USERPROG_PATH_LEN);
    } else {
        up_strcpy_limit(out, "/bin/", USERPROG_PATH_LEN);
        up_append_limit(out, input, USERPROG_PATH_LEN);
    }
}

static int parse_app_name(const char *desc, char *app_name) {
    int i;
    int j = 0;

    if (desc == 0 || app_name == 0) return -1;

    if (!up_starts_with(desc, "APP:")) {
        return -1;
    }

    i = 4;

    while (desc[i] != '\0' &&
           desc[i] != '\n' &&
           desc[i] != ' ' &&
           j < USERPROG_NAME_LEN - 1) {
        app_name[j++] = desc[i++];
    }

    app_name[j] = '\0';

    if (j == 0) return -1;
    return 0;
}

/* ============================================================
 *  简化用户态 libc / syscall wrapper
 * ============================================================ */

static void uprint(const char *s) {
    syscall_handler(SYS_WRITE, s, 0, 0);
}

static int ugetpid(void) {
    return syscall_handler(SYS_GETPID, "", 0, 0);
}

/*
 * 注意：
 * 这里不调用 SYS_EXIT，因为当前 MiniExec 程序是由 Shell 同步调用的。
 * 如果直接 SYS_EXIT，可能会把 Shell 当前进程杀掉。
 */
static int uexit(int code) {
    return code;
}

/* ============================================================
 *  用户程序入口
 * ============================================================ */

static int app_hello_main(int argc, char **argv) {
    (void)argv;

    uprint("[user] hello from MiniOS MiniExec program\n");
    printf("[user] argc=%d, current pid=%d\n", argc, ugetpid());

    return uexit(0);
}

static int app_echo_main(int argc, char **argv) {
    printf("[user] echo:");

    if (argc <= 1) {
        printf(" <empty>");
    } else {
        for (int i = 1; i < argc; i++) {
            printf(" %s", argv[i]);
        }
    }

    printf("\n");
    return uexit(0);
}

static int app_count_main(int argc, char **argv) {
    int n = 5;

    if (argc >= 2) {
        n = up_atoi(argv[1]);
    }

    if (n <= 0) n = 5;
    if (n > 20) n = 20;

    for (int i = 1; i <= n; i++) {
        printf("[user] count %d/%d\n", i, n);
    }

    return uexit(0);
}

static int app_fib_main(int argc, char **argv) {
    int n = 8;
    int a = 0;
    int b = 1;
    int c = 0;

    if (argc >= 2) {
        n = up_atoi(argv[1]);
    }

    if (n < 0) n = 0;
    if (n > 20) n = 20;

    if (n == 0) {
        c = 0;
    } else if (n == 1) {
        c = 1;
    } else {
        for (int i = 2; i <= n; i++) {
            c = a + b;
            a = b;
            b = c;
        }
    }

    printf("[user] fib(%d) = %d\n", n, c);
    return uexit(0);
}

static int app_fsdemo_main(int argc, char **argv) {
    char buf[128];

    (void)argc;
    (void)argv;

    printf("[user] fsdemo start\n");

    ramfs_mkdir("/tmp");
    ramfs_create_file("/tmp/user.txt");
    ramfs_write_file("/tmp/user.txt", "hello_from_user_program");

    if (ramfs_read_file("/tmp/user.txt", buf, sizeof(buf)) >= 0) {
        printf("[user] read /tmp/user.txt: %s\n", buf);
    } else {
        printf("[user] read /tmp/user.txt failed\n");
        return uexit(-1);
    }

    printf("[user] fsdemo done\n");
    return uexit(0);
}

static int app_crash_main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("[user] crash: simulated user program error\n");
    printf("[user] crash handled: return error code, shell keeps running\n");

    return uexit(-1);
}

/* ============================================================
 *  用户程序表
 * ============================================================ */

typedef int (*user_entry_t)(int argc, char **argv);

typedef struct user_app {
    const char *name;
    const char *path;
    const char *desc_file;
    const char *description;
    user_entry_t entry;
} user_app_t;

static user_app_t app_table[] = {
    { "hello",  "/bin/hello",  "APP:hello",  "print hello message",      app_hello_main  },
    { "echo",   "/bin/echo",   "APP:echo",   "print command arguments",  app_echo_main   },
    { "count",  "/bin/count",  "APP:count",  "count numbers",            app_count_main  },
    { "fib",    "/bin/fib",    "APP:fib",    "calculate fibonacci",      app_fib_main    },
    { "fsdemo", "/bin/fsdemo", "APP:fsdemo", "demo RAMFS operation",     app_fsdemo_main },
    { "crash",  "/bin/crash",  "APP:crash",  "simulate user error",      app_crash_main  }
};

static int app_count(void) {
    return sizeof(app_table) / sizeof(app_table[0]);
}

static user_app_t *find_app(const char *name) {
    for (int i = 0; i < app_count(); i++) {
        if (up_strcmp(app_table[i].name, name) == 0) {
            return &app_table[i];
        }
    }

    return 0;
}

/* ============================================================
 *  对外接口
 * ============================================================ */

void userprog_init(void) {
    ramfs_mkdir("/bin");

    for (int i = 0; i < app_count(); i++) {
        ramfs_create_file(app_table[i].path);
        ramfs_write_file(app_table[i].path, app_table[i].desc_file);
    }

    printf("[userprog] MiniExec init ok: %d app file(s) installed under /bin\n",
           app_count());
}

void userprog_list(void) {
    printf("[userprog] available MiniExec programs:\n");

    for (int i = 0; i < app_count(); i++) {
        printf("  %s  -  %s\n", app_table[i].path, app_table[i].description);
    }

    printf("[userprog] backing files in /bin:\n");
    ramfs_list_path("/bin");
}

int userprog_exec(const char *path, int argc, char **argv) {
    char exec_path[USERPROG_PATH_LEN];
    char desc[USERPROG_DESC_LEN];
    char app_name[USERPROG_NAME_LEN];
    user_app_t *app;
    int ret;
    int app_pid = -1;
    int old_pid;
    process_t *old_proc;
    process_state_t old_state = PROC_RUNNING;

    if (path == 0 || up_strlen(path) == 0) {
        printf("[loader] usage: exec <app> [args]\n");
        return -1;
    }

    make_exec_path(path, exec_path);

    if (ramfs_read_file(exec_path, desc, sizeof(desc)) < 0) {
        printf("[loader] executable not found: %s\n", exec_path);
        return -1;
    }

    if (parse_app_name(desc, app_name) < 0) {
        printf("[loader] invalid MiniExec format: %s\n", exec_path);
        return -1;
    }

    app = find_app(app_name);
    if (app == 0) {
        printf("[loader] unknown app: %s\n", app_name);
        return -1;
    }

    printf("[loader] load %s -> app '%s'\n", exec_path, app_name);

    /*
     * 创建一个 PCB，用于模拟“用户程序作为独立进程被加载”。
     * 当前实现不进行真实 Ring3 切换，只在内核态同步执行入口函数。
     */
    old_pid = process_get_current_pid();
    old_proc = process_get_current();
    if (old_proc != 0) {
        old_state = old_proc->state;
    }

    app_pid = process_create(app_name);

    if (app_pid > 0) {
        if (old_pid > 0) {
            process_set_state(old_pid, PROC_READY);
        }

        process_set_current(app_pid);
        process_set_state(app_pid, PROC_RUNNING);

        printf("[loader] process created for app: pid=%d\n", app_pid);
    } else {
        printf("[loader] warning: process table full, run app in shell context\n");
    }

    ret = app->entry(argc, argv);

    if (app_pid > 0) {
       process_set_current(old_pid);

    if (old_pid >= 0) {
        process_t *restore_proc = process_get_by_pid(old_pid);

    if (restore_proc != 0 && restore_proc->state != old_state) {
        process_set_state(old_pid, old_state);
    }
}

process_kill(app_pid);

    }

    printf("[loader] app '%s' exited with code %d\n", app_name, ret);

    return ret;
}

int userprog_exec_cmd(char *cmdline) {
    char *argv[USERPROG_MAX_ARGC];
    int argc = 0;
    char *p = cmdline;

    if (cmdline == 0) {
        return -1;
    }

    while (*p != '\0' && argc < USERPROG_MAX_ARGC) {
        while (*p == ' ') p++;

        if (*p == '\0') break;

        argv[argc++] = p;

        while (*p != '\0' && *p != ' ') p++;

        if (*p == ' ') {
            *p = '\0';
            p++;
        }
    }

    if (argc == 0) {
        printf("[loader] usage: exec <app> [args]\n");
        return -1;
    }

    return userprog_exec(argv[0], argc, argv);
}

void userprog_apptest(void) {
    int ok = 0;

    char *argv_hello[]  = { "/bin/hello" };
    char *argv_echo[]   = { "/bin/echo", "hello", "world" };
    char *argv_count[]  = { "/bin/count", "3" };
    char *argv_fib[]    = { "/bin/fib", "8" };
    char *argv_fsdemo[] = { "/bin/fsdemo" };

    printf("[userprog] apptest start\n");

    if (userprog_exec("/bin/hello", 1, argv_hello) == 0) ok++;
    if (userprog_exec("/bin/echo", 3, argv_echo) == 0) ok++;
    if (userprog_exec("/bin/count", 2, argv_count) == 0) ok++;
    if (userprog_exec("/bin/fib", 2, argv_fib) == 0) ok++;
    if (userprog_exec("/bin/fsdemo", 1, argv_fsdemo) == 0) ok++;

    printf("[userprog] apptest result: %d/5 program(s) passed\n", ok);

    if (ok == 5) {
        printf("[userprog] 5 user programs loaded and executed\n");
    } else {
        printf("[userprog] some user programs failed\n");
    }
}
