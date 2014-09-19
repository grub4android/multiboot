#include <tracy.h>
#include <ll.h>
#include <stdbool.h>
#include <sys/mount.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>

#include <lib/fs_mgr.h>
#include <lib/klog.h>
#include <util.h>

#include <mb_common.h>
#include <mb_fs_redirection.h>
#include <mb_fstab_patcher.h>

struct redirect_info {
	char *syscall_name;
	int argnum;
	bool resolve_symlinks;
	int syscall_number;
};

static struct redirect_info info[] = {
	{"stat", 0, 1, 0},
	{"lstat", 0, 0, 0},
	{"newstat", 0, 1, 0},
	{"newlstat", 0, 0, 0},
	{"stat64", 0, 1, 0},
	{"lstat64", 0, 0, 0},
	{"chmod", 0, 0, 0},
	{"open", 0, 1, 0},
	{"access", 0, 0, 0},
	{"chown", 0, 1, 0},
	{"lchown", 0, 0, 0},
	{"chown16", 0, 1, 0},
	{"lchown16", 0, 0, 0},
	{"utime", 0, 0, 0},
	{"utimes", 0, 0, 0},
	{"mknodat", 1, 0, 0},
	{"futimesat", 1, 0, 0},
	{"faccessat", 1, 0, 0},
	{"fchmodat", 1, 0, 0},
	{"fchownat", 1, 0, 0},
	{"openat", 1, 0, 0},
	{"newfstatat", 1, 1, 0},
	{"fstatat64", 1, 1, 0},
	{"utimensat", 1, 0, 0},
};

static int redirect_file_access(struct tracy_event *e, int argpos,
				bool resolve_symlinks)
{
	struct tracy_sc_args a;
	int rc = TRACY_HOOK_CONTINUE;
	char *path = NULL;
	tracy_child_addr_t devname_new = NULL;
	struct fstab_rec *fstabrec;
	long *argptr;

	if (e->child->pre_syscall) {
		// we need to wait for init
		if (!mb_ready())
			goto out;

		// get path
		argptr = &e->args.a0;
		path = get_patharg(e->child, argptr[argpos], resolve_symlinks);

		// check if we need to redirect this partition
		fstabrec = get_fstab_rec(path);
		if (!fstabrec) {
			goto out;
		}
		// ignore symlinks for calls which don't resolve them
		struct stat sb;
		if (!resolve_symlinks && !lstat(path, &sb)
		    && S_ISLNK(sb.st_mode)) {
			goto out;
		}

		KLOG_INFO(LOG_TAG, "%s(%s): redirect %s arg=%d\n", __func__,
			  get_syscall_name_abi(e->syscall_num, e->abi), path,
			  argpos);

		// copy new devname
		devname_new =
		    copy_patharg(e->child,
				 fstabrec->replacement_bind ? "/dev/null" :
				 fstabrec->replacement_device);
		if (!devname_new) {
			kperror("copy_patharg");
			rc = TRACY_HOOK_ABORT;
			goto out;
		}
		// copy args
		memcpy(&a, &(e->args), sizeof(struct tracy_sc_args));

		// modify args
		argptr = &a.a0;
		argptr[argpos] = (long)devname_new;

		// write new args
		if (tracy_modify_syscall_args(e->child, a.syscall, &a)) {
			kperror("tracy_modify_syscall_args");
			rc = TRACY_HOOK_ABORT;
			goto out;
		}
		// set devname so we can free it later
		e->child->custom = (void *)devname_new;
	} else {
		// free previous data
		if (e->child->custom) {
			free_patharg(e->child,
				     (tracy_child_addr_t) e->child->custom);
			e->child->custom = NULL;
		}
	}

out:
	if (path)
		free(path);
	if (devname_new && rc)
		free_patharg(e->child, devname_new);
	return rc;
}

static int hook_fileaccess(struct tracy_event *e)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(info); i++) {
		if (info[i].syscall_number == e->syscall_num) {
			return redirect_file_access(e, info[i].argnum,
						    info[i].resolve_symlinks);
		}
	}

	return TRACY_HOOK_ABORT;
}

int redirection_hook_mount(struct tracy_event *e)
{
	struct tracy_sc_args a;
	struct fstab_rec *fstabrec;
	tracy_child_addr_t devname_new = NULL;
	char *devname = NULL, *mountpoint = NULL;
	int rc = TRACY_HOOK_CONTINUE;

	if (e->child->pre_syscall) {
		// we need to wait for init
		if (!mb_ready())
			goto out;

		// get args
		devname = get_patharg(e->child, e->args.a0, 1);
		mountpoint = get_patharg(e->child, e->args.a1, 1);
		unsigned long flags = (unsigned long)e->args.a3;
		KLOG_DEBUG(LOG_TAG, "mount %s on %s remount=%lu, ro=%lu\n",
			   devname, mountpoint, (flags & MS_REMOUNT),
			   (flags & MS_RDONLY));

		// check if we need to redirect this partition
		fstabrec = get_fstab_rec(devname);
		if (!fstabrec) {
			goto out;
		}

		KLOG_INFO(LOG_TAG, "hijack: mount %s on %s\n", devname,
			  mountpoint);

		// copy new devname
		devname_new =
		    copy_patharg(e->child, fstabrec->replacement_device);
		if (!devname_new) {
			kperror("copy_patharg");
			rc = TRACY_HOOK_ABORT;
			goto out;
		}
		// copy args
		memcpy(&a, &(e->args), sizeof(struct tracy_sc_args));

		// modify args
		a.a0 = (long)devname_new;
		if (fstabrec->replacement_bind) {
			a.a2 = 0;
			a.a3 |= MS_BIND;
		}
		// write new args
		if (tracy_modify_syscall_args(e->child, a.syscall, &a)) {
			kperror("tracy_modify_syscall_args");
			rc = TRACY_HOOK_ABORT;
			goto out;
		}
		// set devname so we can free it later
		e->child->custom = (void *)devname_new;
	} else {
		// free previous data
		if (e->child->custom) {
			free_patharg(e->child,
				     (tracy_child_addr_t) e->child->custom);
			e->child->custom = NULL;
		}
	}

out:
	if (devname)
		free(devname);
	if (mountpoint)
		free(mountpoint);
	if (devname_new && rc)
		free_patharg(e->child, devname_new);

	return rc;
}

int redirection_tracy_init(struct tracy *tracy)
{
	unsigned i;

	// hooks for file access redirection
	for (i = 0; i < ARRAY_SIZE(info); i++) {
		info[i].syscall_number =
		    get_syscall_number_abi(info[i].syscall_name,
					   TRACY_ABI_NATIVE);
		if (tracy_set_hook
		    (tracy, info[i].syscall_name, TRACY_ABI_NATIVE,
		     hook_fileaccess)) {
			KLOG_ERROR(LOG_TAG, "Could not hook %s\n",
				   info[i].syscall_name);
			return -1;
		}
	}

	return 0;
}
