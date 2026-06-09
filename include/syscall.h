#ifndef SYSCALL_H
#define SYSCALL_H

#define SYS_WRITE   1
#define SYS_GETPID  2
#define SYS_YIELD   3
#define SYS_EXIT    4

void syscall_init(void);
int syscall_handler(int sysno, const char *text, int arg2, int arg3);

#endif
