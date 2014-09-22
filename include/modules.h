#ifndef _MODULES_H
#define _MODULES_H

#include <stdbool.h>
#include <tracy.h>

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

#define __define_module(m) \
	static void __used __attribute__ ((constructor)) __module_##fn##id(void) {module_register(&m);}

#define module_add(m) __define_module(m)

typedef enum { BOOTMODE_RECOVERY, BOOTMODE_ANDROID } bootmode_t;
typedef enum { INITSTAGE_NONE, INITSTAGE_EARLY, INITSTAGE_FSTAB,
	INITSTAGE_TRACY, INITSTAGE_LATE, INITSTAGE_HOOK
} initstage_t;

struct module_data {
	// early_init
	initstage_t initstage;
	bootmode_t bootmode;
	bool multiboot_enabled;
	bool sndstage_enabled;

	char *multiboot_device;
	char *multiboot_path;

	char *grub_device;
	char *grub_path;

	// fstab
	struct fstab *multiboot_fstab;
	struct fstab **target_fstabs;
	unsigned target_fstabs_count;

	// tracy_init
	struct tracy *tracy;
};

typedef int (*module_call_t) (struct module_data *);
typedef int (*module_tracy_hook_t) (struct module_data *,
				    struct tracy_event * e);

struct module {
	// nothing is mounted, but the main data is available
	module_call_t early_init;

	// all fstabs are loaded now
	module_call_t fstab_init;

	// add tracy hooks
	module_call_t tracy_init;

	// we got /dev
	module_call_t late_init;

	module_tracy_hook_t hook_mount;
};

void module_register(struct module *module);
const char *strbootmode(bootmode_t bm);

int modules_call_early_init(struct module_data *data);
int modules_call_fstab_init(struct module_data *data);
int modules_call_tracy_init(struct module_data *data);
int modules_call_late_init(struct module_data *data);

int modules_call_hook_mount(struct module_data *data, struct tracy_event *e);
#endif
