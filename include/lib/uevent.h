#ifndef _LIB_UEVENT_H_
#define _LIB_UEVENT_H_

typedef enum { UEVENT_TYPE_UNKNOWN, UEVENT_TYPE_DISK,
	UEVENT_TYPE_PARTITION
} uevent_type_t;

struct sys_block_uevent {
	unsigned major;
	unsigned minor;
	unsigned mmc_major;
	unsigned mmc_minor;
	char *devname;
	char *partname;
	uevent_type_t type;
};

struct sys_block_info {
	int num_entries;
	struct sys_block_uevent *entries;
};

struct sys_block_info *get_block_devices(void);
void free_block_devices(struct sys_block_info *info);
struct sys_block_uevent *get_blockinfo_for_path(struct sys_block_info *info,
						const char *path);

#endif
