#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <tracy.h>
#include <linux/limits.h>
#include <sys/mman.h>

#define FILE_RECOVERY_BINARY "/sbin/recovery"

/* 
 * Function with behaviour like `mkdir -p'
 *
 * Source: http://niallohiggins.com/2009/01/08/mkpath-mkdir-p-alike-in-c-for-unix/
 */
int mkpath(const char *s, mode_t mode)
{
	char *q, *r = NULL, *path = NULL, *up = NULL;
	int rv;

	rv = -1;
	if (strcmp(s, ".") == 0 || strcmp(s, "/") == 0)
		return (0);

	if ((path = strdup(s)) == NULL)
		exit(1);

	if ((q = strdup(s)) == NULL)
		exit(1);

	if ((r = dirname(q)) == NULL)
		goto out;

	if ((up = strdup(r)) == NULL)
		exit(1);

	if ((mkpath(up, mode) == -1) && (errno != EEXIST))
		goto out;

	if ((mkdir(path, mode) == -1) && (errno != EEXIST))
		rv = -1;
	else
		rv = 0;

out:
	if (up != NULL)
		free(up);
	free(q);
	free(path);
	return (rv);
}

int system_is_recovery(void)
{
	struct stat sb;
	return !stat(FILE_RECOVERY_BINARY, &sb);
}

int can_init(void)
{
	struct stat sb;
	return !stat("/dev/block", &sb) && !stat("/proc/cmdline", &sb);
}

/*
 * Source: http://stackoverflow.com/questions/18547251/when-i-use-strlcpy-function-in-c-the-compilor-give-me-an-error
 */
#ifndef HAVE_STRLCAT
/*
 * '_cups_strlcat()' - Safely concatenate two strings.
 */

size_t				/* O - Length of string */
strlcat(char *dst,		/* O - Destination string */
	const char *src,	/* I - Source string */
	size_t size)
{				/* I - Size of destination string buffer */
	size_t srclen;		/* Length of source string */
	size_t dstlen;		/* Length of destination string */

	/*
	 * Figure out how much room is left...
	 */

	dstlen = strlen(dst);
	size -= dstlen + 1;

	if (!size)
		return (dstlen);	/* No room, return immediately... */

	/*
	 * Figure out how much room is needed...
	 */

	srclen = strlen(src);

	/*
	 * Copy the appropriate amount...
	 */

	if (srclen > size)
		srclen = size;

	memcpy(dst + dstlen, src, srclen);
	dst[dstlen + srclen] = '\0';

	return (dstlen + srclen);
}
#endif /* !HAVE_STRLCAT */

#ifndef HAVE_STRLCPY
/*
 * '_cups_strlcpy()' - Safely copy two strings.
 */

size_t				/* O - Length of string */
strlcpy(char *dst,		/* O - Destination string */
	const char *src,	/* I - Source string */
	size_t size)
{				/* I - Size of destination string buffer */
	size_t srclen;		/* Length of source string */

	/*
	 * Figure out how much room is needed...
	 */

	size--;

	srclen = strlen(src);

	/*
	 * Copy the appropriate amount...
	 */

	if (srclen > size)
		srclen = size;

	memcpy(dst, src, srclen);
	dst[srclen] = '\0';

	return (srclen);
}
#endif /* !HAVE_STRLCPY */

char *get_patharg(struct tracy_child *child, long addr, int real)
{
	static const int len = PATH_MAX;
	char path[PATH_MAX];
	struct stat sb;

	// read string
	memset(path, 0, len);
	tracy_read_mem(child, path, (void *)addr, len);

	if (real && !stat(path, &sb)) {
		char *path_real = malloc(PATH_MAX);
		memset(path_real, 0, len);

		// resolve symlinks
		if (realpath(path, path_real) != NULL && !errno)
			return path_real;
		else
			free(path_real);
	} else
		return strdup(path);
}

tracy_child_addr_t copy_patharg(struct tracy_child * child, const char *path)
{
	long ret;
	static const int len = PATH_MAX + 1;
	struct tracy_sc_args a;
	tracy_child_addr_t path_new;

	// allocate memory for new devname
	if (tracy_mmap(child, &path_new, NULL, len,
		       PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != 0) {
		goto err;
	}
	// copy new devname
	if (tracy_write_mem
	    (child, path_new, (char *)path, (size_t) strlen(path) + 1) < 0) {
		goto err_munmap;
	}

	return path_new;

err_munmap:
	tracy_munmap(child, &ret, path_new, len);
err:
	return NULL;
}

void free_patharg(struct tracy_child *child, tracy_child_addr_t addr)
{
	long ret;
	tracy_munmap(child, &ret, addr, PATH_MAX + 1);
}
