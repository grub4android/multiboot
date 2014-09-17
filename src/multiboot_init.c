#include <sys/stat.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

#include <lib/cmdline.h>
#include <lib/fs_mgr.h>
#include <lib/klog.h>
#include <util.h>

#include <tracy.h>
#include <ll.h>

#define MAX_PARAMETERS 64
#define PATH_MOUNTPOINT_SOURCE "/multiboot/source"
#define FILE_FSTAB "/multiboot/fstab"
#define LOG_TAG "multiboot"
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))

struct redirect_info {
	char *syscall_name;
	int argnum;
	bool resolve_symlinks;
	int syscall_number;
};

static char source_path[PATH_MAX];
static int source_device = -1, source_partition = -1;
static bool detach_all_children = false, is_recovery = false;
static bool multiboot_initialized = false;
static bool enable_multiboot = false;
static struct fstab *fstab;
static struct redirect_info info[] = {
	{"stat", 0, 1, 0},
	{"lstat", 0, 0, 0},
	{"newstat", 0, 1, 0},
	{"newlstat", 0, 0, 0},
	{"stat64", 0, 1, 0},
	{"lstat64", 0, 0, 0},
	//{"chroot", 0, 0, 0},
	//{"mknod", 0, 0, 0},
	{"chmod", 0, 0, 0},
	{"open", 0, 1, 0},
	{"access", 0, 0, 0},
	{"chown", 0, 1, 0},
	{"lchown", 0, 0, 0},
	{"chown16", 0, 1, 0},
	{"lchown16", 0, 0, 0},
	{"utime", 0, 0, 0},
	{"utimes", 0, 0, 0},
	//{"chdir", 0, 0, 0},
	{"mknodat", 1, 0, 0},
	{"futimesat", 1, 0, 0},
	{"faccessat", 1, 0, 0},
	{"fchmodat", 1, 0, 0},
	{"fchownat", 1, 0, 0},
	{"openat", 1, 0, 0},
	{"newfstatat", 1, 1, 0},
	{"fstatat64", 1, 1, 0},
	{"utimensat", 1, 0, 0},
	//{"execve", 0, 0, 0},
};

static void kperror(const char *message)
{
	const char *sep = ": ";
	if (!message) {
		sep = "";
		message = "";
	}

	KLOG_ERROR(LOG_TAG, "%s%s%s", message, sep, strerror(errno));
}

