#include <common.h>

extern int ext2_pre(struct fd_info *fdi);
extern bool ext2_was_format(struct fd_info *fdi);
extern int ext2_cleanup(struct fd_info *fdi);

int fs_pre(struct fd_info *fdi)
{
	if (!strcmp(fdi->fs_type, "ext4"))
		return ext2_pre(fdi);

	ERROR("%s: unhandled fstype %s\n", __func__, fdi->fs_type);
	return 0;
}

bool fs_was_format(struct fd_info * fdi)
{
	const char *fstype = get_fstype(fdi->device);
	if (!fstype)
		fstype = "ext4";

	// filesystem type has changed
	if (strcmp(fdi->fs_type, fstype)) {
		WARNING("%s: fstype changed from %s to %s\n", __func__,
			fdi->fs_type, fstype);
		return true;
	}

	if (!strcmp(fstype, "ext4"))
		return ext2_was_format(fdi);

	ERROR("%s: unhandled fstype %s\n", __func__, fstype);
	return false;
}

int fs_cleanup(struct fd_info *fdi)
{
	if (!strcmp(fdi->fs_type, "ext4"))
		return ext2_cleanup(fdi);

	ERROR("%s: unhandled fstype %s\n", __func__, fdi->fs_type);
	return 0;
}
