#ifndef TRAP_H
#define TRAP_H

#define TRAP_TIMER      1
#define TRAP_FAULT      2
#define TRAP_SYSCALL    3   // 原值完全不动，所有旧代码逻辑不变
#define TRAP_ILLEGAL    4   // 新增，占用空闲4
#define TRAP_NULLPTR    5   // 新增，占用空闲5
#define TRAP_UNKNOWN    99

void trap_init(void);
void trap_handler(int trap_no, const char *message);

void trap_test_fault(void);
void trap_test_illegal(void);
void trap_test_nullptr(void);

#endif