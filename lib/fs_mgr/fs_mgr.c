/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <libgen.h>
#include <time.h>
#include <sys/swap.h>
#include <stdbool.h>
#include <util.h>

#include <linux/loop.h>
#include <linux/capability.h>

#include "fs_mgr_priv.h"

#define E2FSCK_BIN      "/multiboot/e2fsck"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))

struct flag_list {
	const char *name;
	unsigned flag;
};

static struct flag_list mount_flags[] = {
	{"noatime", MS_NOATIME},
	{"noexec", MS_NOEXEC},
	{"nosuid", MS_NOSUID},
	{"nodev", MS_NODEV},
	{"nodiratime", MS_NODIRATIME},
	{"ro", MS_RDONLY},
	{"rw", 0},
	{"remount", MS_REMOUNT},
	{"bind", MS_BIND},
	{"rec", MS_REC},
	{"unbindable", MS_UNBINDABLE},
	{"private", MS_PRIVATE},
	{"slave", MS_SLAVE},
	{"shared", MS_SHARED},
	{"sync", MS_SYNCHRONOUS},
	{"defaults", 0},
	{0, 0},
};

static struct flag_list fs_mgr_flags[] = {
	{"wait", MF_WAIT},
	{"check", MF_CHECK},
	{"encryptable=", MF_CRYPT},
	{"nonremovable", MF_NONREMOVABLE},
	{"voldmanaged=", MF_VOLDMANAGED},
	{"length=", MF_LENGTH},
	{"recoveryonly", MF_RECOVERYONLY},
	{"swapprio=", MF_SWAPPRIO},
	{"zramsize=", MF_ZRAMSIZE},
	{"verify", MF_VERIFY},
	{"noemulatedsd", MF_NOEMULATEDSD},
	{"multiboot", MF_MULTIBOOT},
	{"defaults", 0},
	{0, 0},
};

struct fs_mgr_flag_values {
	char *key_loc;
	long long part_length;
	char *label;
	int partnum;
	int swap_prio;
	unsigned int zram_size;
};

/*
 * gettime() - returns the time in seconds of the system's monotonic clock or
 * zero on error.
 */
static time_t gettime(void)
{
	struct timespec ts;
	int ret;

	ret = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (ret < 0) {
		ERROR("clock_gettime(CLOCK_MONOTONIC) failed: %s\n",
		      strerror(errno));
		return 0;
	}

	return ts.tv_sec;
}

static int wait_for_file(const char *filename, int timeout)
{
	struct stat info;
	time_t timeout_time = gettime() + timeout;
	int ret = -1;

	while (gettime() < timeout_time && ((ret = stat(filename, &info)) < 0))
		usleep(10000);

	return ret;
}

