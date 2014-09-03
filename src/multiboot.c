#include <defs.h>
#include <util.h>
		
int syscall_enter(struct_sysent *sysent, struct tcb *tcp) {
	if(strcmp("write", sysent->sys_name)) return 0;
	//printf("SYSCALL: %s!\n", sysent->sys_name);
		
	static const char* str = "In ya face\n";
	sc_arg_set_str(tcp, 1, str);
	sc_arg_set_long(tcp, 2, strlen(str));

	return 0;
}

int syscall_exit(struct_sysent *sysent, struct tcb *tcp) {
	if(strcmp("write", sysent->sys_name)) return 0;
	//printf("EXIT: %lu!\n", tcp->u_rval);
	
	sc_arg_set_return(tcp, 4);

	return 0;
}
