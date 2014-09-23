#include <common.h>

#define MAX_PARAMETERS 64
#define FILE_FSTAB "/multiboot/fstab"
#define FSTAB_PREFIX "fstab."
#define FSTAB_RECOVERY "/etc/recovery.fstab"
#define FILE_RECOVERY_BINARY "/sbin/recovery"

static struct module_data module_data;

static bool vold_only = false;
static bool late_init_done = false;

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
	char buf[PATH_MAX];
	int rc;

	if (value == 0)
		return;
	*value++ = 0;
	if (name_len == 0)
		return;

	if (!strcmp(name, "multiboot.source")) {
		unsigned int mmc_device, mmc_part;

		// get values
		if (sscanf
		    (value, "(hd%u,%u)%s", &mmc_device, &mmc_part, buf) != 3) {
			kperror("scanf(multiboot.source)");
			return;
		}
		module_data.multiboot_path = strdup(buf);

		// get path to source partition
		rc = snprintf(buf, sizeof(buf), "/dev/block/mmcblk%dp%d",
			      mmc_device, mmc_part);
		if (rc < 0 || rc >= (int)sizeof(buf)) {
			kperror("snprintf(multiboot.source)");
			free(module_data.multiboot_path);
			module_data.multiboot_path = NULL;
			return;
		}
		module_data.multiboot_device = strdup(buf);

		module_data.multiboot_enabled = true;
	}

	if (!strcmp(name, "multiboot.grubdir")) {
		unsigned int mmc_device, mmc_part;

		// get values
		if (sscanf
		    (value, "(hd%u,%u)%s", &mmc_device, &mmc_part, buf) != 3) {
			kperror("scanf(multiboot.grubdir)");
			return;
		}
		module_data.grub_path = strdup(buf);

		// get path to source partition
		snprintf(buf, sizeof(buf), "/dev/block/mmcblk%dp%d", mmc_device,
			 mmc_part);
		if (rc < 0 || rc >= (int)sizeof(buf)) {
			kperror("snprintf(multiboot.grubdir)");
			free(module_data.grub_path);
			module_data.grub_path = NULL;
			return;
		}
		module_data.grub_device = strdup(buf);
	}

	if (!strcmp(name, "multiboot.2ndstage")) {
		unsigned val;
		if (sscanf(value, "%u", &val) != 1) {
			kperror("scanf(multiboot.2ndstage)");
			return;
		}
		module_data.sndstage_enabled = ! !val;
	}
}

static int load_multiboot_fstab(void)
{
	int i;
	char buf[PATH_MAX];
	struct fstab *mbfstab;

	// read multiboot fstab
	module_data.multiboot_fstab = mbfstab = fs_mgr_read_fstab(FILE_FSTAB);
	if (!mbfstab) {
		KLOG_ERROR(LOG_TAG, "failed to load %s\n", FILE_FSTAB);
		return -1;
	}
	// get replacement info
	for (i = 0; i < mbfstab->num_entries; i++) {

		// bind mount
		// TODO add more filesystems
		if (!strcmp(mbfstab->recs[i].fs_type, "ext4")) {
			// get directory
			sprintf(buf, PATH_MOUNTPOINT_SOURCE "%s%s",
				module_data.multiboot_path,
				mbfstab->recs[i].mount_point);

			// set bind directory as device
			mbfstab->recs[i].replacement_device = strdup(buf);
			mbfstab->recs[i].replacement_bind = 1;
		}
		// fsimage mount
		else {
			// create loop device
			char *loopdev = make_loop(NULL);
			if (!loopdev) {
				return -1;
			}
			// set loop dev as device
			mbfstab->recs[i].replacement_device = loopdev;
			mbfstab->recs[i].replacement_bind = 0;
		}
	}

	return 0;
}

