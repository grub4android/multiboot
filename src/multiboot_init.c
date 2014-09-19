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
#include <dirent.h>

#include <lib/cmdline.h>
#include <lib/fs_mgr.h>
#include <lib/klog.h>
#include <util.h>

#include <mb_common.h>
#include <mb_fs_redirection.h>
#include <mb_fstab_patcher.h>
#include <mb_vold.h>

#include <tracy.h>
#include <ll.h>

#define MAX_PARAMETERS 64

static bool vold_only = false;

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

static int early_init(void)
{
	int rc = 0;

	// init klog
	klog_init();
	klog_set_level(6);
	KLOG_INFO(LOG_TAG, "init\n");

	// mount procfs
	mkdir("/proc", 0755);
	mount("proc", "/proc", "proc", 0, NULL);

	// COMMON: parse cmdline
	import_kernel_cmdline(import_kernel_nv);

	// we don't need any multiboot init
	if (!system_is_multiboot() && !system_is_sndstage()) {
		goto unmount_procfs;
	}
	// COMMON: early init
	if (common_early_init()) {
		rc = -1;
		goto unmount_procfs;
	}
	// FSTAB: early init
	if (fstabpatcher_early_init())
		return -1;

	// disable aries' dualboot init
	if (system_is_multiboot() && !system_is_recovery()) {
		if (!unlink("/sbin/dualboot_init"))
			copy_file("/fstab.aries", "/fstab.aries.patched");
	}

unmount_procfs:
	// unmount procfs
	umount("/proc");

	return rc;
}

int hook_mount(struct tracy_event *e)
{
	int rc = TRACY_HOOK_CONTINUE;

	// check if we can init now
	if (!mb_ready() && can_init()) {
		common_late_init();

		// WORKAROUND: we got the context-requirement via a shellscript
		//             and can stop tracing completely now.
		if (vold_only) {
			rc = TRACY_HOOK_STOP;
		}

		goto finish;
	}
	// not ready yet
	if (!mb_ready())
		goto finish;

	// (mini)vold
	if (vold_only) {
		// TODO handle vold mounts
	}
	// multiboot-recovery
	else {
		rc = redirection_hook_mount(e);
	}

finish:
	return rc;
}

int main(int __attribute__ ((unused)) argc, char
	 __attribute__ ((unused)) ** argv)
{
	struct tracy *tracy;

	// early init
	if (early_init())
		return EXIT_FAILURE;

	// run /init directly without any patching or tracing
	if (!system_is_multiboot() && !system_is_sndstage()) {
		KLOG_INFO(LOG_TAG, "multiboot disabled. run /init ...\n");
		if (run_init(NULL)) {
			kperror("run_init");
			return EXIT_FAILURE;
		}
		return 0;
	}
	// !MULTIBOOT && !2NDSTAGE IS IMPOSSIBLE FROM HERE

	// tracy init
	tracy = tracy_init(TRACY_TRACE_CHILDREN | TRACY_MEMORY_FALLBACK);

	// hook mount
	if (tracy_set_hook(tracy, "mount", TRACY_ABI_NATIVE, hook_mount)) {
		KLOG_ERROR(LOG_TAG, "Could not hook mount\n");
		return EXIT_FAILURE;
	}
	// RECOVERY
	if (system_is_recovery()) {
		// multiboot-recovery gets full redirection
		if (system_is_multiboot()) {
			if (redirection_tracy_init(tracy))
				return EXIT_FAILURE;
		} else if (system_is_sndstage()) {
			vold_only = true;
		}
		// else is impossible
	}
	// ANDROID
	else {
		vold_only = true;
	}

	if (vold_only) {
		// TODO fix tracing vold
		// vold_tracy_init(tracy);
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