static int parse_flags(char *flags, struct flag_list *fl,
		       struct fs_mgr_flag_values *flag_vals,
		       char *fs_options, int fs_options_len)
{
	int f = 0;
	int i;
	char *p;
	char *savep;

	/* initialize flag values.  If we find a relevant flag, we'll
	 * update the value */
	if (flag_vals) {
		memset(flag_vals, 0, sizeof(*flag_vals));
		flag_vals->partnum = -1;
		flag_vals->swap_prio = -1;	/* negative means it wasn't specified. */
	}

	/* initialize fs_options to the null string */
	if (fs_options && (fs_options_len > 0)) {
		fs_options[0] = '\0';
	}

	p = strtok_r(flags, ",", &savep);
	while (p) {
		/* Look for the flag "p" in the flag list "fl"
		 * If not found, the loop exits with fl[i].name being null.
		 */
		for (i = 0; fl[i].name; i++) {
			if (!strncmp(p, fl[i].name, strlen(fl[i].name))) {
				f |= fl[i].flag;
				if ((fl[i].flag == MF_CRYPT) && flag_vals) {
					/* The encryptable flag is followed by an = and the
					 * location of the keys.  Get it and return it.
					 */
					flag_vals->key_loc =
					    strdup(strchr(p, '=') + 1);
				} else if ((fl[i].flag == MF_LENGTH)
					   && flag_vals) {
					/* The length flag is followed by an = and the
					 * size of the partition.  Get it and return it.
					 */
					flag_vals->part_length =
					    strtoll(strchr(p, '=') + 1, NULL,
						    0);
				} else if ((fl[i].flag == MF_VOLDMANAGED)
					   && flag_vals) {
					/* The voldmanaged flag is followed by an = and the
					 * label, a colon and the partition number or the
					 * word "auto", e.g.
					 *   voldmanaged=sdcard:3
					 * Get and return them.
					 */
					char *label_start;
					char *label_end;
					char *part_start;

					label_start = strchr(p, '=') + 1;
					label_end = strchr(p, ':');
					if (label_end) {
						flag_vals->label =
						    strndup(label_start,
							    (int)(label_end -
								  label_start));
						part_start = strchr(p, ':') + 1;
						if (!strcmp(part_start, "auto")) {
							flag_vals->partnum = -1;
						} else {
							flag_vals->partnum =
							    strtol(part_start,
								   NULL, 0);
						}
					} else {
						ERROR
						    ("Warning: voldmanaged= flag malformed\n");
					}
				} else if ((fl[i].flag == MF_SWAPPRIO)
					   && flag_vals) {
					flag_vals->swap_prio =
					    strtoll(strchr(p, '=') + 1, NULL,
						    0);
				} else if ((fl[i].flag == MF_ZRAMSIZE)
					   && flag_vals) {
					flag_vals->zram_size =
					    strtoll(strchr(p, '=') + 1, NULL,
						    0);
				}
				break;
			}
		}

		if (!fl[i].name) {
			if (fs_options) {
				/* It's not a known flag, so it must be a filesystem specific
				 * option.  Add it to fs_options if it was passed in.
				 */
				strlcat(fs_options, p, fs_options_len);
				strlcat(fs_options, ",", fs_options_len);
			} else {
				/* fs_options was not passed in, so if the flag is unknown
				 * it's an error.
				 */
				ERROR("Warning: unknown flag %s\n", p);
			}
		}
		p = strtok_r(NULL, ",", &savep);
	}

	if (fs_options && fs_options[0]) {
		/* remove the last trailing comma from the list of options */
		fs_options[strlen(fs_options) - 1] = '\0';
	}

	return f;
}

struct fstab *do_fs_mgr_read_fstab(const char *fstab_path, bool twrp)
{
	FILE *fstab_file;
	int cnt, entries;
	ssize_t len;
	size_t alloc_len = 0;
	char *line = NULL;
	const char *delim = " \t";
	char *save_ptr, *p;
	struct fstab *fstab = NULL;
	struct fs_mgr_flag_values flag_vals;
#define FS_OPTIONS_LEN 1024
	char tmp_fs_options[FS_OPTIONS_LEN];

	fstab_file = fopen(fstab_path, "r");
	if (!fstab_file) {
		ERROR("Cannot open file %s\n", fstab_path);
		return 0;
	}

	entries = 0;
	while ((len = getline(&line, &alloc_len, fstab_file)) != -1) {
		/* if the last character is a newline, shorten the string by 1 byte */
		if (line[len - 1] == '\n') {
			line[len - 1] = '\0';
		}
		/* Skip any leading whitespace */
		p = line;
		while (isspace(*p)) {
			p++;
		}
		/* ignore comments or empty lines */
		if (*p == '#' || *p == '\0')
			continue;
		entries++;
	}

	if (!entries) {
		ERROR("No entries found in fstab\n");
		goto err;
	}

	/* Allocate and init the fstab structure */
	fstab = calloc(1, sizeof(struct fstab));
	fstab->twrp = twrp;
	fstab->num_entries = entries;
	fstab->fstab_filename = strdup(fstab_path);
	fstab->recs = calloc(fstab->num_entries, sizeof(struct fstab_rec));

	fseek(fstab_file, 0, SEEK_SET);

