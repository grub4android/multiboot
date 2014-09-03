#ifndef _UTIL_H
#define _UTIL_H

#if defined(I386)
#error "i386 NOT SUPPORTED"

#elif defined(X86_64)
#include <sys/user.h>
typedef struct user_regs_struct regs_type_t;
#define setArg0(regs, val) regs.rdi = val
#define setArg1(regs, val) regs.rsi = val
#define setArg2(regs, val) regs.rdx = val
#define setReturn(regs, val) regs.rax = val

#elif defined(ARM)
#include <asm/ptrace.h>
typedef struct pt_regs regs_type_t;
#define setArg0(regs, val) regs.ARM_r0 = val
#define setArg1(regs, val) regs.ARM_r1 = val
#define setArg2(regs, val) regs.ARM_r2 = val
#define setReturn(regs, val) regs.ARM_r0 = val
#endif

int sc_arg_set_str(struct tcb *tcp, int argid, const char* data);
int ptrace_getregs(long pid, void *regs_out);
int ptrace_setregs(long pid, void *regs);
int sc_arg_set_long(struct tcb *tcp, int argid, unsigned long value);
int sc_arg_set_return(struct tcb *tcp, unsigned long value);

#endif
