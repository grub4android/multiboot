#include <common.h>

void kperror(const char *message)
{
	const char *sep = ": ";
	if (!message) {
		sep = "";
		message = "";
	}

	ERROR("%s%s%s\n", message, sep, strerror(errno));
}
