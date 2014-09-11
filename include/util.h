#ifndef _UTIL_H
#define _UTIL_H

#include <tracy.h>

int mkpath(const char *s, mode_t mode);
int system_is_recovery(void);
int can_init(void);
char *get_patharg(struct tracy_child *child, long addr, int real);
tracy_child_addr_t copy_patharg(struct tracy_child *child, const char *path);
void free_patharg(struct tracy_child *child, tracy_child_addr_t addr);
#endif
