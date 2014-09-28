#ifndef _MB_COMMON_H_
#define _MB_COMMON_H_

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
#include <libgen.h>
#include <fcntl.h>
#include <ctype.h>
#include <limits.h>
#include <selinux/selinux.h>

typedef int64_t off64_t;

struct multiboot_child_data;
struct fd_info;

#include <tracy.h>
#include <ll.h>

#include <lib/cmdline.h>
#include <lib/fs_mgr.h>
#include <lib/uevent.h>
#include <lib/klog.h>
#include <lib/fs.h>
#include <blkid.h>
#include <util.h>
#include <modules.h>

#if __GNUC__ == 3

#if __GNUC_MINOR__ >= 3
#define __used                 __attribute__((__used__))
#else
#define __used                 __attribute__((__unused__))
#endif

#else
#if __GNUC__ == 4
#define __used                 __attribute__((__used__))
#endif
#endif

#define LOG_TAG "multiboot"

#define PATH_MOUNTPOINT_SOURCE "/multiboot/mnt/source"
#define PATH_MOUNTPOINT_GRUB "/multiboot/mnt/grub"
#define PATH_MOUNTPOINT_DEV "/multiboot/dev"
#define PATH_MOUNTPOINT_STUBFS "/multiboot/stubfs"
#define PATH_MOUNTPOINT_BOOTLOADER "/bootloader"

#define PATH_MULTIBOOT_SBIN "/multiboot/sbin"
#define PATH_MULTIBOOT_BUSYBOX PATH_MULTIBOOT_SBIN "/busybox"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))

#define ERROR(x...)   KLOG_ERROR(LOG_TAG, x)
#define WARNING(x...) KLOG_WARNING(LOG_TAG, x)
#define NOTICE(x...)  KLOG_NOTICE(LOG_TAG, x)
#define INFO(x...)    KLOG_INFO(LOG_TAG, x)
#define DEBUG(x...)   KLOG_DEBUG(LOG_TAG, x)

struct multiboot_child_data {
	tracy_child_addr_t memory;
	char *tmp;

	// fs_redirection
	struct tracy_ll *files;
	bool handled_by_open;
};

struct fd_info {
	struct tracy_child *child;
	unsigned int fd;
	char *filename;
	char *fs_type;
	void *fs_pdata;

	char *device;
};

void kperror(const char *message);

#endif