static int find_fstab(int (*callback) (const char *))
{
	struct stat sb;

	// recovery fstab
	if (module_data.bootmode == BOOTMODE_RECOVERY
	    && !stat(FSTAB_RECOVERY, &sb)) {
		if (callback && callback(FSTAB_RECOVERY))
			return -1;
	}
	// find fstabs in root dir
	DIR *d = opendir("/");
	if (!d) {
		kperror("opendir(/)");
		return -1;
	}

	struct dirent *dt;
	while ((dt = readdir(d))) {
		if (dt->d_type != DT_REG)
			continue;

		if (!strcmp(dt->d_name, "fstab.goldfish"))
			continue;

		if (strncmp(dt->d_name, FSTAB_PREFIX, sizeof(FSTAB_PREFIX) - 1)
		    == 0) {
			if (callback && callback(dt->d_name))
				return -1;
		}
	}

	if (closedir(d)) {
		kperror("closedir");
		return -1;
	}

	return 0;
}

static int add_fstab(const char *path)
{
	struct fstab *fstab = fs_mgr_read_fstab(path);
	if (!fstab) {
		KLOG_ERROR(LOG_TAG, "failed to load %s\n", path);
		fstab = do_fs_mgr_read_fstab(path, 1);
		if (!fstab) {
			KLOG_ERROR(LOG_TAG, "failed to load %s as twrp fstab\n",
				   path);
			return -1;
		}
	}
	// allocate memory
	module_data.target_fstabs =
	    realloc(module_data.target_fstabs,
		    ++module_data.target_fstabs_count);
	if (!module_data.target_fstabs) {
		kperror("realloc");
		return -1;
	}
	// add fstab
	module_data.target_fstabs[module_data.target_fstabs_count - 1] = fstab;

	return 0;
}

static int system_is_recovery(void)
{
	struct stat sb;

	return !stat(FILE_RECOVERY_BINARY, &sb);
}

static int can_init(void)
{
	struct stat sb;
	return !stat("/dev/block", &sb);
}

static int setup(void)
{
	int rc = 0;

	// init klog
	klog_init();
	klog_set_level(6);
	KLOG_INFO(LOG_TAG, "init\n");

	// get system type
	module_data.bootmode =
	    system_is_recovery()? BOOTMODE_RECOVERY : BOOTMODE_ANDROID;

	// mount private dev fs
	util_mount("tmpfs", PATH_MOUNTPOINT_DEV, "tmpfs", MS_NOSUID,
		   "mode=0755");

	// mount sysfs
	mkdir("/sys", 0755);
	mount("sysfs", "/sys", "sysfs", 0, NULL);

	module_data.block_info = get_block_devices();
	if (!module_data.block_info) {
		KLOG_ERROR(LOG_TAG, "Couldn't get block_info!\n");
		rc = -1;
		goto unmount_procfs;
	}
	// unmount sysfs
	umount("/sys");

	// mount procfs
	mkdir("/proc", 0755);
	mount("proc", "/proc", "proc", 0, NULL);

	// parse cmdline
	import_kernel_cmdline(import_kernel_nv);

	KLOG_INFO(LOG_TAG, "bootmode=%s\n", strbootmode(module_data.bootmode));
	KLOG_INFO(LOG_TAG, "multiboot_enabled=%d\n",
		  module_data.multiboot_enabled);
	KLOG_INFO(LOG_TAG, "sndstage_enabled=%d\n",
		  module_data.sndstage_enabled);
	KLOG_INFO(LOG_TAG, "multiboot_device=%s\n",
		  module_data.multiboot_device);
	KLOG_INFO(LOG_TAG, "multiboot_path=%s\n", module_data.multiboot_path);
	KLOG_INFO(LOG_TAG, "grub_device=%s\n", module_data.grub_device);
	KLOG_INFO(LOG_TAG, "grub_path=%s\n", module_data.grub_path);

	// we don't need any patching
	if (!module_data.multiboot_enabled && !module_data.sndstage_enabled) {
		goto unmount_procfs;
	}
	// get grub blockinfo
	if (module_data.grub_device && module_data.grub_path) {
		module_data.grub_blockinfo =
		    get_blockinfo_for_path(module_data.block_info,
					   module_data.grub_device);
		if (!module_data.grub_blockinfo) {
			rc = -1;
			goto unmount_procfs;
		}
	}
	// early init
	module_data.initstage = INITSTAGE_EARLY;
	if (modules_call_early_init(&module_data)) {
		rc = -1;
		goto unmount_procfs;
	}
	// load multiboot fstab
	if (load_multiboot_fstab()) {
		rc = -1;
		goto unmount_procfs;
	}
	// load target fstabs
	if (find_fstab(&add_fstab)) {
		rc = -1;
		goto unmount_procfs;
	}
	// fstab init
	module_data.initstage = INITSTAGE_FSTAB;
	if (modules_call_fstab_init(&module_data)) {
		rc = -1;
		KLOG_INFO(LOG_TAG, "error in fstab_init!\n");
		goto unmount_procfs;
	}

unmount_procfs:
	// unmount procfs
	umount("/proc");

	return rc;
}

