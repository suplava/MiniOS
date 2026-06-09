#include <stdio.h>
#include <string.h>
#include "process.h"
#include "memory.h"

static process_t process_table[MAX_PROCESSES];
static int next_pid = 0;

static const char *state_to_string(process_state_t state) {
    switch (state) {
        case PROC_UNUSED:
            return "UNUSED";
        case PROC_READY:
            return "READY";
        case PROC_RUNNING:
            return "RUNNING";
        case PROC_BLOCKED:
            return "BLOCKED";
        case PROC_ZOMBIE:
            return "ZOMBIE";
        default:
            return "UNKNOWN";
    }
}

void process_init(void) {
    memset(process_table, 0, sizeof(process_table));
    next_pid = 0;

    process_table[0].pid = next_pid++;
    strcpy(process_table[0].name, "idle");
    process_table[0].state = PROC_RUNNING;
    process_table[0].time_slice = 10;
    process_table[0].kernel_stack = alloc_page();

    process_table[1].pid = next_pid++;
    strcpy(process_table[1].name, "shell");
    process_table[1].state = PROC_READY;
    process_table[1].time_slice = 10;
    process_table[1].kernel_stack = alloc_page();

    printf("[MiniOS] process init ok\n");
}

int process_create(const char *name) {
    if (name == NULL || strlen(name) == 0) {
        printf("[process] invalid process name\n");
        return -1;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_UNUSED) {
            process_table[i].pid = next_pid++;

            strncpy(process_table[i].name, name, PROCESS_NAME_LEN - 1);
            process_table[i].name[PROCESS_NAME_LEN - 1] = '\0';

            process_table[i].state = PROC_READY;
            process_table[i].time_slice = 10;
            process_table[i].kernel_stack = alloc_page();

            if (process_table[i].kernel_stack == NULL) {
                printf("[process] create failed: no memory\n");
                process_table[i].state = PROC_UNUSED;
                return -1;
            }

            printf("[process] create process ok\n");
            printf("[process] pid  = %d\n", process_table[i].pid);
            printf("[process] name = %s\n", process_table[i].name);
            printf("[sched] process added to ready queue\n");

            return process_table[i].pid;
        }
    }

    printf("[process] create failed: process table full\n");
    return -1;
}

int process_kill(int pid) {
    if (pid == 0) {
        printf("[process] cannot kill idle process\n");
        return -1;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROC_UNUSED &&
            process_table[i].pid == pid) {

            process_table[i].state = PROC_ZOMBIE;

            if (process_table[i].kernel_stack != NULL) {
                free_page(process_table[i].kernel_stack);
                process_table[i].kernel_stack = NULL;
            }

            printf("[process] kill process ok, pid = %d\n", pid);
            return 0;
        }
    }

    printf("[process] pid %d not found\n", pid);
    return -1;
}

void process_print_list(void) {
    printf("PID   STATE      TIME_SLICE   NAME\n");

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROC_UNUSED) {
            printf("%-5d %-10s %-12d %s\n",
                   process_table[i].pid,
                   state_to_string(process_table[i].state),
                   process_table[i].time_slice,
                   process_table[i].name);
        }
    }
}
