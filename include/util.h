#ifndef _UTIL_H
#define _UTIL_H

int sc_arg_set_str(struct tcb *tcp, int argid, const char* data);
int ptrace_getregs(long pid, void *regs_out);
int ptrace_setregs(long pid, void *regs);
int sc_arg_set_long(struct tcb *tcp, int argid, unsigned long value);
int sc_arg_set_return(struct tcb *tcp, unsigned long value);

#endif