static int run_init(struct tracy *tracy)
{
	char *par[MAX_PARAMETERS];
	int i = 0, ret = 0;

	// build args
	par[i++] = "/init";
	par[i++] = (char *)0;

	// RUN
	if (tracy)
		ret = !tracy_exec(tracy, par);
	else
		ret = execve(par[0], par, NULL);

	// error check
	if (ret) {
		kperror("tracy_exec/execve");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

static void import_kernel_nv(char *name)
{
	char *value = strchr(name, '=');
	int name_len = strlen(name);

	if (value == 0)
		return;
	*value++ = 0;
	if (name_len == 0)
		return;

	if (!strcmp(name, "multiboot_source")) {
		if (sscanf
		    (value, "(hd%d,%d)%s", &source_device, &source_partition,
		     source_path) < 0) {
			kperror("scanf");
			return;
		}
		enable_multiboot = true;
	}
}

static void translate_fstab_paths(struct fstab *fstab)
{
	int i;
	char devname_real[PATH_MAX + 1];

	for (i = 0; i < fstab->num_entries; i++) {
		if (realpath(fstab->recs[i].blk_device, devname_real) != NULL) {
			free(fstab->recs[i].blk_device);
			fstab->recs[i].blk_device = strdup(devname_real);
		}

		if (stat(fstab->recs[i].blk_device, &fstab->recs[i].statbuf) <
		    0)
			KLOG_ERROR(LOG_TAG, "%s: couldn't stat %s\n", __func__,
				   fstab->recs[i].blk_device);
		else {
			dev_t dev = fstab->recs[i].statbuf.st_rdev;
			KLOG_DEBUG(LOG_TAG, "%s: %s: major=%u minor=%u\n",
				   __func__, fstab->recs[i].blk_device,
				   major(dev), minor(dev));
		}
	}
}

static void init_multiboot_environment(void)
{
	int i;
	char buf[PATH_MAX];
	struct stat sb;

	KLOG_INFO(LOG_TAG, "%s\n", __func__);

	// resolve symlinks in fstab paths
	translate_fstab_paths(fstab);

	// parse cmdline
	import_kernel_cmdline(import_kernel_nv);

	if (enable_multiboot) {
		// mount source partition
		sprintf(buf, "/dev/block/mmcblk%dp%d", source_device,
			source_partition);
		KLOG_INFO(LOG_TAG, "source_partition: %s\n", buf);
		mount(buf, PATH_MOUNTPOINT_SOURCE, "ext4",
		      0, "context=u:object_r:sdcard_external:s0");

		// create main mb directory
		sprintf(buf, PATH_MOUNTPOINT_SOURCE "%s", source_path);
		KLOG_INFO(LOG_TAG, "source_path: %s\n", buf);
		mkpath(buf, S_IRWXU | S_IRWXG | S_IRWXO);

		// setup filesystem
		for (i = 0; i < fstab->num_entries; i++) {
			if (!strcmp(fstab->recs[i].fs_type, "ext4")) {
				// create directory
				sprintf(buf, PATH_MOUNTPOINT_SOURCE "%s%s",
					source_path,
					fstab->recs[i].mount_point);
				if (mkpath(buf, S_IRWXU | S_IRWXG | S_IRWXO)) {
					kperror("mkpath");
					continue;
				}
				// set bind directory as device
				fstab->recs[i].replacement_device = strdup(buf);
				fstab->recs[i].replacement_bind = 1;
			}

			else {
				// create filesystem image
				sprintf(buf, PATH_MOUNTPOINT_SOURCE "%s%s.img",
					source_path,
					fstab->recs[i].mount_point);
				if (stat(buf, &sb)) {
					if (createRawImage
					    (fstab->recs[i].blk_device, buf)) {
						kperror("createRawImage");
						continue;
					}
				}
				// create file backup
				char *file = strdup(buf);

				// create device node
				sprintf(buf, "/dev/block/loop%d", 255 - i);
				mknod(buf, fstab->recs[i].statbuf.st_mode,
				      makedev(7, 255 - i));

				// setup loop
				if (set_loop(buf, file, 0))
					kperror("set_loop");

				// set loop dev as device
				fstab->recs[i].replacement_device = strdup(buf);
				fstab->recs[i].replacement_bind = 0;

				// cleanup
				free(file);
			}
		}

		multiboot_initialized = true;
	}
}

static struct fstab_rec *get_fstab_rec(const char *devname)
{
	int i;
	struct stat sb;

	bool use_stat = !stat(devname, &sb);

	for (i = 0; i < fstab->num_entries; i++) {
		if (use_stat && sb.st_rdev == fstab->recs[i].statbuf.st_rdev) {
			KLOG_DEBUG(LOG_TAG, "%s: stat: %d\n", __func__, i);
			return &fstab->recs[i];
		}
		if (!strcmp(devname, fstab->recs[i].blk_device)) {
			KLOG_DEBUG(LOG_TAG, "%s: strcmp: %d\n", __func__, i);
			return &fstab->recs[i];
		}
	}

	KLOG_DEBUG(LOG_TAG, "%s: no match\n", __func__);
	return NULL;
}

int hook_mount(struct tracy_event *e)
{
	struct tracy_sc_args a;
	struct fstab_rec *fstabrec;
	tracy_child_addr_t devname_new = NULL;
	char *devname = NULL, *mountpoint = NULL;
	int rc = TRACY_HOOK_CONTINUE;

	if (e->child->pre_syscall) {
		// we need to wait for init
		if (!multiboot_initialized)
			goto out;

		// get args
		devname = get_patharg(e->child, e->args.a0, 1);
		mountpoint = get_patharg(e->child, e->args.a1, 1);
		unsigned long flags = (unsigned long)e->args.a3;
		KLOG_DEBUG(LOG_TAG, "mount %s on %s remount=%lu, ro=%lu\n",
			   devname, mountpoint, (flags & MS_REMOUNT),
			   (flags & MS_RDONLY));

		// detect booting android
		if (!strcmp(mountpoint, "/") && (flags & MS_REMOUNT)
		    && (flags & MS_RDONLY)) {
			KLOG_INFO(LOG_TAG, "mount rootfs RO. stop tracing!\n");
			detach_all_children = true;
			rc = TRACY_HOOK_DETACH_CHILD;
			goto out;
		}
		// check if we need to redirect this partition
		fstabrec = get_fstab_rec(devname);
		if (!fstabrec) {
			goto out;
		}

		KLOG_INFO(LOG_TAG, "hijack: mount %s on %s\n", devname,
			  mountpoint);

		// copy new devname
		devname_new =
		    copy_patharg(e->child, fstabrec->replacement_device);
		if (!devname_new) {
			kperror("copy_patharg");
			rc = TRACY_HOOK_ABORT;
			goto out;
		}
		// copy args
		memcpy(&a, &(e->args), sizeof(struct tracy_sc_args));

		// modify args
		a.a0 = (long)devname_new;
		if (fstabrec->replacement_bind) {
			a.a2 = 0;
			a.a3 |= MS_BIND;
		}
		// write new args
		if (tracy_modify_syscall_args(e->child, a.syscall, &a)) {
			kperror("tracy_modify_syscall_args");
			rc = TRACY_HOOK_ABORT;
			goto out;
		}
		// set devname so we can free it later
		e->child->custom = (void *)devname_new;
	} else {
		// free previous data
		if (e->child->custom) {
			free_patharg(e->child,
				     (tracy_child_addr_t) e->child->custom);
			e->child->custom = NULL;
		}
		// initialize if not already done
		if (!multiboot_initialized && can_init()) {
			init_multiboot_environment();

			// stop tracing if multiboot cmdline is invalid or wasn't found
			if (!enable_multiboot) {
				KLOG_INFO(LOG_TAG,
					  "multiboot disabled. stop tracing!\n");
				detach_all_children = true;
				rc = TRACY_HOOK_DETACH_CHILD;
				goto out;
			}
		}
	}

out:
	if (devname)
		free(devname);
	if (mountpoint)
		free(mountpoint);
	if (devname_new && rc)
		free_patharg(e->child, devname_new);

	return rc;
}

int redirect_file_access(struct tracy_event *e, int argpos,
			 bool resolve_symlinks)
{
	struct tracy_sc_args a;
	int rc = TRACY_HOOK_CONTINUE;
	char *path = NULL;
	tracy_child_addr_t devname_new;
	struct fstab_rec *fstabrec;
	long *argptr;

	if (e->child->pre_syscall) {
		// we need to wait for init
		if (!multiboot_initialized)
			goto out;

		// get path
		argptr = &e->args.a0;
		path = get_patharg(e->child, argptr[argpos], resolve_symlinks);

		// check if we need to redirect this partition
		fstabrec = get_fstab_rec(path);
		if (!fstabrec) {
			goto out;
		}
		// ignore symlinks for calls which don't resolve them
		struct stat sb;
		if (!resolve_symlinks && !lstat(path, &sb)
		    && S_ISLNK(sb.st_mode)) {
			goto out;
		}

		KLOG_INFO(LOG_TAG, "%s(%s): redirect %s arg=%d\n", __func__,
			  get_syscall_name_abi(e->syscall_num, e->abi), path,
			  argpos);

		// copy new devname
		devname_new =
		    copy_patharg(e->child,
				 fstabrec->replacement_bind ? "/dev/null" :
				 fstabrec->replacement_device);
		if (!devname_new) {
			kperror("copy_patharg");
			rc = TRACY_HOOK_ABORT;
			goto out;
		}
		// copy args
		memcpy(&a, &(e->args), sizeof(struct tracy_sc_args));

		// modify args
		argptr = &a.a0;
		argptr[argpos] = (long)devname_new;

		// write new args
		if (tracy_modify_syscall_args(e->child, a.syscall, &a)) {
			kperror("tracy_modify_syscall_args");
			rc = TRACY_HOOK_ABORT;
			goto out;
		}
		// set devname so we can free it later
		e->child->custom = (void *)devname_new;
	} else {
		// free previous data
		if (e->child->custom) {
			free_patharg(e->child,
				     (tracy_child_addr_t) e->child->custom);
			e->child->custom = NULL;
		}
	}

out:
	if (path)
		free(path);
	if (devname_new && rc)
		free_patharg(e->child, devname_new);
	return rc;
}

int sig_hook(struct tracy_event __attribute__ ((unused)) * e)
{
	if (detach_all_children)
		return TRACY_HOOK_DETACH_CHILD;

	return TRACY_HOOK_CONTINUE;
}

int hook_fileaccess(struct tracy_event *e)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(info); i++) {
		if (info[i].syscall_number == e->syscall_num) {
			return redirect_file_access(e, info[i].argnum,
						    info[i].resolve_symlinks);
		}
	}

	return TRACY_HOOK_ABORT;
}

