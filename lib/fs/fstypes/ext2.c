#include <common.h>

struct ext2_super_block {
	uint32_t s_inodes_count;
	uint32_t s_blocks_count;
	uint32_t s_r_blocks_count;
	uint32_t s_free_blocks_count;
	uint32_t s_free_inodes_count;
	uint32_t s_first_data_block;
	uint32_t s_log_block_size;
	uint32_t s_dummy3[7];
	unsigned char s_magic[2];
	uint16_t s_state;
	uint16_t s_errors;
	uint16_t s_minor_rev_level;
	uint32_t s_lastcheck;
	uint32_t s_checkinterval;
	uint32_t s_creator_os;
	uint32_t s_rev_level;
	uint16_t s_def_resuid;
	uint16_t s_def_resgid;
	uint32_t s_first_ino;
	uint16_t s_inode_size;
	uint16_t s_block_group_nr;
	uint32_t s_feature_compat;
	uint32_t s_feature_incompat;
	uint32_t s_feature_ro_compat;
	unsigned char s_uuid[16];
	char s_volume_name[16];
	char s_last_mounted[64];
	uint32_t s_algorithm_usage_bitmap;
	uint8_t s_prealloc_blocks;
	uint8_t s_prealloc_dir_blocks;
	uint16_t s_reserved_gdt_blocks;
	uint8_t s_journal_uuid[16];
	uint32_t s_journal_inum;
	uint32_t s_journal_dev;
	uint32_t s_last_orphan;
	uint32_t s_hash_seed[4];
	uint8_t s_def_hash_version;
	uint8_t s_jnl_backup_type;
	uint16_t s_reserved_word_pad;
	uint32_t s_default_mount_opts;
	uint32_t s_first_meta_bg;
	uint32_t s_mkfs_time;
	uint32_t s_jnl_blocks[17];
	uint32_t s_blocks_count_hi;
	uint32_t s_r_blocks_count_hi;
	uint32_t s_free_blocks_hi;
	uint16_t s_min_extra_isize;
	uint16_t s_want_extra_isize;
	uint32_t s_flags;
	uint16_t s_raid_stride;
	uint16_t s_mmp_interval;
	uint64_t s_mmp_block;
	uint32_t s_raid_stripe_width;
	uint32_t s_reserved[163];
} __attribute__ ((packed));

/* magic string */
#define EXT_SB_MAGIC				"\123\357"
/* supper block offset */
#define EXT_SB_OFF				0x400
/* supper block offset in kB */
#define EXT_SB_KBOFF				(EXT_SB_OFF >> 10)
/* magic string offset within super block */
#define EXT_MAG_OFF				0x38

struct ext2_pdata {
	struct ext2_super_block sb_pre;
};

int ext2_pre(struct fd_info *fdi)
{
	int fd = open(fdi->device, O_RDONLY);
	if (!fd) {
		kperror("open");
		return false;
	}

	struct ext2_pdata *pdata = fdi->fs_pdata =
	    malloc(sizeof(struct ext2_pdata));
	lseek(fd, EXT_SB_OFF, SEEK_SET);
	if (read(fd, &pdata->sb_pre, sizeof(pdata->sb_pre)) !=
	    sizeof(pdata->sb_pre)) {
		kperror("read");
		return false;
	}
	ERROR("%s: lastcheck=%d created=%d\n", __func__,
	      pdata->sb_pre.s_lastcheck, pdata->sb_pre.s_mkfs_time);

	close(fd);
	return 0;
}

bool ext2_was_format(struct fd_info * fdi)
{
	bool rc = false;
	int fd;

	if (!fdi->fs_pdata)
		return false;
	struct ext2_pdata *pdata = fdi->fs_pdata;

	fd = open(fdi->device, O_RDONLY);
	if (!fd) {
		kperror("open");
		return false;
	}

	struct ext2_super_block sb;
	lseek(fd, EXT_SB_OFF, SEEK_SET);
	if (read(fd, &sb, sizeof(sb)) != sizeof(sb)) {
		kperror("read");
		return false;
	}

	if (sb.s_mkfs_time > pdata->sb_pre.s_mkfs_time) {
		ERROR("%s: mkfs: %d>%d\n", __func__, sb.s_mkfs_time,
		      pdata->sb_pre.s_mkfs_time);
		rc = true;
	} else if (sb.s_lastcheck < pdata->sb_pre.s_lastcheck) {
		ERROR("%s: lastcheck: %d<%d\n", __func__, sb.s_lastcheck,
		      pdata->sb_pre.s_lastcheck);
		rc = true;
	}

	close(fd);
	return rc;
}

int ext2_cleanup(struct fd_info *fdi)
{
	if (fdi->fs_pdata) {
		free(fdi->fs_pdata);
		fdi->fs_pdata = NULL;
	}

	return true;
}
