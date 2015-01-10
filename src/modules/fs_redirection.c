#include <common.h>

#define do_hook(syscall, hook) \
if (tracy_set_hook(data->tracy, #syscall, TRACY_ABI_NATIVE, hook)) { \
	ERROR("Could not hook syscall '%s'\n", #syscall); \
	return -1; \
}

struct redirect_info {
	char *syscall_name;
	int argnum;
	bool resolve_symlinks;
	int syscall_number;
	int atflags;
};

static struct redirect_info info[] = {
	{"stat", 0, 1, 0, -1},
	{"lstat", 0, 0, 0, -1},
	{"newstat", 0, 1, 0, -1},
	{"newlstat", 0, 0, 0, -1},
	{"stat64", 0, 1, 0, -1},
	{"lstat64", 0, 0, 0, -1},
	{"chmod", 0, 0, 0, -1},
	{"open", 0, 1, 1, -1},
	{"access", 0, 0, 0, -1},
	{"chown", 0, 1, 0, -1},
	{"lchown", 0, 0, 0, -1},
	{"chown16", 0, 1, 0, -1},
	{"lchown16", 0, 0, 0, -1},
	{"utime", 0, 0, 0, -1},
	{"utimes", 0, 0, 0, -1},
	{"futimesat", 1, 0, 0, -1},
	{"faccessat", 1, 0, 0, 3},
	{"fchmodat", 1, 0, 0, 3},
	{"fchownat", 1, 0, 0, 4},
	{"openat", 1, 0, 1, -1},
	{"newfstatat", 1, 1, 0, 3},
	{"fstatat64", 1, 1, 0, 3},
	{"utimensat", 1, 0, 0, 3},
};

static int redirect_file_access(struct tracy_event *e, int argpos,
				bool resolve_symlinks);
static struct module_data *module_data = NULL;

static int hook_fileaccess(struct tracy_event *e)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(info); i++) {
		if (info[i].syscall_number == e->syscall_num) {
			bool resolve_symlinks = info[i].resolve_symlinks;
			int atflags_pos = info[i].atflags;

			if (atflags_pos >= 0) {
				long *argptr = &e->args.a0;
				resolve_symlinks =
				    !(argptr[atflags_pos] &
				      AT_SYMLINK_NOFOLLOW);
			}

			if (e->syscall_num ==
			    get_syscall_number_abi("open", e->abi))
				resolve_symlinks = !(e->args.a1 & O_NOFOLLOW);
			else if (e->syscall_num ==
				 get_syscall_number_abi("openat", e->abi))
				resolve_symlinks = !(e->args.a3 & O_NOFOLLOW);

			return redirect_file_access(e, info[i].argnum,
						    resolve_symlinks);
		}
	}

	return TRACY_HOOK_ABORT;
}

static bool fstab_rec_matches(const char *devname, struct fstab_rec *rec,
			      struct stat *sb, bool use_stat)
{
	if (use_stat && sb->st_rdev == rec->statbuf.st_rdev) {
		return true;
	}
	if (!strcmp(devname, rec->blk_device)) {
		return true;
	}

	return false;
}

static struct fstab_rec *get_fstab_rec(const char *devname)
{
	int i;
	struct stat sb;

	bool use_stat = !stat(devname, &sb);
	struct fstab *mbfstab = module_data->multiboot_fstab;

	// multiboot source
	if (module_data->multiboot_path && fstab_rec_matches
	    (devname, &module_data->multiboot_device, &sb, use_stat)) {
		return &module_data->multiboot_device;
	}
	// grub device
	if (module_data->grub_path && fstab_rec_matches
	    (devname, &module_data->grub_device, &sb, use_stat)) {
		return &module_data->grub_device;
	}

	for (i = 0; i < mbfstab->num_entries; i++) {
		if (!fs_mgr_is_multiboot(&mbfstab->recs[i]))
			continue;

		if (fstab_rec_matches
		    (devname, &mbfstab->recs[i], &sb, use_stat)) {
			return &mbfstab->recs[i];
		}
	}

	DEBUG("%s: no match for %s\n", __func__, devname);
	return NULL;
}

