cmake_minimum_required(VERSION 2.8)

# common
project(multiboot)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_C_FLAGS "-Wall -Wextra -Werror -fPIC -static-libgcc -Wl,-static")

# static libs
ADD_LIBRARY(tracy STATIC IMPORTED)
SET_TARGET_PROPERTIES(tracy PROPERTIES
    IMPORTED_LOCATION ${TRACY_BIN_DIR}/libtracy.a)

# main
add_executable(init
	src/multiboot_init.c
	src/util.c

	src/mb_common.c
	src/mb_fstab_patcher.c
	src/mb_fs_redirection.c
	#src/mb_vold.c

	src/lib/klog.c
	src/lib/cmdline.c
	src/lib/fs_mgr/fs_mgr.c
)
set_property(TARGET init PROPERTY INCLUDE_DIRECTORIES
	${TRACY_SRC_DIR}
	${TRACY_BIN_DIR}/include
	${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(init tracy pthread)


