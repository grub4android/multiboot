cmake_minimum_required(VERSION 2.8)

# common
project(multiboot)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_C_FLAGS "-Wall -Wextra -Werror -Wshadow -fPIC -static-libgcc -Wl,-static")

# static libs
ADD_LIBRARY(tracy STATIC IMPORTED)
SET_TARGET_PROPERTIES(tracy PROPERTIES
    IMPORTED_LOCATION ${TRACY_BIN_DIR}/libtracy.a)

# main
add_executable(init
	src/modules.c
	src/multiboot_init.c
	src/util.c
	src/common.c

	src/modules/fstab_patcher.c
	src/modules/env_prepare.c
	src/modules/fs_redirection.c

	lib/klog.c
	lib/uevent.c
	lib/cmdline.c
	lib/fs_mgr/fs_mgr.c
)
set_property(TARGET init PROPERTY INCLUDE_DIRECTORIES
	${TRACY_SRC_DIR}
	${TRACY_BIN_DIR}/include
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_SOURCE_DIR}/lib/libblkid
)
target_link_libraries(init tracy pthread blkid)

add_subdirectory(lib/libblkid)