static struct fd_info *make_fdinfo(struct tracy_child *child, unsigned int fd,
				   char *filename)
{
	struct fd_info *fdi = (void *)malloc(sizeof(struct fd_info));
	memset(fdi, 0, sizeof(fdi[0]));

	fdi->child = child;
	fdi->fd = fd;
	fdi->filename = filename;

	// check if we need to redirect this partition
	struct fstab_rec *fstabrec = get_fstab_rec(fdi->filename);
	if (fstabrec) {
		fdi->device = strdup(fstabrec->stub_device);

		// get fstype
		const char *fstype = get_fstype(fdi->device);
		if (!fstype)
			fstype = "ext4";
		fdi->fs_type = strdup(fstype);
	}

	fdi->fs_pdata = NULL;
	fs_pre(fdi);

	return fdi;
}

static void free_fdinfo(struct fd_info *fdi)
{
	fs_cleanup(fdi);

	if (fdi->filename) {
		free(fdi->filename);
		fdi->filename = NULL;
	}

	if (fdi->fs_type) {
		free(fdi->fs_type);
		fdi->fs_type = NULL;
	}

	if (fdi->device) {
		free(fdi->device);
		fdi->device = NULL;
	}
}

static void free_fdinfo_list(struct tracy_ll *ll)
{
	struct tracy_ll_item *t;

	t = ll->head;

	// free custom fields
	while (t) {
		if (t->data) {
			struct fd_info *fdi = t->data;
			if (fdi) {
				WARNING("unclosed file: %d(%s)\n", t->id,
					fdi->filename);
				free_fdinfo(fdi);
				t->data = NULL;
			}
		}

		t = t->next;
	}

	// free the list itself
	ll_free(ll);
}

/*
 * This function creates a fdinfo entry if the device needs format detection
 */
static int hook_open(struct tracy_event *e, int argpos, bool resolve_symlinks)
{
	int rc = TRACY_HOOK_CONTINUE;
	struct fstab_rec *fstabrec;
	struct multiboot_child_data *mbc = e->child->custom;
	char *path = NULL;
	long *argptr = NULL;

	if (e->child->pre_syscall) {
		argptr = &e->args.a0;

		// we don't need to handle readonly access
		int flags = (int)argptr[argpos + 1];
		if ((flags & O_ACCMODE) == O_RDONLY)
			goto out;

		// get path
		path = get_patharg(e->child, argptr[argpos], resolve_symlinks);
		if (!path) {
			rc = TRACY_HOOK_ABORT;
			goto out;
		}
		// check if we need to redirect this partition
		fstabrec = get_fstab_rec(path);
		if (!fstabrec) {
			goto out;
		}
		// losetup's can be formatted already
		if (!fstabrec->replacement_bind) {
			ERROR("%s: no bind for %s\n", __func__, path);
			goto out;
		}
		// check fs now
		// to update the last_checked timestamp
		check_fs_nomount(fstabrec->stub_device);

		mbc->handled_by_open = 1;

		// store path for post_syscall
		mbc->tmp = path;
	}

	else if (mbc->handled_by_open) {
		// we need to handle format for this fd
		if (mbc->tmp) {
			int fd = (int)e->args.return_code;
			ERROR("open(%s) = %d\n", mbc->tmp, fd);

			// add to fd list
			if (fd >= 0) {
				struct fd_info *fdi =
				    make_fdinfo(e->child, fd, mbc->tmp);

				if (fdi)
					ll_add(mbc->files, fd, fdi);
				else {
					ERROR("Couldn't make fdinfo for %s!\n",
					      mbc->tmp);
					free(mbc->tmp);
				}
			}
			// open failed
			else
				free(mbc->tmp);

			mbc->tmp = NULL;
		}
	}

out:
	if (rc && path)
		free(path);

	if (!e->child->pre_syscall)
		mbc->handled_by_open = 0;

	return rc;
}