	cnt = 0;
	while ((len = getline(&line, &alloc_len, fstab_file)) != -1) {
		/* if the last character is a newline, shorten the string by 1 byte */
		if (line[len - 1] == '\n') {
			line[len - 1] = '\0';
		}

		/* Skip any leading whitespace */
		p = line;
		while (isspace(*p)) {
			p++;
		}
		/* ignore comments or empty lines */
		if (*p == '#' || *p == '\0')
			continue;

		/* If a non-comment entry is greater than the size we allocated, give an
		 * error and quit.  This can happen in the unlikely case the file changes
		 * between the two reads.
		 */
		if (cnt >= entries) {
			ERROR("Tried to process more entries than counted\n");
			break;
		}

		if (twrp) {
			if (!(p = strtok_r(line, delim, &save_ptr))) {
				ERROR("Error parsing mount_point\n");
				goto err;
			}
			fstab->recs[cnt].mount_point = strdup(p);

			if (!(p = strtok_r(NULL, delim, &save_ptr))) {
				ERROR("Error parsing fs_type\n");
				goto err;
			}
			fstab->recs[cnt].fs_type = strdup(p);

			if (!(p = strtok_r(NULL, delim, &save_ptr))) {
				ERROR("Error parsing mount source\n");
				goto err;
			}
			fstab->recs[cnt].blk_device = strdup(p);

			char *unhandled = NULL;
			while ((p = strtok_r(NULL, delim, &save_ptr))) {
				int len_old =
				    unhandled ? strlen(unhandled) : 0, len_new =
				    strlen(p);
				unhandled =
				    realloc(unhandled, len_old + len_new + 2);
				memcpy(unhandled + len_old, p, len_new);
				unhandled[len_old + len_new] = ' ';
				unhandled[len_old + len_new + 1] = '\0';
			}

			fstab->recs[cnt].unhandled_columns = unhandled;

			fstab->recs[cnt].fs_options_unparsed =
			    strdup("defaults");
			fstab->recs[cnt].fs_options = strdup("");
			fstab->recs[cnt].flags = 0;

			fstab->recs[cnt].fs_mgr_flags_unparsed =
			    strdup("defaults");
			fstab->recs[cnt].fs_mgr_flags = 0;

			goto finish_rec;
		}

		if (!(p = strtok_r(line, delim, &save_ptr))) {
			ERROR("Error parsing mount source\n");
			goto err;
		}
		fstab->recs[cnt].blk_device = strdup(p);

		if (!(p = strtok_r(NULL, delim, &save_ptr))) {
			ERROR("Error parsing mount_point\n");
			goto err;
		}
		fstab->recs[cnt].mount_point = strdup(p);

		if (!(p = strtok_r(NULL, delim, &save_ptr))) {
			ERROR("Error parsing fs_type\n");
			goto err;
		}
		fstab->recs[cnt].fs_type = strdup(p);

		if (!(p = strtok_r(NULL, delim, &save_ptr))) {
			ERROR("Error parsing mount_flags\n");
			goto err;
		}

		fstab->recs[cnt].fs_options_unparsed = strdup(p);
		tmp_fs_options[0] = '\0';
		fstab->recs[cnt].flags = parse_flags(p, mount_flags, NULL,
						     tmp_fs_options,
						     FS_OPTIONS_LEN);

		/* fs_options are optional */
		if (tmp_fs_options[0]) {
			fstab->recs[cnt].fs_options = strdup(tmp_fs_options);
		} else {
			fstab->recs[cnt].fs_options = NULL;
		}

		if (!(p = strtok_r(NULL, delim, &save_ptr))) {
			ERROR("Error parsing fs_mgr_options\n");
			goto err;
		}

		fstab->recs[cnt].fs_mgr_flags_unparsed = strdup(p);
		fstab->recs[cnt].fs_mgr_flags = parse_flags(p, fs_mgr_flags,
							    &flag_vals, NULL,
							    0);

finish_rec:
		fstab->recs[cnt].key_loc = flag_vals.key_loc;
		fstab->recs[cnt].length = flag_vals.part_length;
		fstab->recs[cnt].label = flag_vals.label;
		fstab->recs[cnt].partnum = flag_vals.partnum;
		fstab->recs[cnt].swap_prio = flag_vals.swap_prio;
		fstab->recs[cnt].zram_size = flag_vals.zram_size;
		cnt++;
	}
	fclose(fstab_file);
	free(line);
	return fstab;

err:
	fclose(fstab_file);
	free(line);
	if (fstab)
		fs_mgr_free_fstab(fstab);
	return NULL;
}

struct fstab *fs_mgr_read_fstab(const char *fstab_path)
{
	return do_fs_mgr_read_fstab(fstab_path, 0);
}

void fs_mgr_free_fstab(struct fstab *fstab)
{
	int i;

	if (!fstab) {
		return;
	}

	for (i = 0; i < fstab->num_entries; i++) {
		/* Free the pointers return by strdup(3) */
		free(fstab->recs[i].blk_device);
		free(fstab->recs[i].mount_point);
		free(fstab->recs[i].fs_type);
		free(fstab->recs[i].fs_options);
		free(fstab->recs[i].key_loc);
		free(fstab->recs[i].label);
	}

	/* Free the fstab_recs array created by calloc(3) */
	free(fstab->recs);

	/* Free the fstab filename */
	free(fstab->fstab_filename);

	/* Free fstab */
	free(fstab);
}

