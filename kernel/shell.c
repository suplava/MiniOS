#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "shell.h"
#include "memory.h"
#include "process.h"


#define CMD_BUF_SIZE 128

static void shell_help(void) {
    printf("MiniOS command list:\n");
    printf("help        show command list\n");
    printf("mem         show memory information\n");
    printf("ps          show process information\n");
    printf("run <name>  create a test process\n");
    printf("kill <pid>  kill a process\n");
    printf("clear       clear screen\n");
    printf("exit        exit MiniOS\n");
}

void shell_start(void) {
    char cmd[CMD_BUF_SIZE];

    printf("[MiniOS] shell start\n");

    while (1) {
        printf("MiniOS> ");

        if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
            printf("\n");
            break;
        }

        cmd[strcspn(cmd, "\n")] = '\0';

        if (strcmp(cmd, "") == 0) {
            continue;
        } else if (strcmp(cmd, "help") == 0) {
            shell_help();
        } else if (strcmp(cmd, "mem") == 0) {
            memory_print_info();
        } else if (strcmp(cmd, "ps") == 0) {
            process_print_list();
        } else if (strncmp(cmd, "run ", 4) == 0) {
            char *name = cmd + 4;
            process_create(name);
        } else if (strncmp(cmd, "kill ", 5) == 0) {
            int pid = atoi(cmd + 5);
            process_kill(pid);
        } else if (strcmp(cmd, "clear") == 0) {
            for (int i = 0; i < 30; i++) {
                printf("\n");
            }
        } else if (strcmp(cmd, "exit") == 0) {
            printf("[MiniOS] exit shell\n");
            break;
        } else {
            printf("unknown command: %s\n", cmd);
        }
    }
}