static int redirect_file_access(struct tracy_event *e, int argpos,
				bool resolve_symlinks)
{
	struct tracy_sc_args a;
	int rc = TRACY_HOOK_CONTINUE;
	char *path = NULL;
	tracy_child_addr_t devname_new = NULL;
	struct fstab_rec *fstabrec;
	long *argptr;
	struct multiboot_child_data *mbc = e->child->custom;

	// call hook_open
	if (e->syscall_num == get_syscall_number_abi("open", e->abi)
	    || e->syscall_num == get_syscall_number_abi("openat", e->abi)) {
		int rc_open = hook_open(e, argpos, resolve_symlinks);
		if (rc_open)
			return rc_open;
	}

	if (e->child->pre_syscall) {
		// get path
		argptr = &e->args.a0;
		path = get_patharg(e->child, argptr[argpos], resolve_symlinks);
		if (!path) {
			rc = TRACY_HOOK_ABORT;
			goto out;
		}
		// check if we need to redirect this file
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

		char *replacement =
		    fstabrec->replacement_bind ? fstabrec->
		    stub_device : fstabrec->replacement_device;

		DEBUG("%s(%s): redirect %s->%s arg=%d\n", __func__,
		      get_syscall_name_abi(e->syscall_num, e->abi), path,
		      replacement, argpos);

		// copy new devname
		devname_new = copy_patharg(e->child, replacement);
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
		mbc->memory = devname_new;
	} else {
		// free previous data
		if (mbc->memory) {
			free_patharg(e->child, mbc->memory);
			mbc->memory = NULL;
		}
	}

out:
	if (path)
		free(path);
	if (devname_new && rc)
		free_patharg(e->child, devname_new);
	return rc;
}

static int hook_close(struct tracy_event *e)
{
	int rc = TRACY_HOOK_CONTINUE;
	struct multiboot_child_data *mbc = e->child->custom;

	if (e->child->pre_syscall) {
		int fd = (int)e->args.a0;
		struct tracy_ll_item *item = ll_find(mbc->files, fd);

		if (item) {
			struct fd_info *fdi = item->data;
			ERROR("close(%d|%s)\n", fd, fdi->filename);

			// TODO detect format
			if (fs_was_format(fdi)) {
				DEBUG("FORMAT DETECTED!\n");
				struct fstab_rec *fstabrec =
				    get_fstab_rec(fdi->filename);
				if (fstabrec) {
					INFO("format path %s...\n",
					     fstabrec->replacement_device);
					char buf[PATH_MAX];
					snprintf(buf, sizeof(buf), "%s/*",
						 fstabrec->replacement_device);
					format_path(buf);
				}
			}

			free_fdinfo(fdi);
			ll_del(mbc->files, fd);
		}
	}

	return rc;
}

static int hook_dup(struct tracy_event *e)
{
	int rc = TRACY_HOOK_CONTINUE;
	struct multiboot_child_data *mbc = e->child->custom;

	if (e->child->pre_syscall) {
		int fd = (int)e->args.a0;

		struct tracy_ll_item *item = ll_find(mbc->files, fd);
		if (item) {
			struct fd_info *fdi = item->data;
			mbc->tmp = strdup(fdi->filename);
		}
	}

	else {
		// we need to handle write for this fd
		if (mbc->tmp) {
			int fd = (int)e->args.return_code;
			DEBUG("dup(%s) = %d\n", mbc->tmp, fd);

			if (fd >= 0) {
				struct fd_info *fdi =
				    make_fdinfo(e->child, fd, mbc->tmp);

				if (fdi)
					ll_add(mbc->files, fd,
					       make_fdinfo(e->child, fd,
							   mbc->tmp));
				else {
					ERROR("Couldn't make fdinfo for %s!\n",
					      mbc->tmp);
					free(mbc->tmp);
				}
			}
			// dup failed
			else
				free(mbc->tmp);

			mbc->tmp = NULL;
		}
	}

	return rc;
}

static int hook_fcntl(struct tracy_event *e)
{
	int rc = TRACY_HOOK_CONTINUE;
	struct multiboot_child_data *mbc = e->child->custom;

	if (e->child->pre_syscall) {
		int fd = (int)e->args.a0;

		struct tracy_ll_item *item = ll_find(mbc->files, fd);
		if (item) {
			struct fd_info *fdi = item->data;

			// TODO handle switiching access mode
			ERROR("fcntl(%d|%s)\n", fd, fdi->filename);
			rc = TRACY_HOOK_ABORT;
		}
	}

	return rc;
}