static int translate_fstab_paths(struct fstab *fstab)
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

	return 0;
}

static int prepare_fstab(void)
{
	unsigned i;

	if (translate_fstab_paths(module_data.multiboot_fstab))
		return -1;

	for (i = 0; i < module_data.target_fstabs_count; i++) {
		if (translate_fstab_paths(module_data.target_fstabs[i]))
			return -1;
	}

	return 0;
}

static int hook_mount(struct tracy_event *e)
{
	int rc = TRACY_HOOK_CONTINUE;

	// check if we can init now
	if (!late_init_done && can_init()) {
		// fill fstabs with more info
		if (prepare_fstab()) {
			rc = TRACY_HOOK_ABORT;
			goto finish;
		}
		// late init
		module_data.initstage = INITSTAGE_LATE;
		if (modules_call_late_init(&module_data)) {
			KLOG_ERROR(LOG_TAG, "error in late_init!\n");
			rc = TRACY_HOOK_ABORT;
			goto finish;
		}
		late_init_done = true;

		// WORKAROUND: we got the context-requirement via a shellscript
		//             and can stop tracing completely now.
		if (vold_only) {
			rc = TRACY_HOOK_STOP;
			goto finish;
		}

		module_data.initstage = INITSTAGE_HOOK;
	}
	// not ready yet
	if (!late_init_done)
		goto finish;

	// hook_mount
	rc = modules_call_hook_mount(&module_data, e);

finish:
	return rc;
}

int main(void)
{
	struct tracy *tracy;
	long tracy_opt = TRACY_MEMORY_FALLBACK;

	// clear module_data    
	memset(&module_data, 0, sizeof(module_data));
	module_data.initstage = INITSTAGE_NONE;

	// early init
	if (setup()) {
		KLOG_INFO(LOG_TAG, "error in setup()!\n");
		return EXIT_FAILURE;
	}
	// run /init directly without any patching or tracing
	if (!module_data.multiboot_enabled && !module_data.sndstage_enabled) {
		KLOG_INFO(LOG_TAG, "multiboot disabled. run /init ...\n");
		if (run_init(NULL)) {
			kperror("run_init");
			return EXIT_FAILURE;
		}
		return 0;
	}
	// !MULTIBOOT && !2NDSTAGE IS IMPOSSIBLE FROM HERE

	// tracy init
	if (module_data.bootmode == BOOTMODE_RECOVERY)
		tracy_opt |= TRACY_TRACE_CHILDREN;
	module_data.tracy = tracy = tracy_init(tracy_opt);

	// hook mount
	if (tracy_set_hook(tracy, "mount", TRACY_ABI_NATIVE, hook_mount)) {
		KLOG_ERROR(LOG_TAG, "Could not hook mount\n");
		return EXIT_FAILURE;
	}
	// RECOVERY
	if (module_data.bootmode == BOOTMODE_RECOVERY) {
		// multiboot-recovery gets full redirection
		if (module_data.multiboot_enabled) {
			module_data.initstage = INITSTAGE_TRACY;
			// tracy init
			if (modules_call_tracy_init(&module_data)) {
				KLOG_ERROR(LOG_TAG, "error in tracy_init!\n");
				return EXIT_FAILURE;
			}
		} else if (module_data.sndstage_enabled) {
			vold_only = true;
		}
		// else is impossible
	}
	// ANDROID
	else {
		vold_only = true;
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

	// wait for all childs to finish - that hopefully will never happen
	KLOG_ERROR(LOG_TAG, "TRACY EXIT. waiting now...\n");
	while (waitpid(-1, NULL, 0)) {
		if (errno == ECHILD) {
			break;
		}
	}

	return EXIT_SUCCESS;
}
