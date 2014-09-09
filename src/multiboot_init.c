#include <sys/stat.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <stdbool.h>

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

static char source_path[PATH_MAX];
static int source_device = -1, source_partition = -1;
static bool detach_all_children = false, is_recovery = false;
static bool source_mounted = false;
static struct fstab *fstab;

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
		perror("tracy_exec/execve");
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
			perror("scanf");
			return;
		}
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

		stat(fstab->recs[i].blk_device, &fstab->recs[i].statbuf);
	}
}

static void init_multiboot_mounts(void)
{
	KLOG_INFO(LOG_TAG, "%s\n", __func__);

	// resolve symlinks in fstab paths
	translate_fstab_paths(fstab);

	// parse cmdline
	import_kernel_cmdline(import_kernel_nv);

	// mount source fs
	char device[PATH_MAX];
	sprintf(device, "/dev/block/mmcblk%dp%d", source_device,
		source_partition);
	KLOG_INFO(LOG_TAG, "source_partition: %s\n", device);
	mount(device, PATH_MOUNTPOINT_SOURCE, "ext4",
	      MS_NOATIME | MS_NOEXEC | MS_NOSUID, NULL);

	source_mounted = true;
}

static struct fstab_rec *get_fstab_rec(const char *devname)
{
	int i;
	struct stat sb;

	bool use_stat = !stat(devname, &sb);

	for (i = 0; i < fstab->num_entries; i++) {
		if (use_stat && sb.st_dev == fstab->recs[i].statbuf.st_dev) {
			return &fstab->recs[i];
		}
		if (!strcmp(devname, fstab->recs[i].blk_device))
			return &fstab->recs[i];
	}

	return NULL;
}

int hook_mount(struct tracy_event *e)
{
	struct tracy_sc_args a;

	if (e->child->pre_syscall) {
		struct fstab_rec *fstabrec;
		tracy_child_addr_t devname_new;
		char *devname, *mountpoint;

		// we need to wait for init
		if (!source_mounted)
			return TRACY_HOOK_CONTINUE;

		// get args
		devname = get_patharg(e->child, e->args.a0, 1);
		mountpoint = get_patharg(e->child, e->args.a1, 1);
		unsigned long flags = (unsigned long)e->args.a3;
		KLOG_INFO(LOG_TAG, "mount %s on %s remount=%d, ro=%d\n",
			  devname, mountpoint, (flags & MS_REMOUNT),
			  (flags & MS_RDONLY));

		// detect booting android
		if (!strcmp(mountpoint, "/") && (flags & MS_REMOUNT)
		    && (flags & MS_RDONLY)) {
			KLOG_INFO(LOG_TAG, "mount rootfs RO. stop tracing!\n");
			detach_all_children = true;
			return TRACY_HOOK_DETACH_CHILD;
		}
		// check if we need to redirect this partition
		fstabrec = get_fstab_rec(devname);
		if (!fstabrec) {
			return TRACY_HOOK_CONTINUE;
		}

		KLOG_INFO(LOG_TAG, "handle mount %s on %s\n", devname,
			  mountpoint);

		/*
		   // allocate memory for new devname
		   if(tracy_mmap(e->child, &devname_new, NULL, strlen(PATH_MOUNTPOINT_SOURCE)+1,
		   PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1,
		   0) != 0) {
		   perror("tracy_mmap");
		   return TRACY_HOOK_ABORT;
		   }

		   // copy new devname
		   if (tracy_write_mem(e->child, devname_new,
		   (void*)PATH_MOUNTPOINT_SOURCE,
		   strlen(PATH_MOUNTPOINT_SOURCE)+1) < 0) {
		   perror("tracy_write_mem");
		   return TRACY_HOOK_ABORT;
		   }

		   // copy args
		   memcpy(&a, &(e->args), sizeof(struct tracy_sc_args));

		   // modify args
		   a.a0 = (unsigned)devname_new;
		   a.a2 = 0;
		   a.a3 |= MS_BIND;

		   // write new args
		   if (tracy_modify_syscall_args(e->child, a.syscall, &a)) {
		   return TRACY_HOOK_ABORT;
		   } */
	} else {
		// initialize if not already done
		if (!source_mounted && can_init()) {
			init_multiboot_mounts();
		}
	}

	return TRACY_HOOK_CONTINUE;
}

int sig_hook(struct tracy_event *e)
{
	if (detach_all_children)
		return TRACY_HOOK_DETACH_CHILD;

	return TRACY_HOOK_CONTINUE;
}

int main(int argc, char **argv)
{
	struct tracy *tracy;

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
		perror("mkpath");
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
