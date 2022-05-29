// SPDX-License-Identifier: GPL-2.0
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018 Redha Gouicem <redha.gouicem@lip6.fr>
 */

#define pr_fmt(fmt) "ouichefs: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/blkdev.h>
#include <linux/writeback.h>

#include "ouichefs.h"
#include "bitmap.h"

/*
 * Map the buffer_head passed in argument with the iblock-th block of the file
 * represented by inode. If the requested block is not allocated and create is
 * true,  allocate a new block on disk and map it.
 */
static int ouichefs_file_get_block(struct inode *inode, sector_t iblock,
				   struct buffer_head *bh_result, int create)
{
	struct super_block *sb = inode->i_sb;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct ouichefs_file_index_block *index;
	struct buffer_head *bh_index;
	bool alloc = false;
	int ret = 0, bno;

	/* If block number exceeds filesize, fail */
	if (iblock >= OUICHEFS_BLOCK_SIZE >> 2)
		return -EFBIG;

	/* Read index block from disk */
	bh_index = sb_bread(sb, ci->index_block);
	if (!bh_index)
		return -EIO;
	index = (struct ouichefs_file_index_block *)bh_index->b_data;
	/*
	 * Check if iblock is already allocated. If not and create is true,
	 * allocate it. Else, get the physical block number.
	 */
	if (index->blocks[iblock] == 0) {
		if (!create)
			return 0;
		bno = get_free_block(sbi);
		if (!bno) {
			ret = -ENOSPC;
			goto brelse_index;
		}
		index->blocks[iblock] = bno;
		alloc = true;
	} else {
		bno = index->blocks[iblock];
	}

	/* Map the physical block to to the given buffer_head */
	map_bh(bh_result, sb, bno);

brelse_index:
	brelse(bh_index);

	return ret;
}

/*
 * Called by the page cache to read a page from the physical disk and map it in
 * memory.
 */
static int ouichefs_readpage(struct file *file, struct page *page)
{
	return mpage_readpage(page, ouichefs_file_get_block);
}

/*
 * Called by the page cache to write a dirty page to the physical disk (when
 * sync is called or when memory is needed).
 */
static int ouichefs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, ouichefs_file_get_block, wbc);
}

/*
 * Called by the VFS when a write() syscall occurs on file before writing the
 * data in the page cache. This functions checks if the write will be able to
 * complete and allocates the necessary blocks through block_write_begin().
 */
static int ouichefs_write_begin(struct file *file,
				struct address_space *mapping, loff_t pos,
				unsigned int len, unsigned int flags,
				struct page **pagep, void **fsdata)
{
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(file->f_inode->i_sb);
	int err;
	struct inode *inode = file->f_inode;
	uint32_t nr_allocs = 0;
	struct buffer_head *bh_index, *bh_new_index;
	struct buffer_head *bh_new_data_block, *bh_data_block;
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct super_block *sb = inode->i_sb;
	struct ouichefs_file_index_block *index, *new_index;
	uint32_t new_index_no, new_block_no;
	int i;

	/* Check if the write can be completed (enough space?) */
	if (pos + len > OUICHEFS_MAX_FILESIZE)
		return -ENOSPC;
	nr_allocs = max(pos + len, file->f_inode->i_size) / OUICHEFS_BLOCK_SIZE;
	if (nr_allocs > file->f_inode->i_blocks - 1)
		nr_allocs -= file->f_inode->i_blocks - 1;
	else
		nr_allocs = 0;
	if (nr_allocs > sbi->nr_free_blocks)
		return -ENOSPC;

	if (ci->index_block != ci->last_index_block) { 
	 	pr_err("Unable to write to old version!\n");
		return -EINVAL;
	}	

