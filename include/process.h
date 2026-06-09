#ifndef MINIOS_PROCESS_H
#define MINIOS_PROCESS_H

#define MAX_PROCESSES 16
#define PROCESS_NAME_LEN 32

typedef enum {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_ZOMBIE
} process_state_t;

typedef struct process {
    int pid;
    char name[PROCESS_NAME_LEN];
    process_state_t state;
    int time_slice;
    void *kernel_stack;
} process_t;

void process_init(void);
int process_create(const char *name);
int process_kill(int pid);
void process_print_list(void);

#endif
