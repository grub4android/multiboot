#include <common.h>

#define MAX_MODULES 64

static struct module *modules[MAX_MODULES];
static int modules_count = 0;

void module_register(struct module *module)
{
	modules[modules_count++] = module;
}

const char *strbootmode(bootmode_t bm)
{
	switch (bm) {
	case BOOTMODE_RECOVERY:
		return "RECOVERY";
	case BOOTMODE_ANDROID:
		return "ANDROID";
	default:
		return "UNKNOWN";
	}
}

int modules_call_hook_mount(struct module_data *data, struct tracy_event *e)
{
	int i, rc = 0;

	DEBUG("%s\n", __func__);
	for (i = 0; i < modules_count && !rc; i++) {
		if (modules[i] && modules[i]->hook_mount) {
			rc = modules[i]->hook_mount(data, e);
		}
	}

	return rc;
}

#define DECLARE_INIT_FN(stage) \
int modules_call_##stage(struct module_data *data) \
{ \
	int i, rc = 0; \
 \
	DEBUG("%s\n", __func__); \
	for (i = 0; i < modules_count && !rc; i++) { \
		if (modules[i] && modules[i]->stage) { \
			rc = modules[i]->stage(data); \
		} \
	} \
 \
	return rc; \
}

DECLARE_INIT_FN(early_init);
DECLARE_INIT_FN(fstab_init);
DECLARE_INIT_FN(tracy_init);
DECLARE_INIT_FN(late_init);