	if ((long long int) file->private_data == -1) {
		file->private_data = (void *) pos;
		/* Duplicate index block and data */
		bh_index = sb_bread(sb, ci->index_block);
		if (!bh_index)
			return -EIO;
		index = (struct ouichefs_file_index_block *)bh_index->b_data;

		new_index_no = get_free_block(sbi);
		if (!new_index_no)
			return -ENOSPC;

		bh_new_index = sb_bread(sb, new_index_no);
		new_index = (struct ouichefs_file_index_block *)bh_new_index->b_data;
		memset(new_index, 0, sizeof(*new_index));

		for (i = 0; i < sizeof(index->blocks) / sizeof(uint32_t); i++) {
			if (index->blocks[i] == 0)
				continue;

			new_block_no = get_free_block(sbi);
			if (!new_block_no)
				return -ENOSPC;
			pr_info("Duplicating block %x to %x\n", index->blocks[i], new_block_no);

			bh_new_data_block = sb_bread(sb, new_block_no);
			bh_data_block = sb_bread(sb, index->blocks[i]);
			memcpy(bh_new_data_block->b_data, bh_data_block->b_data,
					OUICHEFS_BLOCK_SIZE);
			mark_buffer_dirty(bh_new_data_block);
			sync_dirty_buffer(bh_new_data_block);
			brelse(bh_new_data_block);
			brelse(bh_data_block);
			//file->private_data = bh_new_data_block;
			new_index->blocks[i] = new_block_no;
		}

		new_index->own_block_number = new_index_no;
		new_index->previous_block_number = ci->index_block;
		ci->index_block = new_index_no;
		ci->last_index_block = new_index_no;
		mark_buffer_dirty(bh_new_index);
		mark_buffer_dirty(bh_index);
		sync_dirty_buffer(bh_new_index);
		sync_dirty_buffer(bh_index);
		brelse(bh_index);
		brelse(bh_new_index);
	}

	/* prepare the write */
	err = block_write_begin(mapping, pos, len, flags, pagep,
				ouichefs_file_get_block);
	/* if this failed, reclaim newly allocated blocks */
	if (err < 0) {
		pr_err("%s:%d: newly allocated blocks reclaim not implemented yet\n",
		       __func__, __LINE__);
	}
	return err;
}

/*
 * Called by the VFS after writing data from a write() syscall to the page
 * cache. This functions updates inode metadata and truncates the file if
 * necessary.
 */
static int ouichefs_write_end(struct file *file, struct address_space *mapping,
			      loff_t pos, unsigned int len, unsigned int copied,
			      struct page *page, void *fsdata)
{
	int ret, i;
	struct inode *inode = file->f_inode;
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct super_block *sb = inode->i_sb;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct buffer_head *bh_index, *bh_prev_index;
	uint32_t index_block, prev_index_block, cur_no, prev_no;
	struct ouichefs_file_index_block *index, *prev_index;
	int64_t write_pos = (int64_t) file->private_data;

	if (write_pos != -1 && write_pos < pos) {
		index_block = ci->last_index_block;
	
		bh_index = sb_bread(sb, index_block);
		if (!bh_index)
			return -EIO;

		index = (struct ouichefs_file_index_block *)bh_index->b_data;
		prev_index_block = index->previous_block_number;

		bh_prev_index = sb_bread(sb, prev_index_block);
		if (!bh_prev_index) 
			return -EIO;
		prev_index = (struct ouichefs_file_index_block *)bh_prev_index->b_data;
		for(i = 0; (i + 1) * OUICHEFS_BLOCK_SIZE < write_pos; i++) {
			cur_no = index->blocks[i];
			prev_no = prev_index->blocks[i];
			if((cur_no == 0) || (prev_no == 0))
				continue;
			if(cur_no == prev_no)
				continue;

			pr_info("Delete block %x\n", cur_no);
			index->blocks[i] = prev_no;
			put_block(sbi, cur_no);
			mark_buffer_dirty(bh_index);
			sync_dirty_buffer(bh_index);
		}
		brelse(bh_prev_index);
		brelse(bh_index);
	}

