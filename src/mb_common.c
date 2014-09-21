#include <tracy.h>
#include <ll.h>
#include <stdbool.h>
#include <sys/mount.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>

#include <lib/fs_mgr.h>
#include <lib/klog.h>
#include <util.h>

#include <mb_common.h>
#include <mb_fs_redirection.h>
#include <mb_fstab_patcher.h>

// general information
static int is_sndstage = 0;
static bool is_multiboot = false;
static bool multiboot_initialized = false;
static struct fstab *mbfstab = NULL;

static char source_path[PATH_MAX];
static int source_device = -1, source_partition = -1;
static char source_device_path[PATH_MAX];
static char multiboot_dir[PATH_MAX];

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

int system_is_recovery(void)
{
	static int cache_available = 0;
	static int cache_ret = 0;
	struct stat sb;

	if (!cache_available) {
		cache_ret = !stat(FILE_RECOVERY_BINARY, &sb);
		cache_available = 1;
	}

	return cache_ret;
}

int system_is_sndstage(void)
{
	return is_sndstage;
}

int system_is_multiboot(void)
{
	return is_multiboot;
}

int can_init(void)
{
	struct stat sb;
	return !stat("/dev/block", &sb);
}

int mb_ready(void)
{
	return multiboot_initialized;
}

int needs_context(void)
{
	static int has_cache = 0;
	static int cache_ret = 0;

	if (has_cache)
		return cache_ret;

	char *par[64];
	int i = 0;
	char buf[PATH_MAX];

	// tool
	par[i++] = "/multiboot/busybox";
	par[i++] = "sh";
	par[i++] = "/multiboot/check_sdcard_context.sh";

	// args
	//par[i++] = get_multiboot_dir();
	par[i++] = SOURCE_MOUNT_DATA;
	if (system_is_recovery()) {
		par[i++] = FILE_VOLD_RECOVERY;
	} else {
		sprintf(buf, "%s/%s", get_multiboot_dir(), FILE_VOLD_ANDROID);
		par[i++] = buf;
	}
	par[i++] = CONTEXT_ID_FILE;

	// end
	par[i++] = (char *)0;

	do_exec(par);

	struct stat sb;
	cache_ret = !stat("/multiboot/.needs_context", &sb);
	has_cache = 1;
	return cache_ret;
}

void kperror(const char *message)
{
	const char *sep = ": ";
	if (!message) {
		sep = "";
		message = "";
	}

	KLOG_ERROR(LOG_TAG, "%s%s%s", message, sep, strerror(errno));
}

char *get_source_device(void)
{
	return source_device_path;
}

char *get_multiboot_dir(void)
{
	return multiboot_dir;
}

struct fstab *get_multiboot_fstab(void)
{
	return mbfstab;
}

struct fstab_rec *get_fstab_rec(const char *devname)
{
	int i;
	struct stat sb;

	bool use_stat = !stat(devname, &sb);

	for (i = 0; i < mbfstab->num_entries; i++) {
		if (use_stat && sb.st_rdev == mbfstab->recs[i].statbuf.st_rdev) {
			KLOG_DEBUG(LOG_TAG, "%s: stat: %d\n", __func__, i);
			return &mbfstab->recs[i];
		}
		if (!strcmp(devname, mbfstab->recs[i].blk_device)) {
			KLOG_DEBUG(LOG_TAG, "%s: strcmp: %d\n", __func__, i);
			return &mbfstab->recs[i];
		}
	}

	KLOG_DEBUG(LOG_TAG, "%s: no match\n", __func__);
	return NULL;
}

void import_kernel_nv(char *name)
{
	char *value = strchr(name, '=');
	int name_len = strlen(name);

	if (value == 0)
		return;
	*value++ = 0;
	if (name_len == 0)
		return;

	if (!strcmp(name, "multiboot.source")) {
		if (sscanf
		    (value, "(hd%d,%d)%s", &source_device, &source_partition,
		     source_path) < 0) {
			kperror("scanf");
			return;
		}
		// get path to source partition
		sprintf(source_device_path, "/dev/block/mmcblk%dp%d",
			source_device, source_partition);
		KLOG_INFO(LOG_TAG, "source_device_path: %s\n",
			  source_device_path);

		// get multiboot directory
		sprintf(multiboot_dir, PATH_MOUNTPOINT_SOURCE "%s",
			source_path);
		KLOG_INFO(LOG_TAG, "multiboot_dir: %s\n", multiboot_dir);

		is_multiboot = 1;
	}

	if (!strcmp(name, "multiboot.2ndstage")) {
		if (sscanf(value, "%d", &is_sndstage) < 0) {
			kperror("scanf");
			return;
		}
	}
}

