#ifndef _MODULES_H
#define _MODULES_H

#include <common.h>
#include <stdbool.h>
#include <tracy.h>

#define __define_module(m) \
	static void __used __attribute__ ((constructor)) __module_##fn##id(void) {module_register(&m);}

#define module_add(m) __define_module(m)

typedef enum { BOOTMODE_RECOVERY, BOOTMODE_ANDROID } bootmode_t;
typedef enum { INITSTAGE_NONE, INITSTAGE_EARLY, INITSTAGE_FSTAB,
	INITSTAGE_TRACY
} initstage_t;

struct module_data {
	// early_init
	initstage_t initstage;
	bootmode_t bootmode;
	bool multiboot_enabled;
	bool sndstage_enabled;
	char *hw_name;

	char *multiboot_path;
	struct fstab_rec multiboot_device;

	char *grub_path;
	struct fstab_rec grub_device;
	struct sys_block_uevent *grub_blockinfo;

	// fstab
	struct fstab *multiboot_fstab;
	struct fstab **target_fstabs;
	unsigned target_fstabs_count;
	struct sys_block_info *block_info;

	// tracy_init
	struct tracy *tracy;
};

typedef int (*module_call_t) (struct module_data *);
typedef int (*module_call_tracy_child_t) (struct module_data *,
					  struct tracy_child *);

struct module {
	// nothing is mounted, but the main data is available
	// we have private /dev already
	module_call_t early_init;

	// all fstabs are loaded now
	module_call_t fstab_init;

	// add tracy hooks
	module_call_t tracy_init;

	module_call_tracy_child_t tracy_child_create;
	module_call_tracy_child_t tracy_child_destroy;
};

void module_register(struct module *module);
const char *strbootmode(bootmode_t bm);

int modules_call_early_init(struct module_data *data);
int modules_call_fstab_init(struct module_data *data);
int modules_call_tracy_init(struct module_data *data);
int modules_call_tracy_child_create(struct module_data *data,
				    struct tracy_child *child);
int modules_call_tracy_child_destroy(struct module_data *data,
				     struct tracy_child *child);
#endif