	/* Complete the write() */
	ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);

	if (ret < len) {
		pr_err("%s:%d: wrote less than asked... what do I do? nothing for now...\n",
		       __func__, __LINE__);
	} else {
		uint32_t nr_blocks_old = inode->i_blocks;

		/* Update inode metadata */
		inode->i_blocks = inode->i_size / OUICHEFS_BLOCK_SIZE + 2;
		inode->i_mtime = inode->i_ctime = current_time(inode);
		mark_inode_dirty(inode);
		write_inode_now(inode, 1);

		/* If file is smaller than before, free unused blocks */
		if (nr_blocks_old > inode->i_blocks) {
			int i;
			struct buffer_head *bh_index;
			struct ouichefs_file_index_block *index;

			/* Free unused blocks from page cache */
			truncate_pagecache(inode, inode->i_size);

			/* Read index block to remove unused blocks */
			bh_index = sb_bread(sb, ci->index_block);
			if (!bh_index) {
				pr_err("failed truncating '%s'. we just lost %llu blocks\n",
				       file->f_path.dentry->d_name.name,
				       nr_blocks_old - inode->i_blocks);
				goto end;
			}
			index = (struct ouichefs_file_index_block *)
				bh_index->b_data;

			for (i = inode->i_blocks - 1; i < nr_blocks_old - 1;
			     i++) {
				put_block(OUICHEFS_SB(sb), index->blocks[i]);
				index->blocks[i] = 0;
			}
			mark_buffer_dirty(bh_index);
			brelse(bh_index);
		}
	}

end:
	return ret;
}

int ouichefs_change_file_version(struct file *file, int version)
{
	struct super_block *sb = file->f_inode->i_sb;
	struct ouichefs_file_index_block *index_block;
	struct buffer_head *bh;
	struct ouichefs_inode_info *info = OUICHEFS_INODE(file->f_inode);
	uint32_t current_version_block = info->last_index_block;

	while (version > 0) {
		bh = sb_bread(sb, current_version_block);
		index_block = (struct ouichefs_file_index_block *)bh->b_data;
		current_version_block = index_block->previous_block_number;
		version--;
	}
	info->index_block = current_version_block;

	mark_inode_dirty(file->f_inode);
//	invalidate_inode_buffers(file->f_inode);
	invalidate_mapping_pages(file->f_inode->i_mapping, 0, -1);
	return 0;
}

int ouichefs_restore_file_version(struct file *file)
{
	struct ouichefs_inode_info *info = OUICHEFS_INODE(file->f_inode);
	struct buffer_head *bh_index, *bh_prev_index;
	struct super_block *sb = file->f_inode->i_sb;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct ouichefs_file_index_block *index, *prev_index;
	uint32_t current_version_block = info->last_index_block;
	uint32_t prev_version_block;
	int i;

	while (current_version_block != info->index_block) {
		bh_index = sb_bread(sb, current_version_block);
		if (!bh_index)
			return -EIO;
		index = (struct ouichefs_file_index_block *)bh_index->b_data;

		prev_version_block = index->previous_block_number;

		if (prev_version_block != 0) {
			bh_prev_index = sb_bread(sb, prev_version_block);
			if (!bh_prev_index)
				return -EIO;
			prev_index = (struct ouichefs_file_index_block *)bh_prev_index->b_data;
		}
		
		for (i = 0; i < sizeof(index->blocks) / sizeof(uint32_t); i++) {
			if (index->blocks[i] == 0)
				continue;
			if (prev_version_block != 0 && index->blocks[i] == prev_index->blocks[i])
				continue;
			put_block(sbi, index->blocks[i]);
		}
		put_block(sbi, current_version_block);
		current_version_block = index->previous_block_number;
		brelse(bh_index);
		
		if (prev_version_block != 0) {
			brelse(bh_prev_index);
		}
	}
	info->last_index_block = info->index_block;
	return 0;
}

long ouichefs_ioctl(struct file *file, unsigned int cmd, unsigned long argp)
{
	switch (cmd) {
	case (OUICHEFS_SHOW_VERSION):
		return ouichefs_change_file_version(file, argp);
	case (OUICHEFS_RESTORE_VERSION):
		return ouichefs_restore_file_version(file);
	default:
		return -EINVAL;
	}
	return 0;
}

ssize_t file_write_iter(struct kiocb *kiocb, struct iov_iter *iov)
{
	kiocb->ki_filp->private_data = (void *) -1;
	return generic_file_write_iter(kiocb, iov);
}

const struct address_space_operations ouichefs_aops = {
	.readpage    = ouichefs_readpage,
	.writepage   = ouichefs_writepage,
	.write_begin = ouichefs_write_begin,
	.write_end   = ouichefs_write_end
};

const struct file_operations ouichefs_file_ops = {
	.owner      = THIS_MODULE,
	.llseek     = generic_file_llseek,
	.read_iter  = generic_file_read_iter,
	.write_iter = file_write_iter,
	.unlocked_ioctl = ouichefs_ioctl
};
