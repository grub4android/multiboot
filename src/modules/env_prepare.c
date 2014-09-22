#include <common.h>

#define FILE_VOLD_ANDROID "/system/bin/vold"
#define FILE_VOLD_RECOVERY "/sbin/minivold"
#define SOURCE_MOUNT_DATA "context=u:object_r:sdcard_external:s0"
#define CONTEXT_ID_FILE "/multiboot/.needs_context"

static int mount_grub_partition(struct module_data *data)
{
	int i = 0;
	int ret = -1;
	struct stat sb_grub_part;
	struct fstab *fstab;

	// get target fstab
	if (data->target_fstabs_count < 1) {
		KLOG_ERROR(LOG_TAG, "%s: no fstab\n", __func__);
		return ret;
	}
	fstab = data->target_fstabs[0];

	// stat grub partition
	if (stat(data->grub_device, &sb_grub_part)) {
		KLOG_ERROR(LOG_TAG, "%s: couldn't stat grubdev\n", __func__);
		return ret;
	}

	for (i = 0; i < fstab->num_entries; i++) {
		// compare both partitions
		if (sb_grub_part.st_rdev != fstab->recs[i].statbuf.st_rdev)
			continue;

		// First check the filesystem if requested
		if (fs_mgr_is_wait(&fstab->recs[i])) {
			check_fs(fstab->recs[i].blk_device,
				 fstab->recs[i].fs_type,
				 fstab->recs[i].mount_point);
		}
		// mount it to /multiboot/grub now
		if (util_mount
		    (fstab->recs[i].blk_device, PATH_MOUNTPOINT_GRUB,
		     fstab->recs[i].fs_type, fstab->recs[i].flags,
		     fstab->recs[i].fs_options)) {
			KLOG_ERROR(LOG_TAG,
				   "Cannot mount filesystem on %s at %s options: %s error: %s\n",
				   fstab->recs[i].blk_device,
				   PATH_MOUNTPOINT_GRUB,
				   fstab->recs[i].fs_options, strerror(errno));
			continue;
		} else {
			ret = 0;
			goto out;
		}
	}

	// TODO: search for it in multiboot fstab

	// We didn't find a match, say so and return an error
	KLOG_ERROR(LOG_TAG, "Cannot find mount point %s in fstab\n",
		   fstab->recs[i].mount_point);

out:
	return ret;
}

