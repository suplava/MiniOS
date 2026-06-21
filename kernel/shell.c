/**
 * ================================================================
 *  MiniOS 命令行 Shell
 *
 *  改进点：
 *   - 新增 sched / yield / tick / block / wakeup 命令
 *   - 在命令循环中模拟时钟滴答（每次命令输入触发一次 tick）
 *   - 集成调度器，让用户直观感受多任务调度
 * ================================================================ */

#include "hal.h"
#include "shell.h"
#include "memory.h"
#include "process.h"
#include "sched.h"
#include "timer.h"
#include "ramfs.h"
#include "syscall.h"
#include "test.h"
#include "trap.h"
#include "userprog.h"

#define CMD_BUF_SIZE 256

static void shell_help(void) {
    printf("MiniOS command list:\n");
    printf("  help                 show command list\n");
    printf("  mem                  show memory information\n");
    printf("  ps                   show process information\n");
    printf("  pstree               show process tree\n");
    printf("  run <name>           create a worker process\n");
    printf("  fork                 fork current process\n");
    printf("  exec <app> [args]    load and run MiniExec user program\n");
    printf("  pexec <name>         simulate process_exec replacement\n");
    printf("  apps                 list MiniExec user programs\n");
    printf("  apptest              run MiniExec user program tests\n");
    printf("  wait                 wait for child process\n");
    printf("  kill <pid>           kill a process\n");
    printf("  block <pid>          block a process (simulate I/O wait)\n");
    printf("  wakeup <pid>         wake up a blocked process\n");
    printf("  sched                show scheduler info + timing\n");
    printf("  fcfs                 switch to FCFS (non-preemptive)\n");
    printf("  rr                   switch to Round-Robin (preemptive)\n");
    printf("  timeslice <N>        set time slice to N ticks\n");
    printf("  yield                voluntarily yield CPU\n");
    printf("  tick [N]             simulate N timer ticks\n");
    printf("  ls                   list files in RAMFS\n");
    printf("  cat <file>           show file content\n");
    printf("  touch <file>         create a new file\n");
    printf("  write <file> <text>  write text to file\n");
    printf("  rm <file>            delete a file\n");
    printf("  fsinfo               show RAMFS filesystem statistics\n");
    printf("  mkdir <dir>          create directory\n");
    printf("  cd <dir>             change current directory\n");
    printf("  pwd                  show current directory\n");
    printf("  mkfs                 format RAMFS\n");
    printf("  open <file>          open file and return fd\n");
    printf("  close <fd>           close fd\n");
    printf("  readfd <fd> <len>    read from fd\n");
    printf("  writefd <fd> <text>  write text to fd\n");
    printf("  seek <fd> <offset>   change fd offset\n");
    printf("  fdtest               run fd/seek demo\n");
    printf("  echo <text>          print text\n");
    printf("  clear                clear screen\n");
    printf("  test                 run all MiniOS test cases\n");
    printf("  fault                simulate trap exception\n");
    printf("  illegal              trigger illegal instruction trap\n");
    printf("  nullptr              trigger null pointer access trap\n");
    printf("  divzero              trigger division by zero trap\n");
    printf("  overflow             trigger integer overflow trap\n");
    printf("  breakpoint           trigger breakpoint trap\n");
    printf("  singlestep           trigger single step trap\n");
    printf("  align                trigger misaligned access trap\n");
    printf("  stack                trigger stack error trap\n");
    printf("  protection           trigger general protection trap\n");
    printf("  fpu                  trigger floating point exception\n");
    printf("  pfread               trigger page fault (read)\n");
    printf("  pfwrite              trigger page fault (write)\n");
    printf("  proc                 start multi-process round-robin demo\n");
    printf("  syscall              simulate syscall\n");
    printf("  exit                 exit MiniOS\n");
}

