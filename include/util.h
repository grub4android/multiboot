#ifndef _UTIL_H
#define _UTIL_H

#include <tracy.h>

char *get_process_name_by_pid(const int pid);
int mkpath(const char *s, mode_t mode);
int util_mount(const char *source, const char *target,
	       const char *filesystemtype, unsigned long mountflags,
	       const void *data);
char *make_loop(const char *path);
size_t strlcpy(char *dst, const char *src, size_t dstsize);
size_t strlcat(char *dst, const char *src, size_t dstsize);
char *get_patharg(struct tracy_child *child, long addr, int real);
tracy_child_addr_t copy_patharg(struct tracy_child *child, const char *path);
void free_patharg(struct tracy_child *child, tracy_child_addr_t addr);
int do_exec(char **args);
int createRawImage(const char *source, const char *target);
int set_loop(char *device, char *file, int ro);
int copy_file(char *source, char *target);
#endif
