#include <common.h>

#define MAX_PARAMETERS 64
#define FILE_FSTAB "/bootloader/multiboot/etc/fstab"
#define FSTAB_PREFIX "fstab."
#define FSTAB_RECOVERY "/etc/recovery.fstab"
#define FILE_RECOVERY_BINARY "/sbin/recovery"

static struct module_data module_data;

static int prepare_fstab(void);

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
		module_data.multiboot_device.blk_device = strdup(buf);
		module_data.multiboot_device.mount_point =
		    PATH_MOUNTPOINT_SOURCE;

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

		module_data.grub_device.blk_device = strdup(buf);
		module_data.grub_device.mount_point = PATH_MOUNTPOINT_GRUB;
	}

	if (!strcmp(name, "multiboot.2ndstage")) {
		unsigned val;
		if (sscanf(value, "%u", &val) != 1) {
			kperror("scanf(multiboot.2ndstage)");
			return;
		}
		module_data.sndstage_enabled = ! !val;
	}

	if (!strcmp(name, "multiboot.debug")) {
		unsigned val;
		if (sscanf(value, "%u", &val) != 1) {
			kperror("scanf(multiboot.debug)");
			return;
		}
		klog_set_level(val);
	}

	if (!strcmp(name, "androidboot.hardware")) {
		module_data.hw_name = strdup(value);
	}

	if (!strcmp(name, "multiboot.ums")) {
		INFO("UMS: %s\n", value);
		umount("/proc");

		// create script
		create_ums_script("/sbin/ums.sh", value);
		chmod("/sbin/ums.sh", 0700);

		// patch init.rc
		sed_replace("/init.rc",
			    "s/\\/sbin\\/recovery/\\/sbin\\/ums.sh/g");

		// run init
		if (run_init(NULL)) {
			kperror("run_init");
			exit(1);
		}
		return;
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
		ERROR("failed to load %s\n", FILE_FSTAB);
		return -1;
	}
	// get replacement info
	for (i = 0; i < mbfstab->num_entries; i++) {

		// bind mount
		if (!strcmp(mbfstab->recs[i].fs_type, "ext2")
		    || !strcmp(mbfstab->recs[i].fs_type, "ext3")
		    || !strcmp(mbfstab->recs[i].fs_type, "ext4")
		    || !strcmp(mbfstab->recs[i].fs_type, "f2fs")) {
			// get directory
			snprintf(buf, ARRAY_SIZE(buf),
				 PATH_MOUNTPOINT_SOURCE "%s%s",
				 module_data.multiboot_path,
				 mbfstab->recs[i].mount_point);

			// create loop device
			char *loopdev = make_loop(NULL);
			if (!loopdev) {
				return -1;
			}
			// set bind directory as device
			mbfstab->recs[i].replacement_device = strdup(buf);	// bind source
			mbfstab->recs[i].stub_device = loopdev;
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
			mbfstab->recs[i].replacement_device = loopdev;	// mount device
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
		ERROR("failed to load %s\n", path);
		fstab = do_fs_mgr_read_fstab(path, 1);
		if (!fstab) {
			ERROR("failed to load %s as twrp fstab\n", path);
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

static int setup(void)
{
	int rc = 0;

	// init klog
	klog_init();
	klog_set_level(5);	// NOTICE
	INFO("%s\n", __func__);

	// get system type
	module_data.bootmode =
	    system_is_recovery()? BOOTMODE_RECOVERY : BOOTMODE_ANDROID;

	// mount private dev fs
	util_mount("tmpfs", PATH_MOUNTPOINT_DEV, "tmpfs", MS_NOSUID,
		   "mode=0755");

	// basic /dev nodes
	if (mknod(PATH_MOUNTPOINT_DEV "/zero", S_IFCHR | 0666, makedev(1, 5))) {
		kperror("mknod");
		return -1;
	}
	// make stubfs dir
	if (mkpath(PATH_MOUNTPOINT_STUBFS, S_IRWXU | S_IRWXG | S_IRWXO)) {
		kperror("mkpath");
		return -1;
	}
	// mount procfs
	mkdir("/proc", 0755);
	mount("proc", "/proc", "proc", 0, NULL);

	// parse cmdline
	import_kernel_cmdline(import_kernel_nv);

	INFO("bootmode=%s\n", strbootmode(module_data.bootmode));
	INFO("multiboot_enabled=%d\n", module_data.multiboot_enabled);
	INFO("sndstage_enabled=%d\n", module_data.sndstage_enabled);
	INFO("multiboot_device=%s\n", module_data.multiboot_device.blk_device);
	INFO("multiboot_path=%s\n", module_data.multiboot_path);
	INFO("grub_device=%s\n", module_data.grub_device.blk_device);
	INFO("grub_path=%s\n", module_data.grub_path);

	// we don't need any patching or tracing
	if (!module_data.multiboot_enabled && !module_data.sndstage_enabled) {
		goto unmount_procfs;
	}
	// !MULTIBOOT && !2NDSTAGE IS IMPOSSIBLE FROM HERE

	// mount sysfs
	mkdir("/sys", 0755);
	mount("sysfs", "/sys", "sysfs", 0, NULL);

	module_data.block_info = get_block_devices();
	if (!module_data.block_info) {
		ERROR("Couldn't get block_info!\n");
		rc = -1;
		goto unmount_procfs;
	}
	// unmount sysfs
	umount("/sys");

	// setup /multiboot/dev/block
	uevent_create_nodes(module_data.block_info, PATH_MOUNTPOINT_DEV);

	// get grub blockinfo
	if (module_data.grub_device.blk_device && module_data.grub_path) {
		module_data.grub_blockinfo =
		    get_blockinfo_for_path(module_data.block_info,
					   module_data.grub_device.blk_device);
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
	// fill fstabs with more info
	if (prepare_fstab()) {
		rc = -1;
		goto unmount_procfs;
	}
	// fstab init
	module_data.initstage = INITSTAGE_FSTAB;
	if (modules_call_fstab_init(&module_data)) {
		ERROR("error in fstab_init!\n");
		rc = -1;
		goto unmount_procfs;
	}

unmount_procfs:
	// unmount procfs
	umount("/proc");

	// mount private procfs for tracy
	mkdir("/multiboot/proc", 0755);
	mount("proc", "/multiboot/proc", "proc", 0, NULL);

	return rc;
}

static int translate_fstab_rec(struct fstab_rec *rec)
{
	char devname_real[PATH_MAX + 1];

	if (uevent_realpath
	    (module_data.block_info, rec->blk_device, devname_real) != NULL) {
		free(rec->blk_device);
		rec->blk_device = strdup(devname_real);
	}

	if (uevent_stat
	    (module_data.block_info, rec->blk_device, &rec->statbuf) < 0)
		WARNING("%s: couldn't stat %s\n", __func__, rec->blk_device);
	else {
		dev_t dev = rec->statbuf.st_rdev;
		DEBUG("%s: %s: major=%u minor=%u\n",
		      __func__, rec->blk_device, major(dev), minor(dev));
	}

	return 0;
}

static int translate_fstab_paths(struct fstab *fstab)
{
	int i;

	for (i = 0; i < fstab->num_entries; i++) {
		translate_fstab_rec(&fstab->recs[i]);
	}

	return 0;
}

static int prepare_fstab(void)
{
	unsigned i;

	if (module_data.multiboot_path) {
		// source device
		translate_fstab_rec(&module_data.multiboot_device);
		module_data.multiboot_device.replacement_bind = 1;
		module_data.multiboot_device.replacement_device =
		    strdup(PATH_MOUNTPOINT_SOURCE);
		module_data.multiboot_device.stub_device =
		    strdup(module_data.multiboot_device.blk_device);
	}

	if (module_data.grub_path) {
		// grub device
		translate_fstab_rec(&module_data.grub_device);
		module_data.grub_device.replacement_bind = 1;
		module_data.grub_device.replacement_device =
		    strdup(PATH_MOUNTPOINT_GRUB);
		module_data.grub_device.stub_device =
		    strdup(module_data.grub_device.blk_device);
	}
	// multiboot fstab
	if (translate_fstab_paths(module_data.multiboot_fstab))
		return -1;

	// ROM fstabs
	for (i = 0; i < module_data.target_fstabs_count; i++) {
		if (translate_fstab_paths(module_data.target_fstabs[i]))
			return -1;
	}

	return 0;
}

static void multiboot_child_create(struct tracy_child *child)
{
	DEBUG("%s: %d\n", __func__, child->pid);

	// just in case we have the data already
	if (child->custom)
		return;

	// allocate
	child->custom = malloc(sizeof(struct multiboot_child_data));
	if (!child->custom) {
		ERROR("Couldn't allocate custom child mem!\n");
	}
	// initialize
	struct multiboot_child_data *mbc = child->custom;
	memset(child->custom, 0, sizeof(mbc[0]));

	// tracy_child_create
	if (modules_call_tracy_child_create(&module_data, child)) {
		ERROR("Error in tracy_child_create!\n");
		tracy_quit(module_data.tracy, 1);
	}
}

static void multiboot_child_destroy(struct tracy_child *child)
{
	DEBUG("%s: %d\n", __func__, child->pid);

	// nothing to do here
	if (!child->custom)
		return;

	struct multiboot_child_data *mbc = child->custom;

	// tracy_child_destroy
	if (modules_call_tracy_child_destroy(&module_data, child)) {
		ERROR("Error in tracy_child_destroy!\n");
		tracy_quit(module_data.tracy, 1);
	}
	// free
	free(mbc);
	child->custom = NULL;
}

static void usr1_sighandler(int sig, siginfo_t * siginfo, void *context)
{
	(void)(sig);
	(void)(context);

	// attach
	if (!tracy_attach(module_data.tracy, siginfo->si_pid)) {
		kperror("tracy_attach");
		return;
	}
}

int main(int argc, char **argv)
{
	struct tracy *tracy;
	long tracy_opt = TRACY_MEMORY_FALLBACK | TRACY_WORKAROUND_ARM_7475_1;

	// VOLDWRAPPER
	if (argc == 2 && !strcmp(argv[1], "voldwrapper")) {
		kill(1, SIGUSR1);

		// create local copy of vold without file context
		mkpath("/multiboot/bin", 0755);
		util_copy("/system/bin/vold", "/multiboot/bin/vold", false,
			  true);

		char *newargv[] = { "/multiboot/bin/vold", NULL };
		execvp(newargv[0], newargv);
	}
	// clear module_data    
	memset(&module_data, 0, sizeof(module_data));
	module_data.initstage = INITSTAGE_NONE;

	// early init
	if (setup()) {
		ERROR("error in setup()!\n");
		return EXIT_FAILURE;
	}
	// run /init directly without any patching or tracing
	if (!module_data.multiboot_enabled && !module_data.sndstage_enabled) {
		INFO("multiboot disabled. run /init ...\n");
		if (run_init(NULL)) {
			kperror("run_init");
			return EXIT_FAILURE;
		}
		return 0;
	}
	// !MULTIBOOT && !2NDSTAGE IS IMPOSSIBLE FROM HERE

	// tracy init
	tracy_opt |= TRACY_TRACE_CHILDREN;
	module_data.tracy = tracy = tracy_init(tracy_opt);

	tracy->se.child_create = &multiboot_child_create;
	tracy->se.child_destroy = &multiboot_child_destroy;

	// tracy init
	module_data.initstage = INITSTAGE_TRACY;
	if (modules_call_tracy_init(&module_data)) {
		ERROR("error in tracy_init!\n");
		return EXIT_FAILURE;
	}

	if (module_data.bootmode == BOOTMODE_RECOVERY) {
		// run and trace /init
		if (run_init(tracy)) {
			kperror("tracy_exec");
			return EXIT_FAILURE;
		}
	} else {
		// start init in child process
		pid_t pid = fork();
		if (pid == 0) {
			if (run_init(NULL)) {
				kperror("run_init");
				return EXIT_FAILURE;
			}
		}
	}

	// setup "traceme" signal handler
	if (module_data.bootmode != BOOTMODE_RECOVERY) {
		struct sigaction act;
		memset(&act, 0, sizeof(act));
		act.sa_sigaction = &usr1_sighandler;
		act.sa_flags = SA_SIGINFO;
		if (sigaction(SIGUSR1, &act, NULL) < 0) {
			kperror("sigaction");
			return EXIT_FAILURE;
		}
	}
	// Main event-loop
	tracy_main(tracy);

	// cleanup
	tracy_free(tracy);

	// wait for all childs to finish - that hopefully will never happen
	ERROR("TRACY EXIT. waiting now...\n");
	while (waitpid(-1, NULL, 0)) {
		if (errno == ECHILD) {
			break;
		}
	}

	return EXIT_SUCCESS;
}
