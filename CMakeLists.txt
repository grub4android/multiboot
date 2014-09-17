cmake_minimum_required(VERSION 2.8)

# common
project(multiboot)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_C_FLAGS "-Wall -Wextra -Werror -fPIC -static-libgcc -Wl,-static")

# main
add_executable(init
	src/multiboot_init.c
	src/util.c
	src/lib/cmdline.c
	src/lib/fs_mgr/fs_mgr.c
	src/lib/klog.c
)
set_property(TARGET init PROPERTY INCLUDE_DIRECTORIES
	${TRACY_SRC_DIR}
	${TRACY_BIN_DIR}/include
	${CMAKE_SOURCE_DIR}/include
)

# dependencies
ADD_LIBRARY(tracy STATIC IMPORTED)
SET_TARGET_PROPERTIES(tracy PROPERTIES
    IMPORTED_LOCATION ${TRACY_BIN_DIR}/libtracy.a)
TARGET_LINK_LIBRARIES(init tracy pthread)
