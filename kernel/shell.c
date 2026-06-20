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

#define CMD_BUF_SIZE 256

static void shell_help(void) {
    printf("MiniOS command list:\n");
    printf("  help                 show command list\n");
    printf("  mem                  show memory information\n");
    printf("  ps                   show process information\n");
    printf("  run <name>           create a test process\n");
    printf("  fork                 fork current process\n");
    printf("  exec <name>          replace process image\n");
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

void shell_start(void) {
    char cmd[CMD_BUF_SIZE];

    printf("[MiniOS] shell start\n");
    printf("[MiniOS] hint: type 'help' for available commands\n");

    while (1) {
        printf("MiniOS> ");

        if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
            printf("\n");
            break;
        }

        /* 去除尾部换行符 */
        cmd[strcspn(cmd, "\n")] = '\0';

        /* ---- 命令分发 ---- */

        if (strcmp(cmd, "") == 0) {
            continue;
        }
        else if (strcmp(cmd, "help") == 0) {
            shell_help();
        }
        else if (strcmp(cmd, "mem") == 0) {
            memory_print_info();
        }
        else if (strcmp(cmd, "ps") == 0) {
            process_print_list();
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
            char *name = cmd + 5;
            while (*name == ' ') name++;
            printf("[shell] exec '%s'...\n", name);
            process_exec(name);
            printf("[shell] exec done, current=%s (pid=%d)\n",
                   process_get_current()->name,
                   process_get_current_pid());
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
            break;
        }
        else {
            printf("unknown command: %s\n", cmd);
        }
    }
}