int needs_context(struct module_data *data)
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
	par[i++] = SOURCE_MOUNT_DATA;
	if (data->bootmode == BOOTMODE_RECOVERY) {
		par[i++] = FILE_VOLD_RECOVERY;
	} else {
		sprintf(buf, PATH_MOUNTPOINT_SOURCE "%s/%s",
			data->multiboot_path, FILE_VOLD_ANDROID);
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

static int sndstage_setup_partition(struct module_data *data, const char *name)
{
	char buf[PATH_MAX];
	struct stat sb;

	struct fstab_rec *rec =
	    fs_mgr_get_entry_for_mount_point(data->multiboot_fstab, name);
	if (rec == NULL) {
		KLOG_ERROR(LOG_TAG, "Couldn't find %s in mbfstab\n", name);
		return -1;
	}
	// create filesystem image
	sprintf(buf, PATH_MOUNTPOINT_GRUB "%s/..%s.img", data->grub_path,
		rec->mount_point);
	if (stat(buf, &sb)) {
		if (createRawImage(rec->blk_device, buf)) {
			kperror("createRawImage");
			return -1;
		}
	}
	// delete original node
	if (unlink(rec->blk_device)) {
		KLOG_ERROR(LOG_TAG, "Couldn't delete %s!\n", rec->blk_device);
		return -1;
	}
	// create loop device node
	char *loopdev = make_loop(rec->blk_device);
	if (!loopdev) {
		kperror("make_loop");
		return -1;
	}
	// setup loop
	if (set_loop(rec->blk_device, buf, 0))
		kperror("set_loop");
	return 0;
}

static int ep_late_init(struct module_data *data)
{
	int i, rc = 0;
	char buf[PATH_MAX];
	struct stat sb;
	const char *mount_data = NULL;

	KLOG_INFO(LOG_TAG, "%s\n", __func__);

	// mount grub part for bootrec redirection
	if (data->sndstage_enabled) {
		// mount grub_dir
		if (data->grub_device && data->grub_path) {
			if (mount_grub_partition(data)) {
				KLOG_ERROR(LOG_TAG, "couldn't mount grubdev\n");
				rc = -1;
				goto finish;
			}
			// create grub directory
			sprintf(buf, PATH_MOUNTPOINT_GRUB "%s",
				data->grub_path);
			if (mkpath(buf, S_IRWXU | S_IRWXG | S_IRWXO)) {
				kperror("mkpath");
			}
			// prepare partition images
			if (!data->multiboot_enabled
			    && sndstage_setup_partition(data, "/boot")) {
				rc = -1;
				goto finish;
			}
			if (sndstage_setup_partition(data, "/recovery")) {
				rc = -1;
				goto finish;
			}
		}

		if (!data->multiboot_enabled) {
			KLOG_ERROR(LOG_TAG,
				   "this isn't multiboot - stop here.\n");
			goto finish;
		}
	}
	// minivold is on ramdisk
	if (data->bootmode == BOOTMODE_RECOVERY && needs_context(data)) {
		KLOG_ERROR(LOG_TAG, "minivold: mount with sdcard context\n");
		mount_data = SOURCE_MOUNT_DATA;
	}
	// mount source partition
	// TODO use libblkid to detect the filesystem
	check_fs(data->multiboot_device, "ext4", PATH_MOUNTPOINT_SOURCE);
	if (util_mount
	    (data->multiboot_device, PATH_MOUNTPOINT_SOURCE, "ext4", 0,
	     mount_data)) {
		kperror("mount(multiboot_device)");
		rc = -1;
		goto finish;
	}
	// android's vold is on /system that's why we couldn't check it before mounting
	if (data->bootmode != BOOTMODE_RECOVERY && needs_context(data)) {
		KLOG_ERROR(LOG_TAG, "vold: remount with sdcard context\n");
		umount(PATH_MOUNTPOINT_SOURCE);
		if (mount
		    (data->multiboot_device, PATH_MOUNTPOINT_SOURCE, "ext4", 0,
		     SOURCE_MOUNT_DATA)) {
			kperror("mount(multiboot_device, context)");
			rc = -1;
			goto finish;
		}
	}
	// create main mb directory
	sprintf(buf, PATH_MOUNTPOINT_SOURCE "%s", data->multiboot_path);
	if (mkpath(buf, S_IRWXU | S_IRWXG | S_IRWXO)) {
		kperror("mkpath");
		rc = -1;
		goto finish;
	}
	// setup filesystem
	struct fstab *mbfstab = data->multiboot_fstab;
	for (i = 0; i < mbfstab->num_entries; i++) {
		if (!fs_mgr_is_multiboot(&mbfstab->recs[i]))
			continue;

		if (mbfstab->recs[i].replacement_bind) {
			// create directory
			if (mkpath
			    (mbfstab->recs[i].replacement_device,
			     S_IRWXU | S_IRWXG | S_IRWXO)) {
				kperror("mkpath");
				rc = -1;
				goto finish;
			}
		}

		else {
			// create filesystem image
			sprintf(buf, PATH_MOUNTPOINT_SOURCE "%s%s.img",
				data->multiboot_path,
				mbfstab->recs[i].mount_point);
			if (stat(buf, &sb)) {
				if (createRawImage
				    (mbfstab->recs[i].blk_device, buf)) {
					kperror("createRawImage");
					rc = -1;
					goto finish;
				}
			}
			// setup loop
			if (set_loop
			    (mbfstab->recs[i].replacement_device, buf, 0))
				kperror("set_loop");
		}
	}

finish:
	return rc;
}

static struct module module_env_prepare = {
	.late_init = ep_late_init,
};

module_add(module_env_prepare);
