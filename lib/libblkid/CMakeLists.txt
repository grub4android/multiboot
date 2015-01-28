# common
cmake_minimum_required(VERSION 2.8)
project(blkid)
set(CMAKE_BUILD_TYPE Release)

# common cflags
set(CMAKE_C_FLAGS "-D_FILE_OFFSET_BITS=64 -DHAVE_LOFF_T -DHAVE_ERR_H -DHAVE_MEMPCPY -DHAVE_FSYNC -DHAVE_SYSCONF")

# util-linux
add_library(util-linux STATIC 
	lib/at.c
	lib/blkdev.c
	lib/canonicalize.c
	lib/colors.c
	lib/crc32.c
	lib/crc64.c
	lib/env.c
	lib/exec_shell.c
	lib/fileutils.c
	lib/ismounted.c
	lib/langinfo.c
	lib/linux_version.c
	lib/loopdev.c
	lib/mangle.c
	lib/match.c
	lib/mbsalign.c
	lib/md5.c
	lib/pager.c
	lib/path.c
	lib/procutils.c
	lib/randutils.c
	lib/setproctitle.c
	lib/strutils.c
	lib/sysfs.c
)
set_property(TARGET util-linux PROPERTY INCLUDE_DIRECTORIES
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_SOURCE_DIR}/src
)

# UUID
add_library(uuid STATIC 
	libuuid/src/clear.c
	libuuid/src/copy.c
	libuuid/src/isnull.c
	libuuid/src/parse.c
	libuuid/src/unpack.c
	libuuid/src/uuid_time.c
	libuuid/src/compare.c
	libuuid/src/gen_uuid.c
	libuuid/src/pack.c
	libuuid/src/test_uuid.c
	libuuid/src/unparse.c
)
set_property(TARGET uuid PROPERTY INCLUDE_DIRECTORIES
	${PROJECT_SOURCE_DIR}/libuuid/src
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(uuid util-linux)

# fdisk
add_library(fdisk STATIC
	libfdisk/src/alignment.c
	libfdisk/src/context.c 
	libfdisk/src/init.c
	libfdisk/src/partition.c
	libfdisk/src/sgi.c
	libfdisk/src/test.c
	libfdisk/src/ask.c
	libfdisk/src/dos.c
	libfdisk/src/iter.c
	libfdisk/src/parttype.c
	libfdisk/src/sun.c
	libfdisk/src/utils.c
	libfdisk/src/bsd.c
	libfdisk/src/gpt.c
	libfdisk/src/label.c
	libfdisk/src/script.c
	libfdisk/src/table.c
)
set_property(TARGET fdisk PROPERTY INCLUDE_DIRECTORIES
	${PROJECT_SOURCE_DIR}/libfdisk/src
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_SOURCE_DIR}/libuuid/src
	${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(fdisk util-linux uuid)

# blkid
add_library(blkid STATIC
	src/config.c
	src/dev.c
	src/devname.c
	src/devno.c
	src/encode.c
	src/evaluate.c
	src/getsize.c
	src/init.c
	src/llseek.c
	src/probe.c
	src/read.c
	src/resolve.c
	src/save.c
	src/tag.c
	src/verify.c
	src/version.c
	src/partitions/aix.c
	src/partitions/bsd.c
	src/partitions/dos.c
	src/partitions/gpt.c
	src/partitions/mac.c
	src/partitions/minix.c
	src/partitions/partitions.c
	src/partitions/sgi.c
	src/partitions/solaris_x86.c
	src/partitions/sun.c
	src/partitions/ultrix.c
	src/partitions/unixware.c
	src/superblocks/adaptec_raid.c 
	src/superblocks/bcache.c 
	src/superblocks/befs.c 
	src/superblocks/bfs.c
	src/superblocks/btrfs.c
	src/superblocks/cramfs.c
	src/superblocks/ddf_raid.c
	src/superblocks/drbd.c
	src/superblocks/drbdproxy_datalog.c
	src/superblocks/exfat.c
	src/superblocks/ext.c
	src/superblocks/f2fs.c
	src/superblocks/gfs.c
	src/superblocks/hfs.c
	src/superblocks/highpoint_raid.c
	src/superblocks/hpfs.c
	src/superblocks/iso9660.c
	src/superblocks/isw_raid.c
	src/superblocks/jfs.c
	src/superblocks/jmicron_raid.c
	src/superblocks/linux_raid.c
	src/superblocks/lsi_raid.c
	src/superblocks/luks.c
	src/superblocks/lvm.c
	src/superblocks/minix.c
	src/superblocks/netware.c
	src/superblocks/nilfs.c
	src/superblocks/ntfs.c
	src/superblocks/nvidia_raid.c
	src/superblocks/ocfs.c
	src/superblocks/promise_raid.c
	src/superblocks/refs.c
	src/superblocks/reiserfs.c
	src/superblocks/romfs.c
	src/superblocks/silicon_raid.c
	src/superblocks/squashfs.c
	src/superblocks/superblocks.c
	src/superblocks/swap.c
	src/superblocks/sysv.c
	src/superblocks/ubifs.c
	src/superblocks/udf.c
	src/superblocks/ufs.c
	src/superblocks/vfat.c
	src/superblocks/via_raid.c
	src/superblocks/vmfs.c
	src/superblocks/vxfs.c
	src/superblocks/xfs.c
	src/superblocks/zfs.c
	src/topology/dm.c
	src/topology/evms.c
	src/topology/ioctl.c
	src/topology/lvm.c
	src/topology/md.c
	src/topology/sysfs.c
	src/topology/topology.c
)
set_property(TARGET blkid PROPERTY INCLUDE_DIRECTORIES
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(blkid util-linux)
