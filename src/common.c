#include <common.h>

void kperror(const char *message)
{
	const char *sep = ": ";
	if (!message) {
		sep = "";
		message = "";
	}

	KLOG_ERROR(LOG_TAG, "%s%s%s", message, sep, strerror(errno));
}
