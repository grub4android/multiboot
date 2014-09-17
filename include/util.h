#ifndef _UTIL_H
#define _UTIL_H

#include <tracy.h>

char *get_process_name_by_pid(const int pid);
int mkpath(const char *s, mode_t mode);
int system_is_recovery(void);
int can_init(void);
size_t strlcpy(char *dst, const char *src, size_t dstsize);
size_t strlcat(char *dst, const char *src, size_t dstsize);
char *get_patharg(struct tracy_child *child, long addr, int real);
tracy_child_addr_t copy_patharg(struct tracy_child *child, const char *path);
void free_patharg(struct tracy_child *child, tracy_child_addr_t addr);
int do_exec(char **args);
int createRawImage(const char *source, const char *target);
int set_loop(char *device, char *file, int ro);
#endif