static void handle_write_command(char *cmd) {
    char *filename = cmd + 6;

    while (*filename == ' ') {
        filename++;
    }

    char *content = strchr(filename, ' ');

    if (content == NULL) {
        printf("usage: write <file> <text>\n");
        return;
    }

    *content = '\0';
    content++;

    while (*content == ' ') {
        content++;
    }

    if (strlen(filename) == 0 || strlen(content) == 0) {
        printf("usage: write <file> <text>\n");
        return;
    }

    ramfs_write_file(filename, content);
}
static void handle_writefd_command(char *cmd) {
    char *p = cmd + 8;

    while (*p == ' ') p++;

    int fd = atoi(p);

    while (*p != '\0' && *p != ' ') p++;
    while (*p == ' ') p++;

    if (*p == '\0') {
        printf("usage: writefd <fd> <text>\n");
        return;
    }

    int len = strlen(p);
    int n = ramfs_write(fd, p, len);

    printf("[shell] writefd result: ");
    printf("%d\n", n);
}

static void handle_readfd_command(char *cmd) {
    char *p = cmd + 7;
    char buf[256];

    while (*p == ' ') p++;

    int fd = atoi(p);

    while (*p != '\0' && *p != ' ') p++;
    while (*p == ' ') p++;

    int len = atoi(p);

    if (len <= 0) {
        printf("usage: readfd <fd> <len>\n");
        return;
    }

    if (len > 255) {
        len = 255;
    }

    int n = ramfs_read(fd, buf, len);

    if (n >= 0) {
        printf("[shell] readfd bytes: %d\n", n);
        printf("[shell] readfd data : %s\n", buf);
    }
}

/* ================================================================ */
/*  Shell redirect & pipe helpers                                   */
/* ================================================================ */

static int shell_run_cmd(char *cmd);

static void shell_redirect_out(char *cmd, int append) {
    char *op = append ? strstr(cmd, ">>") : strchr(cmd, '>');
    char cap_buf[4096];
    char *file, *end;
    if (!op) return;
    *op = 0;
    file = op + (append ? 2 : 1);
    while (*file == ' ') file++;
    end = cmd + strlen(cmd) - 1;
    while (end >= cmd && *end == ' ') { *end = 0; end--; }
    if (strlen(cmd) == 0 || strlen(file) == 0) {
        printf("usage: command > file\n");
        return;
    }
    printf("[shell] redirect '%s' > '%s'%s\n", cmd, file,
           append ? " (append)" : "");
    hal_capture_start(cap_buf, sizeof(cap_buf));
    shell_run_cmd(cmd);
    hal_capture_stop();
    if (append) {
        char old[65536];
        int old_len = ramfs_read_file(file, old, 65534);
        int cap_len;
        if (old_len < 0) {
            /* file doesn't exist: create new */
            old_len = 0;
            ramfs_create_file(file);
        }
        old[old_len] = 0;
        cap_len = strlen(cap_buf);
        if (old_len + cap_len >= 65535) cap_len = 65535 - old_len;
        if (cap_len > 0) { memcpy(old + old_len, cap_buf, cap_len); old[old_len + cap_len] = 0; }
        ramfs_write_file(file, old);
    } else {
        ramfs_create_file(file);  /* auto-create, ignore "exists" error */
        ramfs_write_file(file, cap_buf);
    }
}

static void shell_redirect_in(char *cmd) {
    char *op = strchr(cmd, '<');
    char *file;
    char buf[4096];
    int len;
    if (!op) return;
    *op = 0;
    file = op + 1;
    while (*file == ' ') file++;
    len = ramfs_read_file(file, buf, sizeof(buf) - 1);
    if (len < 0) {
        printf("[shell] cannot read file: %s\n", file);
        return;
    }
    buf[len] = 0;
    printf("[shell] redirect '%s' < '%s' (%d bytes)\n", cmd, file, len);
    if (strcmp(cmd, "cat") == 0 || strcmp(cmd, "") == 0)
        printf("%s\n", buf);
    else
        printf("[shell] input %d bytes: %s\n", len, buf);
}

static void shell_handle_pipe(char *cmd) {
    char *op = strchr(cmd, '|');
    char cap_buf[4096];
    char *right;
    if (!op) return;
    *op = 0;
    right = op + 1;
    while (*right == ' ') right++;
    /* trim trailing spaces from left side */
    { char *e = cmd + strlen(cmd) - 1; while (e >= cmd && *e == ' ') *e-- = 0; }
    printf("[shell] pipe '%s' | '%s'\n", cmd, right);
    hal_capture_start(cap_buf, sizeof(cap_buf));
    shell_run_cmd(cmd);
    hal_capture_stop();
    printf("[shell] pipe result (%d bytes): %s\n", (int)strlen(cap_buf), cap_buf);
}

