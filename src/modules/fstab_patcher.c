#include <common.h>

#define FS_OPTION_REMOUNT "remount,"

static int file_write_fstabrec(FILE * f, bool twrp, const char *blk_device,
			       const char *mount_point, const char *fs_type,
			       const char *fs_options, const char *fs_mgr_flags,
			       const char *unhandled)
{
	if (twrp) {
		if (!strcmp(fs_options, "defaults"))
			fs_options = "";
		if (!strcmp(fs_mgr_flags, "defaults"))
			fs_mgr_flags = "";
		return fprintf(f, "%s %s %s %s %s %s\n", mount_point, fs_type,
			       blk_device, fs_options, fs_mgr_flags, unhandled);
	} else {
		return fprintf(f, "%s %s %s %s %s\n", blk_device, mount_point,
			       fs_type, fs_options, fs_mgr_flags);
	}
}

static int patch_fstab(struct module_data *data, int index)
{
	int i;
	struct fstab_rec *rec;

	struct fstab *fstab_orig = data->target_fstabs[index];
	struct fstab *mbfstab = data->multiboot_fstab;

	// open fstab for writing
	FILE *f = fopen(fstab_orig->fstab_filename, "w");
	if (!f) {
		KLOG_ERROR(LOG_TAG, "Error opening fstab!\n");
		return -1;
	}
	// write new fstab
	for (i = 0; i < fstab_orig->num_entries; i++) {
		const char *blk_device = fstab_orig->recs[i].blk_device;
		const char *mount_point = fstab_orig->recs[i].mount_point;
		const char *fs_type = fstab_orig->recs[i].fs_type;
		const char *fs_options =
		    fstab_orig->recs[i].fs_options_unparsed ? : "";
		const char *fs_mgr_flags =
		    fstab_orig->recs[i].fs_mgr_flags_unparsed ? : "";
		const char *unhandled_columns =
		    fstab_orig->recs[i].unhandled_columns ? : "";

		// lookup partition in multiboot fstab
		bool use_bind = false;

		// fixup for boot and recovery partitions on 2ndstage devices
		if (data->sndstage_enabled && data->grub_device
		    && data->grub_path) {
			struct sys_block_uevent *event =
			    get_blockinfo_for_path(data->block_info,
						   blk_device);
			if (!event) {
				KLOG_WARNING(LOG_TAG,
					     "Couldn't find event_info for path %s!\n",
					     blk_device);
			} else if (event->major ==
				   data->grub_blockinfo->major
				   && event->minor ==
				   data->grub_blockinfo->minor) {
				blk_device = PATH_MOUNTPOINT_GRUB;
				// TODO TWRP seems to ignore this
				if (fstab_orig->twrp)
					fs_options = "flags=fsflags=\"bind\"";
				else
					fs_options = "bind";
			}
		}

		if (data->multiboot_enabled) {
			rec =
			    fs_mgr_get_entry_for_mount_point(mbfstab,
							     mount_point);
			if (rec && fs_mgr_is_multiboot(rec)) {
				// bind mount
				if (rec->replacement_bind) {
					if (data->bootmode == BOOTMODE_RECOVERY) {
						// to fix format in recovery
						// TODO TWRP doesn't like this
						fs_type = "multiboot";

					} else {
						// set new args
						blk_device =
						    rec->replacement_device;
						fs_options = "bind";

						use_bind = true;
					}
				}
				// fsimage mount
				else if (data->bootmode != BOOTMODE_RECOVERY) {
					blk_device = rec->replacement_device;
				}
			}
		}
		// write new entry
		file_write_fstabrec(f, fstab_orig->twrp, blk_device,
				    mount_point, fs_type, fs_options,
				    fs_mgr_flags, unhandled_columns);

		// we need a remount for bind mounts to set the flags
		if (use_bind && strcmp(mount_point, "/system")) {
			const char *local_fs_options =
			    fstab_orig->recs[i].fs_options_unparsed;
			char *new_fs_options = NULL;

			// create remount options
			new_fs_options =
			    malloc(strlen(FS_OPTION_REMOUNT) +
				   strlen(local_fs_options) + 1);
			sprintf(new_fs_options, "%s%s", FS_OPTION_REMOUNT,
				local_fs_options);

			// add remount entry
			file_write_fstabrec(f, fstab_orig->twrp, blk_device,
					    mount_point, fs_type,
					    new_fs_options, "wait",
					    unhandled_columns);

			// cleanup
			free(new_fs_options);
		}
	}

	fclose(f);
	return 0;
}

static int fp_fstab_init(struct module_data *data)
{
	unsigned i;

	KLOG_INFO(LOG_TAG, "patch fstabs...\n");
	for (i = 0; i < data->target_fstabs_count; i++) {
		if (patch_fstab(data, i))
			return -1;
	}

	return 0;
}

static struct module module_fstab_patcher = {
	.fstab_init = fp_fstab_init,
};

module_add(module_fstab_patcher);
