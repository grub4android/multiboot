cmake_minimum_required(VERSION 2.8)

project(multiboot)
set(CMAKE_BUILD_TYPE Release)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include ${STRACE_BIN_DIR} ${STRACE_SRC_DIR})
set(CMAKE_C_FLAGS "-static-libgcc -Wl,-static -DHAVE_CONFIG_H")
add_library(multiboot SHARED src/util.c src/multiboot.c)