void shell_start(void) {
    char cmd[CMD_BUF_SIZE];

    printf("[MiniOS] shell start\n");
    printf("[MiniOS] hint: type 'help' for available commands\n");

    userprog_init();

    while (1) {
        printf("MiniOS> ");

        if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
            printf("\n");
            break;
        }

        /* 去除尾部换行符 */
        cmd[strcspn(cmd, "\n")] = '\0';

        /* ---- 命令分发 ---- */

        /* ---- redirect / pipe pre-check ---- */
        if (strstr(cmd, ">>")) { shell_redirect_out(cmd, 1); continue; }
        if (strchr(cmd, '>'))  { shell_redirect_out(cmd, 0); continue; }
        if (strchr(cmd, '<'))  { shell_redirect_in(cmd);      continue; }
        if (strchr(cmd, '|'))  { shell_handle_pipe(cmd);       continue; }

        if (shell_run_cmd(cmd)) break;
    }
}
/* ---- command dispatch function ---- */
static int shell_run_cmd(char *cmd) {
        if (strcmp(cmd, "") == 0) {
            return 0;
        }
        else if (strcmp(cmd, "help") == 0) {
            shell_help();
        }
        else if (strcmp(cmd, "mem") == 0) {
            memory_print_info();
        }
        else if (strcmp(cmd, "ps") == 0) {
            process_print_list();
        } else if (strcmp(cmd, "pstree") == 0) {
            process_print_tree();
        }
        /* ---- 调度器相关命令 (NEW) ---- */
        else if (strcmp(cmd, "sched") == 0) {
            sched_print_info();
        }
        else if (strcmp(cmd, "fcfs") == 0) {
            sched_set_algo(SCHED_FCFS);
            printf("[shell] switched to FCFS (non-preemptive)\n");
        }
        else if (strcmp(cmd, "rr") == 0) {
            sched_set_algo(SCHED_RR);
            printf("[shell] switched to Round-Robin (preemptive)\n");
        }
        else if (strncmp(cmd, "timeslice ", 10) == 0) {
            int ts = atoi(cmd + 10);
            if (ts > 0) sched_set_time_slice(ts);
            else printf("[shell] usage: timeslice <1-100>\n");
        }
        else if (strcmp(cmd, "yield") == 0) {
            printf("[shell] calling sched_yield()...\n");
            sched_yield();
            printf("[shell] returned from yield (current: pid=%d)\n",
                   process_get_current_pid());
        }
        else if (strncmp(cmd, "tick", 4) == 0) {
            int n = 1;   /* 默认 1 个滴答 */
            if (strlen(cmd) > 5) {
                n = atoi(cmd + 5);
                if (n <= 0) n = 1;
                if (n > 100) {
                    printf("[shell] too many ticks, capped at 100\n");
                    n = 100;
                }
            }
            printf("[shell] simulating %d timer tick(s)...\n", n);
            for (int i = 0; i < n; i++) {
                timer_tick();
            }
            printf("[shell] done. current process: pid=%d\n",
                   process_get_current_pid());
        }
        else if (strncmp(cmd, "block ", 6) == 0) {
            int pid = atoi(cmd + 6);
            if (pid <= 0) {
                printf("[shell] cannot block pid %d\n", pid);
            } else {
                process_t *proc = process_get_by_pid(pid);
                if (proc && proc->state == PROC_RUNNING) {
                    sched_block();
                } else if (proc && proc->state == PROC_READY) {
                    /* 从就绪队列移除并阻塞 */
                    process_block(pid);
                    printf("[shell] blocked pid %d (was READY)\n", pid);
                } else {
                    printf("[shell] cannot block pid %d (state issue)\n", pid);
                }
            }
        }
        else if (strncmp(cmd, "wakeup ", 7) == 0) {
            int pid = atoi(cmd + 7);
            if (pid <= 0) {
                printf("[shell] cannot wakeup pid %d\n", pid);
            } else {
                sched_wakeup(pid);
            }
        }
        /* ---- 进程管理 ---- */
        else if (strncmp(cmd, "run ", 4) == 0) {
            char *name = cmd + 4;
            while (*name == ' ') name++;
            int pid = process_create(name);
            if (pid > 0) {
                process_t *p = process_get_by_pid(pid);
                if (p) {
                    p->context.eip = (uint32_t)(uintptr_t)worker_main;
                    printf("[shell] worker %s (pid=%d) created\n", name, pid);
                }
            }
        }
        else if (strcmp(cmd, "fork") == 0) {
            printf("[shell] forking current process...\n");
            int child = process_fork();
            if (child > 0)
                printf("[shell] fork success: child pid=%d\n", child);
            else
                printf("[shell] fork failed\n");
        }
        else if (strncmp(cmd, "exec ", 5) == 0) {
    char *line = cmd + 5;

    while (*line == ' ') {
        line++;
    }

    if (*line == '\0') {
        printf("usage: exec <app> [args]\n");
    } else {
        userprog_exec_cmd(line);
    }
}
    else if (strncmp(cmd, "pexec ", 6) == 0) {
        char *name = cmd + 6;

        while (*name == ' ') {
         name++;
        }

        printf("[shell] process_exec '%s'...\n", name);
        process_exec(name);
        printf("[shell] process_exec done, current=%s (pid=%d)\n",
           process_get_current()->name,
           process_get_current_pid());
        }   
        else if (strcmp(cmd, "apps") == 0) {
            userprog_list();
        }
        else if (strcmp(cmd, "apptest") == 0) {
            userprog_apptest();
        }

        else if (strcmp(cmd, "wait") == 0) {
            printf("[shell] waiting for child...\n");
            int child = process_wait(NULL);
            if (child > 0)
                printf("[shell] child pid=%d exited\n", child);
            else
                printf("[shell] no children to wait for\n");
        }
        else if (strncmp(cmd, "kill ", 5) == 0) {
            int pid = atoi(cmd + 5);
            process_kill(pid);
        }
        else if (strcmp(cmd, "test") == 0) {
            printf("[shell] running all system test cases\n");
            kernel_test();
            printf("[shell] all test finished\n");
        }
        else if (strcmp(cmd, "fault") == 0) {
            printf("[shell] trigger simulated page fault trap\n");
            trap_test_fault();
        }
        else if (strcmp(cmd, "illegal") == 0) {
            printf("[shell] trigger illegal instruction trap\n");
            trap_test_illegal();
        }
        else if (strcmp(cmd, "nullptr") == 0) {
            printf("[shell] trigger null pointer write trap\n");
            trap_test_nullptr();
        }
        else if (strcmp(cmd, "divzero") == 0) {   // 新增分支
            printf("[shell] trigger division by zero trap\n");
            trap_test_divzero();
        }
        else if (strcmp(cmd, "overflow") == 0) {
            printf("[shell] trigger integer overflow trap\n");
            trap_test_overflow();
        }
        else if (strcmp(cmd, "breakpoint") == 0) {
            printf("[shell] trigger breakpoint trap\n");
            trap_test_breakpoint();
        }
        else if (strcmp(cmd, "singlestep") == 0) {
            printf("[shell] trigger single step trap\n");
            trap_test_singlestep();
        }
        else if (strcmp(cmd, "align") == 0) {
            printf("[shell] trigger misaligned access trap\n");
            trap_test_alignment();
        }
        else if (strcmp(cmd, "stack") == 0) {
            printf("[shell] trigger stack error trap\n");
            trap_test_stack();
        }
        else if (strcmp(cmd, "protection") == 0) {
            printf("[shell] trigger general protection trap\n");
            trap_test_protection();
        }
        else if (strcmp(cmd, "fpu") == 0) {
            printf("[shell] trigger floating point exception\n");
            trap_test_fpu();
        }
        else if (strcmp(cmd, "pfread") == 0) {
            printf("[shell] trigger page fault (read access violation) trap\n"); 
            trap_test_pf_read(); 
        }
        else if (strcmp(cmd, "pfwrite") == 0) { 
            printf("[shell] trigger page fault (write access violation) trap\n");
            trap_test_pf_write();
         }
        else if (strcmp(cmd, "proc") == 0) {
            printf("[shell] start multi-process round-robin demo\n");
            process_create("proc_1");
            process_create("proc_2");
            process_create("proc_3");
        }
        else if (strcmp(cmd, "syscall") == 0) {
            printf("[shell] simulate standard syscall sequence\n");
            syscall_handler(SYS_WRITE, "test write", 10, 0);
            syscall_handler(SYS_GETPID, "", 0, 0);
            syscall_handler(SYS_YIELD, "", 0, 0);
            syscall_handler(SYS_EXIT, "", 0, 0);
        }
        /* ---- 文件系统 ---- */
        else if (strcmp(cmd, "ls") == 0) {
            ramfs_list_files();
        }
        else if (strncmp(cmd, "ls ", 3) == 0) {
            char *path = cmd + 3;
            while (*path == ' ') path++;
            ramfs_list_path(path);
        }
        else if (strncmp(cmd, "cat ", 4) == 0) {
            char *filename = cmd + 4;
            while (*filename == ' ') filename++;
            ramfs_print_file(filename);
        }
        else if (strncmp(cmd, "touch ", 6) == 0) {
            char *filename = cmd + 6;
            while (*filename == ' ') filename++;
            ramfs_create_file(filename);
        }
        else if (strncmp(cmd, "write ", 6) == 0) {
            handle_write_command(cmd);
        }
        else if (strncmp(cmd, "rm ", 3) == 0) {
            char *filename = cmd + 3;
            while (*filename == ' ') filename++;
            ramfs_delete_file(filename);
        }
        else if (strncmp(cmd, "mkdir ", 6) == 0) {
            char *dirname = cmd + 6;
            while (*dirname == ' ') dirname++;
            ramfs_mkdir(dirname);
        }
        else if (strncmp(cmd, "cd ", 3) == 0) {
            char *dirname = cmd + 3;
            while (*dirname == ' ') dirname++;
            ramfs_chdir(dirname);
        }
        else if (strcmp(cmd, "pwd") == 0) {
            ramfs_pwd();
        }
        else if (strcmp(cmd, "mkfs") == 0) {
            ramfs_format();
            userprog_init();
            printf("[shell] RAMFS formatted and MiniExec apps reinstalled\n");
        }

        else if (strcmp(cmd, "fsinfo") == 0) {
            ramfs_print_info();
        }
        else if (strncmp(cmd, "open ", 5) == 0) {
            char *filename = cmd + 5;
            while (*filename == ' ') filename++;
            int fd = ramfs_open(filename, RAMFS_O_RDWR);
            printf("[shell] fd = %d\n", fd);
        }
        else if (strncmp(cmd, "close ", 6) == 0) {
            int fd = atoi(cmd + 6);
            ramfs_close(fd);
        }
        else if (strncmp(cmd, "seek ", 5) == 0) {
            char *p = cmd + 5;
            while (*p == ' ') p++;

            int fd = atoi(p);

            while (*p != '\0' && *p != ' ') p++;
            while (*p == ' ') p++;

            int offset = atoi(p);
            ramfs_seek(fd, offset);
        }
        else if (strncmp(cmd, "readfd ", 7) == 0) {
            handle_readfd_command(cmd);
        }
        else if (strncmp(cmd, "writefd ", 8) == 0) {
            handle_writefd_command(cmd);
        }
        else if (strcmp(cmd, "fdtest") == 0) {
            ramfs_fdtest();
        }

        else if (strcmp(cmd, "fsinfo") == 0) {
            ramfs_print_info();
        }
        /* ---- 其他 ---- */
        else if (strncmp(cmd, "echo ", 5) == 0) {
            printf("%s\n", cmd + 5);
        }
        else if (strcmp(cmd, "clear") == 0) {
            for (int i = 0; i < 30; i++) {
                printf("\n");
            }
        }
        else if (strcmp(cmd, "exit") == 0) {
            printf("[MiniOS] exit shell\n");
            return 1;
        }
        else {
            printf("unknown command: %s\n", cmd);
        }
    return 0;
}

