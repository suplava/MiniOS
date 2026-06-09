#ifndef TRAP_H
#define TRAP_H

#define TRAP_TIMER      1
#define TRAP_FAULT      2
#define TRAP_SYSCALL    3
#define TRAP_UNKNOWN    99

void trap_init(void);
void trap_handler(int trap_no, const char *message);
void trap_test_fault(void);

#endif
