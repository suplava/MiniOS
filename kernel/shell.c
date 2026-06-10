#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "shell.h"
#include "memory.h"
#include "process.h"
#include "ramfs.h"

#define CMD_BUF_SIZE 256

static void shell_help(void) {
    printf("MiniOS command list:\n");
    printf("help                 show command list\n");
    printf("mem                  show memory information\n");
    printf("ps                   show process information\n");
    printf("run <name>           create a test process\n");
    printf("kill <pid>           kill a process\n");
    printf("ls                   list files in RAMFS\n");
    printf("cat <file>           show file content\n");
    printf("touch <file>         create a new file\n");
    printf("write <file> <text>  write text to file\n");
    printf("rm <file>            delete a file\n");
    printf("echo <text>          print text\n");
    printf("clear                clear screen\n");
    printf("exit                 exit MiniOS\n");
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
        } else if (strcmp(cmd, "ls") == 0) {
            ramfs_list_files();
        } else if (strncmp(cmd, "cat ", 4) == 0) {
            char *filename = cmd + 4;
            ramfs_print_file(filename);
        } else if (strncmp(cmd, "touch ", 6) == 0) {
            char *filename = cmd + 6;
            ramfs_create_file(filename);
        } else if (strncmp(cmd, "write ", 6) == 0) {
            handle_write_command(cmd);
        } else if (strncmp(cmd, "rm ", 3) == 0) {
            char *filename = cmd + 3;
            ramfs_delete_file(filename);
        } else if (strncmp(cmd, "echo ", 5) == 0) {
            printf("%s\n", cmd + 5);
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
