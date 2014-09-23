#include <common.h>

void kperror(const char *message)
{
	const char *sep = ": ";
	if (!message) {
		sep = "";
		message = "";
	}

	ERROR("%s%s%s", message, sep, strerror(errno));
}
