#include <common.h>

static const size_t block_size = 512;

/*
 * Source: http://stackoverflow.com/questions/15545341/process-name-from-its-pid-in-linux
 */
char *get_process_name_by_pid(const int pid)
{
	char *name = (char *)calloc(1024, sizeof(char));
	if (name) {
		sprintf(name, "/proc/%d/cmdline", pid);
		FILE *f = fopen(name, "r");
		if (f) {
			size_t size;
			size = fread(name, sizeof(char), 1024, f);
			if (size > 0) {
				if ('\n' == name[size - 1])
					name[size - 1] = '\0';
			}
			fclose(f);
		}
	}
	return name;
}

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

int util_mount(const char *source, const char *target,
	       const char *filesystemtype, unsigned long mountflags,
	       const void *data)
{

	if (mkpath(target, S_IRWXU | S_IRWXG | S_IRWXO)) {
		return -1;
	}

	return mount(source, target, filesystemtype, mountflags, data);
}

char *make_loop(const char *path)
{
	static int loops_created = 0;

	char buf[PATH_MAX];
	int minor = 255 - loops_created;

	if (!path) {
		sprintf(buf, PATH_MOUNTPOINT_DEV "/loop%d", minor);
		path = buf;
	}

	if (mknod(path, S_IRUSR | S_IWUSR | S_IFBLK, makedev(7, minor))) {
		kperror("mknod");
		return NULL;
	}

	loops_created++;
	return strdup(buf);
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
	} else {
		return strdup(path);
	}

	KLOG_ERROR(LOG_TAG, "%s: Error getting patharg!\n", __func__);
	return NULL;
}

tracy_child_addr_t copy_patharg(struct tracy_child * child, const char *path)
{
	long rc;
	int len = strlen(path) + 1;
	tracy_child_addr_t path_new = NULL;

	if (len > PATH_MAX + 1) {
		KLOG_ERROR(LOG_TAG, "%s: path is too long!\n", __func__);
		goto err;
	}
	// allocate memory for new devname
	rc = tracy_mmap(child, &path_new, NULL, PATH_MAX + 1,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (rc < 0 || !path_new) {
		goto err;
	}
	// copy new devname
	rc = tracy_write_mem(child, path_new, (char *)path,
			     (size_t) strlen(path) + 1);
	if (rc < 0) {
		goto err_munmap;
	}

	return path_new;

err_munmap:
	tracy_munmap(child, &rc, path_new, len);
err:
	KLOG_ERROR(LOG_TAG, "%s: Error copying patharg!\n", __func__);
	return NULL;
}

void free_patharg(struct tracy_child *child, tracy_child_addr_t addr)
{
	long ret;
	tracy_munmap(child, &ret, addr, PATH_MAX + 1);
}

static unsigned long get_blknum(const char *path)
{
	int fd;
	unsigned long numblocks = 0;

	fd = open(path, O_RDONLY);
	if (!fd)
		return 0;

	if (ioctl(fd, BLKGETSIZE, &numblocks) == -1)
		return 0;

	close(fd);
	return numblocks;
}

int do_exec(char **args)
{
	pid_t pid;
	int status = 0;

	pid = fork();
	if (!pid) {
		execve(args[0], args, NULL);
		exit(0);
	} else {
		waitpid(pid, &status, 0);
	}

	return status;
}

int createRawImage(const char *source, const char *target)
{
	unsigned long numblocks = get_blknum(source);
	char *par[64];
	char buf[PATH_MAX];
	int i = 0;
	char *buf_of = NULL, *buf_bs = NULL, *buf_count = NULL;
	int rc;

	// tool
	par[i++] = "/multiboot/busybox";
	par[i++] = "dd";

	// input
	par[i++] = "if=/dev/zero";

	// output
	sprintf(buf, "of=%s", target);
	buf_of = strdup(buf);
	par[i++] = buf_of;

	// blocksize
	sprintf(buf, "bs=%d", block_size);
	buf_bs = strdup(buf);
	par[i++] = buf_bs;

	// count
	sprintf(buf, "count=%lu", numblocks);
	buf_count = strdup(buf);
	par[i++] = buf_count;

	// end
	par[i++] = (char *)0;

	// exec
	rc = do_exec(par);

	// cleanup
	free(buf_of);
	free(buf_bs);
	free(buf_count);

	return rc;
}

int set_loop(char *device, char *file, int ro)
{
	char *par[64];
	int i = 0;

	// tool
	par[i++] = "/multiboot/busybox";
	par[i++] = "losetup";

	// access mode
	if (ro)
		par[i++] = "-r";

	// paths
	par[i++] = device;
	par[i++] = file;

	// end
	par[i++] = (char *)0;

	return do_exec(par);
}

int copy_file(char *source, char *target)
{
	char *par[64];
	int i = 0;

	// tool
	par[i++] = "/multiboot/busybox";
	par[i++] = "cp";
	par[i++] = source;
	par[i++] = target;

	// end
	par[i++] = (char *)0;

	return do_exec(par);
}
