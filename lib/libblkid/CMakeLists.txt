# common
cmake_minimum_required(VERSION 2.8)
project(blkid)
set(CMAKE_BUILD_TYPE Release)

# common cflags
set(CMAKE_C_FLAGS "-Wall -Wextra -Wshadow -fPIC -static-libgcc -Wl,-static")

# blkid defines
set(BLKID_SOURCES
	aix.c
	at.c
	befs.c
	bfs.c
	blkdev.c
	bsd.c
	btrfs.c
	cache.c
	canonicalize.c
	colors.c
	config.c
	cramfs.c
	crc32.c
	ddf_raid.c
	dev.c
	devname.c
	devno.c
	dm.c
	dos.c
	drbd.c
	drbdproxy_datalog.c
	encode.c
	env.c
	evaluate.c
	evms.c
	exec_shell.c
	exfat.c
	ext.c
	f2fs.c
	fileutils.c
	getsize.c
	gfs.c
	gpt.c
	hfs.c
	highpoint_raid.c
	hpfs.c
	ioctl.c
	ismounted.c
	iso9660.c
	isw_raid.c
	jfs.c
	jmicron_raid.c
	langinfo.c
	linux_raid.c
	linux_version.c
	llseek.c
	loopdev.c
	lsi_raid.c
	luks.c
	lvm1.c
	lvm2.c
	mac.c
	mangle.c
	match.c
	mbsalign.c
	md5.c
	md.c
	minix1.c
	minix2.c
	netware.c
	nilfs.c
	ntfs.c
	nvidia_raid.c
	ocfs.c
	pager.c
	partitions.c
	path.c
	probe.c
	procutils.c
	promise_raid.c
	randutils.c
	read.c
	reiserfs.c
	resolve.c
	romfs.c
	save.c
	setproctitle.c
	sgi.c
	silicon_raid.c
	solaris_x86.c
	squashfs.c
	sun.c
	superblocks.c
	swap.c
	sysfs1.c
	sysfs2.c
	sysv.c
	tag.c
	topology.c
	
	ubifs.c
	udf.c
	ufs.c
	ultrix.c
	unixware.c
	verify.c
	version.c
	vfat.c
	via_raid.c
	vmfs.c
	vxfs.c
	wholedisk.c
	xfs.c
	zfs.c
	adaptec_raid.c
)
add_library(blkid STATIC ${BLKID_SOURCES})

set_property(TARGET blkid PROPERTY INCLUDE_DIRECTORIES
	${PROJECT_SOURCE_DIR}
)
