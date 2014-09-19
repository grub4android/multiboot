#include <tracy.h>
#include <ll.h>
#include <stdbool.h>
#include <sys/mount.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>

#include <lib/fs_mgr.h>
#include <lib/klog.h>
#include <util.h>

#include <mb_common.h>
#include <mb_fs_redirection.h>
#include <mb_fstab_patcher.h>

#define FSTAB_PREFIX "fstab."
#define FSTAB_RECOVERY "/etc/recovery.fstab"
#define FS_OPTION_REMOUNT "remount,"

static void find_fstab(int (*callback) (const char *))
{
	struct stat sb;

	// hardcoded paths
	if (!stat(FSTAB_RECOVERY, &sb)) {
		if (callback(FSTAB_RECOVERY))
			return;
	}
	// find fstabs in root dir
	DIR *d = opendir("/");
	if (!d) {
		KLOG_ERROR("Failed to open /\n");
		return;
	}

	struct dirent *dt;
	while ((dt = readdir(d))) {
		if (dt->d_type != DT_REG)
			continue;

		if (!strcmp(dt->d_name, "fstab.goldfish"))
			continue;

		if (strncmp(dt->d_name, FSTAB_PREFIX, sizeof(FSTAB_PREFIX) - 1)
		    == 0) {
			if (callback(dt->d_name))
				return;
		}
	}

	closedir(d);
}

static int patch_fstab(const char *path)
{
	KLOG_ERROR(LOG_TAG, "%s: %s\n", __func__, path);
	int i, j;

	// parse original fstab
	struct fstab *fstab_orig = fs_mgr_read_fstab(path);
	struct fstab *mbfstab = get_multiboot_fstab();

	// open fstab for writing
	FILE *f = fopen(path, "w");
	if (!f) {
		KLOG_ERROR(LOG_TAG, "Error opening fstab!\n");
		goto out_free_fstab;
	}
	// write new fstab
	for (i = 0; i < fstab_orig->num_entries; i++) {
		const char *blk_device = fstab_orig->recs[i].blk_device;
		const char *mount_point = fstab_orig->recs[i].mount_point;
		const char *fs_type = fstab_orig->recs[i].fs_type;
		const char *fs_options =
		    fstab_orig->recs[i].fs_options_unparsed;
		const char *fs_mgr_flags =
		    fstab_orig->recs[i].fs_mgr_flags_unparsed;

		// lookup partition is replacement fstab
		bool use_bind = false;
		for (j = 0; j < mbfstab->num_entries; j++) {
			if (strcmp(mount_point, mbfstab->recs[j].mount_point))
				continue;

			// bind mount
			if (mbfstab->recs[j].replacement_bind) {
				if (!system_is_recovery()) {
					// set new args
					blk_device =
					    mbfstab->recs[j].replacement_device;
					fs_options = "bind";

					use_bind = true;
				} else {
					// to fix format in recovery
					fs_type = "multiboot";
				}
			}
			// fsimage mount
			else if (!system_is_recovery()) {
				blk_device =
				    mbfstab->recs[j].replacement_device;
			}

			break;
		}

		// write new entry
		fprintf(f, "%s %s %s %s %s\n", blk_device, mount_point, fs_type,
			fs_options, fs_mgr_flags);

		// we need a remount for bind mounts to set the flags
		if (use_bind && strcmp(mount_point, "/system")) {
			const char *fs_options =
			    fstab_orig->recs[i].fs_options_unparsed;
			char *new_fs_options = NULL;

			// create remount options
			new_fs_options =
			    malloc(strlen(FS_OPTION_REMOUNT) +
				   strlen(fs_options) + 1);
			sprintf(new_fs_options, "%s%s", FS_OPTION_REMOUNT,
				fs_options);

			// add remount entry
			fprintf(f, "%s %s %s %s %s\n", blk_device, mount_point,
				fs_type, new_fs_options, "wait");

			// cleanup
			free(new_fs_options);
		}
	}
// cleanup
	fclose(f);
out_free_fstab:
	fs_mgr_free_fstab(fstab_orig);

	return 0;
}

int fstabpatcher_early_init(void)
{
	// prepare fstab
	find_fstab(&patch_fstab);

	return 0;
}
