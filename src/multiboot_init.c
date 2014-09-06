#include <stdio.h>
#include <unistd.h> 
#include <errno.h>
#include <stdlib.h>

#include <tracy.h>
#include <ll.h>

#define MAX_PARAMETERS 64
int run_init(struct tracy *tracy) {
	char *par[MAX_PARAMETERS];
	int i = 0;

	// build args
	par[i++] = "/init";
	par[i++] = (char*)0;

	// RUN
	if (!tracy_exec(tracy, par)) {
		perror("tracy_exec");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

int main(void) {
	struct tracy *tracy;

	/* Tracy options */
	tracy = tracy_init(TRACY_TRACE_CHILDREN);

	/* Start child */
	if (run_init(tracy)) {
		perror("tracy_exec");
		return EXIT_FAILURE;
	}

	/* Main event-loop */
	tracy_main(tracy);

	tracy_free(tracy);

	return EXIT_SUCCESS;
}