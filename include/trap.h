#ifndef TRAP_H
#define TRAP_H

#define TRAP_TIMER      1
#define TRAP_FAULT      2
#define TRAP_SYSCALL    3   
#define TRAP_ILLEGAL    4   
#define TRAP_NULLPTR    5   
#define TRAP_DIVZERO    6  // 新增：除零异常编号
#define TRAP_OVERFLOW       7
#define TRAP_BREAKPOINT     8
#define TRAP_SINGLE_STEP    9
#define TRAP_ALIGNMENT      10
#define TRAP_STACK          11
#define TRAP_PROTECTION     12
#define TRAP_FPU            13
#define TRAP_PAGEFAULT_READ   14
#define TRAP_PAGEFAULT_WRITE  15
#define TRAP_UNKNOWN    99

void trap_init(void);
void trap_handler(int trap_no, const char *message);

void trap_test_fault(void);
void trap_test_illegal(void);
void trap_test_nullptr(void);
void trap_test_divzero(void); 
void trap_test_overflow(void);
void trap_test_breakpoint(void);
void trap_test_singlestep(void);
void trap_test_alignment(void);
void trap_test_stack(void);
void trap_test_protection(void);
void trap_test_fpu(void);
void trap_test_pf_read(void);
void trap_test_pf_write(void);

#endif