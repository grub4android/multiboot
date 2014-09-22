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

#include <lib/cmdline.h>
#include <lib/fs_mgr.h>
#include <lib/klog.h>
#include <util.h>
#include <modules.h>

#include <tracy.h>
#include <ll.h>

#define LOG_TAG "multiboot"

#define PATH_MOUNTPOINT_SOURCE "/multiboot/source"
#define PATH_MOUNTPOINT_GRUB "/multiboot/grub"
#define PATH_MOUNTPOINT_DEV "/multiboot/dev"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))

void kperror(const char *message);

#endif
