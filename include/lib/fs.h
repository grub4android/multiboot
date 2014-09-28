#ifndef _LIB_FS_H_
#define _LIB_FS_H_

#include <stdlib.h>
#include <stdbool.h>

int fs_pre(struct fd_info *fdi);
bool fs_was_format(struct fd_info *fdi);
int fs_cleanup(struct fd_info *fdi);

#endif