int common_early_init(void)
{
	int i;
	char buf[PATH_MAX];

	// read multiboot fstab
	mbfstab = fs_mgr_read_fstab(FILE_FSTAB);
	if (!mbfstab) {
		KLOG_ERROR(LOG_TAG, "failed to open %s\n", FILE_FSTAB);
		return -1;
	}
	// create mountpoint
	if (mkpath(PATH_MOUNTPOINT_SOURCE, S_IRWXU | S_IRWXG | S_IRWXO)) {
		kperror("mkpath");
		return -1;
	}
	// get replacement info
	for (i = 0; i < mbfstab->num_entries; i++) {

		// bind mount
		if (!strcmp(mbfstab->recs[i].fs_type, "ext4")) {
			// get directory
			sprintf(buf, PATH_MOUNTPOINT_SOURCE "%s%s",
				source_path, mbfstab->recs[i].mount_point);

			// set bind directory as device
			mbfstab->recs[i].replacement_device = strdup(buf);
			mbfstab->recs[i].replacement_bind = 1;
		}
		// fsimage mount
		else {
			// get device node
			sprintf(buf, "/dev/block/loop%d", 255 - i);

			// set loop dev as device
			mbfstab->recs[i].replacement_device = strdup(buf);
			mbfstab->recs[i].replacement_bind = 0;
		}
	}

	return 0;
}

int common_late_init(void)
{
	int i;
	char buf[PATH_MAX];
	struct stat sb;
	const char *mount_data = NULL;

	if (multiboot_initialized || !can_init())
		return 0;

	KLOG_INFO(LOG_TAG, "%s\n", __func__);

	// resolve symlinks in fstab paths
	translate_fstab_paths(mbfstab);

	// minivold is on ramdisk
	if (system_is_recovery() && needs_context()) {
		KLOG_ERROR(LOG_TAG, "minivold: mount with sdcard context\n");
		mount_data = SOURCE_MOUNT_DATA;
	}
	// mount source partition
	check_fs(source_device_path, "ext4", PATH_MOUNTPOINT_SOURCE);
	mount(source_device_path, PATH_MOUNTPOINT_SOURCE, "ext4", 0,
	      mount_data);

	// android's vold is on /system that's why we couldn't check it before mounting
	if (!system_is_recovery() && needs_context()) {
		KLOG_ERROR(LOG_TAG, "vold: remount with sdcard context\n");
		umount(PATH_MOUNTPOINT_SOURCE);
		mount(source_device_path, PATH_MOUNTPOINT_SOURCE, "ext4",
		      0, SOURCE_MOUNT_DATA);
	}
	// create main mb directory
	mkpath(buf, S_IRWXU | S_IRWXG | S_IRWXO);

	// setup filesystem
	for (i = 0; i < mbfstab->num_entries; i++) {
		if (mbfstab->recs[i].replacement_bind) {
			// create directory
			if (mkpath
			    (mbfstab->recs[i].replacement_device,
			     S_IRWXU | S_IRWXG | S_IRWXO)) {
				kperror("mkpath");
				continue;
			}
		}

		else {
			// create filesystem image
			sprintf(buf, PATH_MOUNTPOINT_SOURCE "%s%s.img",
				source_path, mbfstab->recs[i].mount_point);
			if (stat(buf, &sb)) {
				if (createRawImage
				    (mbfstab->recs[i].blk_device, buf)) {
					kperror("createRawImage");
					continue;
				}
			}
			// create device node
			mknod(mbfstab->recs[i].replacement_device,
			      mbfstab->recs[i].statbuf.st_mode, makedev(7,
									255
									- i));

			// setup loop
			if (set_loop
			    (mbfstab->recs[i].replacement_device, buf, 0))
				kperror("set_loop");
		}
	}

	multiboot_initialized = true;
	return 0;
}
