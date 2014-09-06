#include <stdio.h>
#include <unistd.h> 
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <tracy.h>
#include <ll.h>

#define MAX_PARAMETERS 64
#define FILE_RECOVERY_BINARY "/sbin/recovery"

int run_init(struct tracy *tracy) {
	char *par[MAX_PARAMETERS];
	int i = 0, ret = 0;

	// build args
	par[i++] = "/init";
	par[i++] = (char*)0;

	// RUN
	if(tracy) ret = !tracy_exec(tracy, par);
	else ret = execve(par[0], par, NULL);
	if (ret) {
		perror("tracy_exec");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

int system_is_recovery(void) {
    struct stat sb;
    return !stat(FILE_RECOVERY_BINARY, &sb);
}

int main(void) {
	struct tracy *tracy;

	// trace recovery only
	if(!system_is_recovery()) {
		run_init(NULL);
		exit(1);
	}

	// tracy init
	tracy = tracy_init(TRACY_TRACE_CHILDREN);

	// run and trace /init
	if (run_init(tracy)) {
		perror("tracy_exec");
		return EXIT_FAILURE;
	}

	// Main event-loop
	tracy_main(tracy);

	// cleanup
	tracy_free(tracy);

	return EXIT_SUCCESS;
}
