#ifndef _MB_COMMON_H_
#define _MB_COMMON_H_

#include <linux/limits.h>

#define LOG_TAG "multiboot"

#define PATH_MOUNTPOINT_SOURCE "/multiboot/source"
#define FILE_FSTAB "/multiboot/fstab"
#define FILE_RECOVERY_BINARY "/sbin/recovery"

#define FILE_VOLD_ANDROID "/system/bin/vold"
#define FILE_VOLD_RECOVERY "/sbin/minivold"
#define SOURCE_MOUNT_DATA "context=u:object_r:sdcard_external:s0"
#define CONTEXT_ID_FILE "/multiboot/.needs_context"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))

int system_is_recovery(void);
int system_is_sndstage(void);
int system_is_multiboot(void);

int can_init(void);
int mb_ready(void);
int needs_context(void);

void kperror(const char *message);
char *get_source_device(void);
char *get_multiboot_dir(void);
struct fstab *get_multiboot_fstab(void);
struct fstab_rec *get_fstab_rec(const char *devname);

void import_kernel_nv(char *name);
int common_early_init(void);
int common_late_init(void);

#endif