int main(int __attribute__ ((unused)) argc, char
	 __attribute__ ((unused)) ** argv)
{
	struct tracy *tracy;
	unsigned int i;

	// init klog
	klog_init();
	klog_set_level(6);
	KLOG_INFO(LOG_TAG, "init\n");

	// read fstab
	fstab = fs_mgr_read_fstab(FILE_FSTAB);
	if (!fstab) {
		KLOG_ERROR(LOG_TAG, "failed to open %s\n", FILE_FSTAB);
		exit(1);
	}
	// create mountpoint
	if (mkpath(PATH_MOUNTPOINT_SOURCE, S_IRWXU | S_IRWXG | S_IRWXO)) {
		kperror("mkpath");
		exit(1);
	}
	// identify recovery boot
	is_recovery = system_is_recovery();

	// tracy init
	tracy = tracy_init(TRACY_TRACE_CHILDREN | TRACY_MEMORY_FALLBACK);
	tracy_set_signal_hook(tracy, sig_hook);

	// hook mount
	if (tracy_set_hook(tracy, "mount", TRACY_ABI_NATIVE, hook_mount)) {
		KLOG_ERROR(LOG_TAG, "Could not hook mount\n");
		return EXIT_FAILURE;
	}
	// hooks for file access redirection
	for (i = 0; i < ARRAY_SIZE(info); i++) {
		info[i].syscall_number =
		    get_syscall_number_abi(info[i].syscall_name,
					   TRACY_ABI_NATIVE);
		if (tracy_set_hook
		    (tracy, info[i].syscall_name, TRACY_ABI_NATIVE,
		     hook_fileaccess)) {
			KLOG_ERROR(LOG_TAG, "Could not hook %s\n",
				   info[i].syscall_name);
			return EXIT_FAILURE;
		}
	}

	// run and trace /init
	if (run_init(tracy)) {
		kperror("tracy_exec");
		return EXIT_FAILURE;
	}
	// Main event-loop
	tracy_main(tracy);

	// cleanup
	tracy_free(tracy);

	return EXIT_SUCCESS;
}
