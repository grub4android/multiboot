#include <common.h>

/*
 * 1) mount grub root (if we don't use a ramdisk)
 * 2) setup /multiboot
 * 3) mount source partition
 */
static int ep_early_init(struct module_data *data)
{
	int rc = 0;
	char part_grub[PATH_MAX];
	char part_source[PATH_MAX];

	// mount grub device
	if (data->grub_device.blk_device != NULL && data->grub_path != NULL) {
		// mount grub partition
		snprintf(part_grub, sizeof(part_grub), "/multiboot%s",
			 data->grub_device.blk_device);
		if (util_mount(part_grub, PATH_MOUNTPOINT_GRUB, NULL, 0, NULL)) {
			kperror("mount(grub_device)");
			rc = -1;
			goto finish;
		}
		// bind mount subfolder
		snprintf(part_grub, sizeof(part_grub),
			 PATH_MOUNTPOINT_GRUB "/%s/..", data->grub_path);
		if (util_mount
		    (part_grub, PATH_MOUNTPOINT_BOOTLOADER, NULL, MS_BIND,
		     NULL)) {
			kperror("mount(grub_device|bind)");
			rc = -1;
			goto finish;
		}
	}
	// create local copy of multiboot files
	// the original filesystem could be mounted with noexec
	util_copy(PATH_MOUNTPOINT_BOOTLOADER "/multiboot", "/", true, true);
	chmod(PATH_MULTIBOOT_BUSYBOX, 0700);	// in case we lost permission

	// update to more secure permissions
	util_chmod("/multiboot/etc", "0600", true);
	util_chmod("/multiboot/sbin", "0700", true);

	// convert source part
	snprintf(part_source, sizeof(part_source), "/multiboot%s",
		 data->multiboot_device.blk_device);

	// mount source partition
	if (util_mount(part_source, PATH_MOUNTPOINT_SOURCE, NULL, 0, NULL)) {
		kperror("mount(multiboot_device)");
		rc = -1;
		goto finish;
	}
	// setup voldwrapper
	if (data->bootmode != BOOTMODE_RECOVERY)
		patch_vold();

finish:
	return rc;
}

/*
 * setup multiboot diretories and images in case they don't exist
 */
static int ep_fstab_init(struct module_data *data)
{
	int i, rc = 0;
	char part_source[PATH_MAX];
	char buf[PATH_MAX];
	struct stat sb;

	// create main mb directory
	snprintf(buf, ARRAY_SIZE(buf), PATH_MOUNTPOINT_SOURCE "%s",
		 data->multiboot_path);
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
			// create stub filesystem image
			snprintf(buf, ARRAY_SIZE(buf),
				 PATH_MOUNTPOINT_STUBFS "%s.img",
				 mbfstab->recs[i].mount_point);
			if (stat(buf, &sb)) {
				if (createRawImage(NULL, buf, 2048 * 5)) {	// 5MB
					kperror("createRawImage");
					rc = -1;
					goto finish;
				}
			}
			// setup loop
			if (set_loop(mbfstab->recs[i].stub_device, buf, 0))
				kperror("set_loop");

			// format
			// TODO: detect filesystem and use correct mkfs tool
			if (make_ext4fs(mbfstab->recs[i].stub_device))
				kperror("make_ext4fs");
		}

		else {
			// create filesystem image
			snprintf(buf, ARRAY_SIZE(buf),
				 PATH_MOUNTPOINT_SOURCE "%s%s.img",
				 data->multiboot_path,
				 mbfstab->recs[i].mount_point);
			if (stat(buf, &sb)) {
				// convert source part
				snprintf(part_source, sizeof(part_source),
					 "/multiboot%s",
					 mbfstab->recs[i].blk_device);

				if (createRawImage(part_source, buf, ULONG_MAX)) {
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
	.early_init = ep_early_init,
	.fstab_init = ep_fstab_init,
};

module_add(module_env_prepare);
