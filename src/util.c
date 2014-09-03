#include <defs.h>

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

int sc_arg_set_str(struct tcb *tcp, int argid, const char* data) {
	int i;
	for(i=0; i<strlen(data)+1; i+=4) {		
		if(ptrace(PTRACE_POKEDATA, tcp->pid, tcp->u_arg[argid] + i, *(int*)(data+i)))
			return -1;
	}
	return 0;
}

int ptrace_getregs(long pid, void *regs_out)
{
        if (ptrace(PTRACE_GETREGS, pid, 0, regs_out) < 0)
                return -errno;
        return 0;
}

int ptrace_setregs(long pid, void *regs)
{
        if (ptrace(PTRACE_SETREGS, pid, 0, regs) < 0)
               return -errno;
        return 0;
}

int sc_arg_set_long(struct tcb *tcp, int argid, unsigned long value) {
	regs_type_t regs;

	if (ptrace_getregs(tcp->pid, &regs))
		return -1;

	if(argid==0)
		setArg0(regs, value);
	else if(argid==1)
		setArg1(regs, value);
	else if(argid==2)
		setArg2(regs, value);
	else return -1;

	if (ptrace_setregs(tcp->pid, &regs))
		return -1;

	return 0;
}

int sc_arg_set_return(struct tcb *tcp, unsigned long value) {
	regs_type_t regs;

	if (ptrace_getregs(tcp->pid, &regs))
		return -1;

	setReturn(regs, value);

	if (ptrace_setregs(tcp->pid, &regs))
		return -1;

	return 0;
}