static struct fstab_rec *asec_rec;

static int hook_mount(struct tracy_event *e)
{
	struct tracy_sc_args a;
	struct fstab_rec *fstabrec;
	tracy_child_addr_t devname_new = NULL;
	char *devname = NULL, *mountpoint = NULL;
	int rc = TRACY_HOOK_CONTINUE;
	struct multiboot_child_data *mbc = e->child->custom;

	if (e->child->pre_syscall) {
		// get args
		devname = get_patharg(e->child, e->args.a0, 1);
		if (!devname) {
			rc = TRACY_HOOK_ABORT;
			goto out;
		}
		mountpoint = get_patharg(e->child, e->args.a1, 1);
		if (!mountpoint) {
			rc = TRACY_HOOK_ABORT;
			goto out;
		}
		unsigned long flags = (unsigned long)e->args.a3;
		DEBUG("mount %s on %s remount=%lu, ro=%lu\n",
		      devname, mountpoint, (flags & MS_REMOUNT),
		      (flags & MS_RDONLY));

		if (!strcmp(mountpoint, "/mnt/secure/asec")) {
			fstabrec = asec_rec;
		} else {
			// check if we need to redirect this partition
			fstabrec = get_fstab_rec(devname);
			if (!fstabrec) {
				goto out;
			}
		}

		DEBUG("hijack: mount %s on %s\n", devname, mountpoint);

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
		mbc->memory = devname_new;
	} else {
		// free previous data
		if (mbc->memory) {
			free_patharg(e->child, mbc->memory);
			mbc->memory = NULL;
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

static int fsr_tracy_init(struct module_data *data)
{
	unsigned i;
	module_data = data;
	DEBUG("%s\n", __func__);
	// hooks for file access redirection
	for (i = 0; i < ARRAY_SIZE(info); i++) {
		info[i].syscall_number =
		    get_syscall_number_abi(info[i].syscall_name,
					   TRACY_ABI_NATIVE);
		if (tracy_set_hook
		    (data->tracy, info[i].syscall_name,
		     TRACY_ABI_NATIVE, hook_fileaccess)) {
			ERROR("Could not hook %s\n", info[i].syscall_name);
			return -1;
		}
	}

	// open/close
	do_hook(close, hook_close);

	// dup
	do_hook(dup, hook_dup);
	do_hook(dup2, hook_dup);
	do_hook(dup3, hook_dup);

	// fnctl
	do_hook(fcntl, hook_fcntl);
	do_hook(fcntl64, hook_fcntl);
	// mount
	do_hook(mount, hook_mount);

	// prepate asec_rec
	asec_rec = calloc(sizeof(asec_rec[0]), 1);
	asec_rec->replacement_bind = 0;
	asec_rec->replacement_device = malloc(PATH_MAX);
	snprintf(asec_rec->replacement_device, PATH_MAX,
		 PATH_MOUNTPOINT_SOURCE "%s/android_secure",
		 data->multiboot_path);

	// create android_secure folder
	mkpath(asec_rec->replacement_device, 0770);
	chown(asec_rec->replacement_device, 1023, 1023);

	return 0;
}

static int fsr_tracy_child_create(struct module_data
				  __attribute__ ((__unused__)) * data,
				  struct tracy_child *child)
{
	struct multiboot_child_data *mbc = child->custom;
	DEBUG("%s\n", __func__);

	// create list
	mbc->files = ll_init();
	if (!mbc->files) {
		ERROR("Couldn't allocate mbc->files!\n");
		return -1;
	}

	return 0;
}

static int fsr_tracy_child_destroy(struct module_data
				   __attribute__ ((__unused__)) * data,
				   struct tracy_child *child)
{
	struct multiboot_child_data *mbc = child->custom;
	DEBUG("%s\n", __func__);
	// free list
	free_fdinfo_list(mbc->files);
	mbc->files = NULL;

	return 0;
}

static struct module module_fs_redirection = {
	.tracy_init = fsr_tracy_init,
	.tracy_child_create = fsr_tracy_child_create,
	.tracy_child_destroy = fsr_tracy_child_destroy,
};

module_add(module_fs_redirection);
