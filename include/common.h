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

#include <lib/cmdline.h>
#include <lib/fs_mgr.h>
#include <lib/uevent.h>
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

#define ERROR(x...)   KLOG_ERROR(LOG_TAG, x)
#define WARNING(x...) KLOG_WARNING(LOG_TAG, x)
#define NOTICE(x...)  KLOG_NOTICE(LOG_TAG, x)
#define INFO(x...)    KLOG_INFO(LOG_TAG, x)
#define DEBUG(x...)   KLOG_DEBUG(LOG_TAG, x)

void kperror(const char *message);

#endif