void check_fs(char *blk_device, char *fs_type, char *target)
{
	int ret;
	long tmpmnt_flags = MS_NOATIME | MS_NOEXEC | MS_NOSUID;
	char *tmpmnt_opts = "nomblk_io_submit,errors=remount-ro";
	char *e2fsck_argv[] = {
		E2FSCK_BIN,
		"-y",
		blk_device,
		NULL
	};

	/* Check for the types of filesystems we know how to check */
	if (!strcmp(fs_type, "ext2") || !strcmp(fs_type, "ext3")
	    || !strcmp(fs_type, "ext4")) {
		/*
		 * First try to mount and unmount the filesystem.  We do this because
		 * the kernel is more efficient than e2fsck in running the journal and
		 * processing orphaned inodes, and on at least one device with a
		 * performance issue in the emmc firmware, it can take e2fsck 2.5 minutes
		 * to do what the kernel does in about a second.
		 *
		 * After mounting and unmounting the filesystem, run e2fsck, and if an
		 * error is recorded in the filesystem superblock, e2fsck will do a full
		 * check.  Otherwise, it does nothing.  If the kernel cannot mount the
		 * filesytsem due to an error, e2fsck is still run to do a full check
		 * fix the filesystem.
		 */
		ret =
		    mount(blk_device, target, fs_type, tmpmnt_flags,
			  tmpmnt_opts);
		if (!ret) {
			umount(target);
		}

		INFO("Running %s on %s\n", E2FSCK_BIN, blk_device);

		ret = do_exec(e2fsck_argv);

		if (ret) {
			/* No need to check for error in fork, we can't really handle it now */
			ERROR("Failed trying to run %s\n", E2FSCK_BIN);
		}
	}

	return;
}

static void remove_trailing_slashes(char *n)
{
	int len;

	len = strlen(n) - 1;
	while ((*(n + len) == '/') && len) {
		*(n + len) = '\0';
		len--;
	}
}

/*
 * Mark the given block device as read-only, using the BLKROSET ioctl.
 * Return 0 on success, and -1 on error.
 */
static void fs_set_blk_ro(const char *blockdev)
{
	int fd;
	int ON = 1;

	fd = open(blockdev, O_RDONLY);
	if (fd < 0) {
		// should never happen
		return;
	}

	ioctl(fd, BLKROSET, &ON);
	close(fd);
}

/*
 * __mount(): wrapper around the mount() system call which also
 * sets the underlying block device to read-only if the mount is read-only.
 * See "man 2 mount" for return values.
 */
static int __mount(const char *source, const char *target,
		   const char *filesystemtype, unsigned long mountflags,
		   const void *data)
{
	int ret = mount(source, target, filesystemtype, mountflags, data);

	if ((ret == 0) && (mountflags & MS_RDONLY) != 0) {
		fs_set_blk_ro(source);
	}

	return ret;
}

static int fs_match(char *in1, char *in2)
{
	char *n1;
	char *n2;
	int ret;

	n1 = strdup(in1);
	n2 = strdup(in2);

	remove_trailing_slashes(n1);
	remove_trailing_slashes(n2);

	ret = !strcmp(n1, n2);

	free(n1);
	free(n2);

	return ret;
}

/* If tmp_mount_point is non-null, mount the filesystem there.  This is for the
 * tmp mount we do to check the user password
 */
int fs_mgr_do_mount(struct fstab *fstab, char *n_name, char *n_blk_device,
		    char *tmp_mount_point)
{
	int i = 0;
	int ret = -1;
	char *m;

	if (!fstab) {
		return ret;
	}

	for (i = 0; i < fstab->num_entries; i++) {
		if (!fs_match(fstab->recs[i].mount_point, n_name)) {
			continue;
		}

		/* We found our match */
		/* If this swap or a raw partition, report an error */
		if (!strcmp(fstab->recs[i].fs_type, "swap") ||
		    !strcmp(fstab->recs[i].fs_type, "emmc") ||
		    !strcmp(fstab->recs[i].fs_type, "mtd")) {
			ERROR("Cannot mount filesystem of type %s on %s\n",
			      fstab->recs[i].fs_type, n_blk_device);
			goto out;
		}

		/* First check the filesystem if requested */
		if (fstab->recs[i].fs_mgr_flags & MF_WAIT) {
			wait_for_file(n_blk_device, WAIT_TIMEOUT);
		}

		if (fstab->recs[i].fs_mgr_flags & MF_CHECK) {
			check_fs(n_blk_device, fstab->recs[i].fs_type,
				 fstab->recs[i].mount_point);
		}

		/* Now mount it where requested */
		if (tmp_mount_point) {
			m = tmp_mount_point;
		} else {
			m = fstab->recs[i].mount_point;
		}
		if (__mount(n_blk_device, m, fstab->recs[i].fs_type,
			    fstab->recs[i].flags, fstab->recs[i].fs_options)) {
			ERROR
			    ("Cannot mount filesystem on %s at %s options: %s error: %s\n",
			     n_blk_device, m, fstab->recs[i].fs_options,
			     strerror(errno));
			goto out;
		} else {
			ret = 0;
			goto out;
		}
	}

	/* We didn't find a match, say so and return an error */
	ERROR("Cannot find mount point %s in fstab\n",
	      fstab->recs[i].mount_point);

out:
	return ret;
}

