#include <common.h>

static const size_t block_size = 512;

/*
 * Source: http://stackoverflow.com/questions/15545341/process-name-from-its-pid-in-linux
 */
char *get_process_name_by_pid(const int pid)
{
	char *name = (char *)calloc(1024, sizeof(char));
	if (name) {
		snprintf(name, 1024, "/proc/%d/cmdline", pid);
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
	// get fstype of source partition
	if (!filesystemtype && !(mountflags & MS_BIND)) {
		filesystemtype = get_fstype(source);
		if (!filesystemtype)
			filesystemtype = "ext4";

	}

	char *__source = source ? strdup(source) : NULL;
	char *__filesystemtype = filesystemtype ? strdup(filesystemtype) : NULL;
	char *__target = target ? strdup(target) : NULL;

	if (!(mountflags & MS_BIND))
		check_fs(__source, __filesystemtype, __target);

	if (__target)
		free(__target);
	if (__filesystemtype)
		free(__filesystemtype);
	if (__source)
		free(__source);

	return mount(source, target, filesystemtype, mountflags, data);
}

char *make_loop(const char *path)
{
	static int loops_created = 0;

	char buf[PATH_MAX];
	int minor = 255 - loops_created;

	if (!path) {
		snprintf(buf, ARRAY_SIZE(buf),
			 PATH_MOUNTPOINT_DEV "/block/loop%d", minor);
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
	if (tracy_read_mem(child, path, (void *)addr, len) < 0) {
		kperror("tracy_read_mem");
		return NULL;
	}

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

	ERROR("%s: Error getting patharg!\n", __func__);
	return NULL;
}

tracy_child_addr_t copy_patharg(struct tracy_child * child, const char *path)
{
	long rc;
	int len = strlen(path) + 1;
	tracy_child_addr_t path_new = NULL;

	if (len > PATH_MAX + 1) {
		ERROR("%s: path is too long!\n", __func__);
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
	ERROR("%s: Error copying patharg!\n", __func__);
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

int createRawImage(const char *source, const char *target, unsigned long blocks)
{
	unsigned long numblocks = source ? get_blknum(source) : blocks;
	char *par[64];
	char buf[PATH_MAX];
	int i = 0;
	char *buf_if = NULL, *buf_of = NULL, *buf_bs = NULL, *buf_count = NULL;
	int rc;

	// tool
	par[i++] = PATH_MULTIBOOT_BUSYBOX;
	par[i++] = "dd";

	// input
	if (source) {
		snprintf(buf, ARRAY_SIZE(buf), "if=%s", source);
		buf_if = strdup(buf);
		par[i++] = buf_if;
	} else
		par[i++] = "if=" PATH_MOUNTPOINT_DEV "/zero";

	// output
	snprintf(buf, ARRAY_SIZE(buf), "of=%s", target);
	buf_of = strdup(buf);
	par[i++] = buf_of;

	// blocksize
	snprintf(buf, ARRAY_SIZE(buf), "bs=%d", block_size);
	buf_bs = strdup(buf);
	par[i++] = buf_bs;

	// count
	snprintf(buf, ARRAY_SIZE(buf), "count=%lu", numblocks);
	buf_count = strdup(buf);
	par[i++] = buf_count;

	// end
	par[i++] = (char *)0;

	// exec
	rc = do_exec(par);

	// cleanup
	if (buf_if)
		free(buf_if);
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
	par[i++] = PATH_MULTIBOOT_BUSYBOX;
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

int util_copy(char *source, char *target, bool recursive, bool force)
{
	char *par[64];
	int i = 0;

	// tool
	par[i++] = PATH_MULTIBOOT_BUSYBOX;
	par[i++] = "cp";

	if (recursive)
		par[i++] = "-R";

	if (force)
		par[i++] = "-f";

	par[i++] = source;
	par[i++] = target;

	// end
	par[i++] = (char *)0;

	return do_exec(par);
}

int util_chmod(char *path, char *mode, bool recursive)
{
	char *par[64];
	int i = 0;

	// tool
	par[i++] = PATH_MULTIBOOT_BUSYBOX;
	par[i++] = "chmod";

	if (recursive)
		par[i++] = "-R";

	par[i++] = mode;
	par[i++] = path;

	// end
	par[i++] = (char *)0;

	return do_exec(par);
}

int format_path(char *path)
{
	char *par[64];
	int i = 0;
	char buf[PATH_MAX + 30];

	snprintf(buf, ARRAY_SIZE(buf), PATH_MULTIBOOT_BUSYBOX " rm -Rf %s",
		 path);

	// tool
	par[i++] = PATH_MULTIBOOT_BUSYBOX;
	par[i++] = "sh";
	par[i++] = "-c";
	par[i++] = buf;

	// end
	par[i++] = (char *)0;

	return do_exec(par);
}

const char *get_fstype(const char *blk_device)
{
	const char *type;
	blkid_probe pr;

	pr = blkid_new_probe_from_filename(blk_device);
	if (blkid_do_fullprobe(pr)) {
		blkid_free_probe(pr);
		ERROR("Can't probe device %s\n", blk_device);
		return NULL;
	}

	if (blkid_probe_lookup_value(pr, "TYPE", &type, NULL) < 0) {
		blkid_free_probe(pr);
		ERROR("can't find filesystem on device %s\n", blk_device);
		return NULL;
	}

	blkid_free_probe(pr);

	return type;
}

int make_ext4fs(char *path)
{
	char *par[64];
	int i = 0;

	// tool
	par[i++] = "/multiboot/sbin/mkfs.ext4";
	par[i++] = path;

	// end
	par[i++] = (char *)0;

	return do_exec(par);
}

int check_fs_nomount(char *path)
{
	char *par[64];
	int i = 0;

	// tool
	par[i++] = "/multiboot/sbin/e2fsck";
	par[i++] = "-fy";
	par[i++] = path;

	// end
	par[i++] = (char *)0;

	return do_exec(par);
}

int patch_vold(void)
{
	return sed_replace("/init.rc",
			   "s/\\/system\\/bin\\/vold/\\/multiboot\\/sbin\\/init voldwrapper/g");
}

int sed_replace(const char *file, const char *regex)
{
	char *par[64];
	int i = 0;

	// tool
	par[i++] = PATH_MULTIBOOT_BUSYBOX;
	par[i++] = "sed";
	par[i++] = "-i";
	par[i++] = (char *)regex;
	par[i++] = (char *)file;

	// end
	par[i++] = (char *)0;

	return do_exec(par);
}

int dump_strings(const char *filename, char **result, int size, check_string cb)
{
	int c;
	char *string = NULL;
	int strsz = 0;
	int count = 0;
	int rc = 0;

	// open file
	FILE *file = fopen(filename, "rb");
	if (!file)
		return -1;

	// initial buffer
	strsz = 100;
	string = malloc(strsz * sizeof(char));

	do {
		c = fgetc(file);
		if (isprint(c) || c == '\t') {
			// increase string size
			count++;

			// increase buffser size if necessary (+1 for 0 terminator)
			if (count + 1 > strsz) {
				strsz = count + 1;
				string = realloc(string, strsz * sizeof(char));
			}
			// write char
			string[count - 1] = (char)c;
		}

		else {
			// end of string
			if (count > 0) {
				// add 0 terminator
				string[count] = '\0';

				if (rc < size) {
					if (!cb || (cb && cb(string))) {
						// add string to results
						result[rc++] = strdup(string);
					}
				} else {
					break;
				}
			}
			// start new string
			count = 0;
		}
	} while (c != EOF);

	// close file
	fclose(file);
	file = NULL;

	return rc;
}

int create_ums_script(char *path, char *file)
{
	FILE *f;
	f = fopen(path, "w");
	if (!f)
		return -1;

	fputs("#!/sbin/sh\n", f);
	fputs("stop adbd\n", f);
	fputs("echo 0 > /sys/class/android_usb/android0/enable\n", f);
	fputs
	    ("echo mtp,adb,mass_storage > /sys/class/android_usb/android0/functions\n",
	     f);

	fputs("echo ", f);
	fputs(file, f);
	fputs(" > /sys/devices/platform/msm_hsusb/gadget/lun0/file\n", f);

	fputs("echo 0 > /sys/devices/platform/msm_hsusb/gadget/lun0/cdrom\n",
	      f);
	fputs("echo 0 > /sys/devices/platform/msm_hsusb/gadget/lun0/ro\n", f);
	fputs("echo 1 > /sys/class/android_usb/android0/enable\n", f);
	fputs("start adbd\n", f);
	fputs("setprop sys.usb.state mtp,adb,mass_storage\n", f);
	fputs("while true; do sleep 10000; done\n", f);

	fclose(f);
	return 0;
}