/*
 * key_loc must be at least PROPERTY_VALUE_MAX bytes long
 *
 * real_blk_device must be at least PROPERTY_VALUE_MAX bytes long
 */
int fs_mgr_get_crypt_info(struct fstab *fstab, char *key_loc,
			  char *real_blk_device, int size)
{
	int i = 0;

	if (!fstab) {
		return -1;
	}
	/* Initialize return values to null strings */
	if (key_loc) {
		*key_loc = '\0';
	}
	if (real_blk_device) {
		*real_blk_device = '\0';
	}

	/* Look for the encryptable partition to find the data */
	for (i = 0; i < fstab->num_entries; i++) {
		/* Don't deal with vold managed enryptable partitions here */
		if (fstab->recs[i].fs_mgr_flags & MF_VOLDMANAGED) {
			continue;
		}
		if (!(fstab->recs[i].fs_mgr_flags & MF_CRYPT)) {
			continue;
		}

		/* We found a match */
		if (key_loc) {
			strlcpy(key_loc, fstab->recs[i].key_loc, size);
		}
		if (real_blk_device) {
			strlcpy(real_blk_device, fstab->recs[i].blk_device,
				size);
		}
		break;
	}

	return 0;
}

/* Add an entry to the fstab, and return 0 on success or -1 on error */
int fs_mgr_add_entry(struct fstab *fstab,
		     const char *mount_point, const char *fs_type,
		     const char *blk_device, long long
		     __attribute__ ((unused)) length)
{
	struct fstab_rec *new_fstab_recs;
	int n = fstab->num_entries;

	new_fstab_recs = (struct fstab_rec *)
	    realloc(fstab->recs, sizeof(struct fstab_rec) * (n + 1));

	if (!new_fstab_recs) {
		return -1;
	}

	/* A new entry was added, so initialize it */
	memset(&new_fstab_recs[n], 0, sizeof(struct fstab_rec));
	new_fstab_recs[n].mount_point = strdup(mount_point);
	new_fstab_recs[n].fs_type = strdup(fs_type);
	new_fstab_recs[n].blk_device = strdup(blk_device);
	new_fstab_recs[n].length = 0;

	/* Update the fstab struct */
	fstab->recs = new_fstab_recs;
	fstab->num_entries++;

	return 0;
}

struct fstab_rec *fs_mgr_get_entry_for_mount_point(struct fstab *fstab,
						   const char *path)
{
	int i;

	if (!fstab) {
		return NULL;
	}

	for (i = 0; i < fstab->num_entries; i++) {
		int len = strlen(fstab->recs[i].mount_point);
		if (strncmp(path, fstab->recs[i].mount_point, len) == 0 &&
		    (path[len] == '\0' || path[len] == '/')) {
			return &fstab->recs[i];
		}
	}

	return NULL;
}

int fs_mgr_is_voldmanaged(struct fstab_rec *fstab)
{
	return fstab->fs_mgr_flags & MF_VOLDMANAGED;
}

int fs_mgr_is_nonremovable(struct fstab_rec *fstab)
{
	return fstab->fs_mgr_flags & MF_NONREMOVABLE;
}

int fs_mgr_is_encryptable(struct fstab_rec *fstab)
{
	return fstab->fs_mgr_flags & MF_CRYPT;
}

int fs_mgr_is_noemulatedsd(struct fstab_rec *fstab)
{
	return fstab->fs_mgr_flags & MF_NOEMULATEDSD;
}

int fs_mgr_is_wait(struct fstab_rec *fstab)
{
	return fstab->fs_mgr_flags & MF_WAIT;
}

int fs_mgr_is_multiboot(struct fstab_rec *fstab)
{
	return fstab->fs_mgr_flags & MF_MULTIBOOT;
}
