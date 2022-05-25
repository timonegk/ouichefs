// SPDX-License-Identifier: GPL-2.0
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018  Redha Gouicem <redha.gouicem@lip6.fr>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/buffer_head.h>

#include "ouichefs.h"

static int debug_seq_show(struct seq_file *file, void *v)
{
	int ino;
	struct super_block *sb = (struct super_block *)file->private;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct inode *inode;
	struct ouichefs_inode_info *ci;
	uint32_t first_block_no, cur_block_no;
	struct buffer_head *bh;
	int count;
	struct ouichefs_file_index_block *index;
	char c[100];

	for (ino = 0; ino < sbi->nr_inodes; ino++) {
		inode = ouichefs_iget(sb, ino);
		ci = OUICHEFS_INODE(inode);

		if (inode->i_nlink == 0)
			goto iput;

		if (!S_ISREG(inode->i_mode))
			goto iput;

		count = 0;

		memset(c, 0, sizeof(c));

		first_block_no = ci->last_index_block;
		cur_block_no = first_block_no;

		while (cur_block_no != 0) {
			count++;
			snprintf(c, 100, "%s, %d", c, cur_block_no);

			bh = sb_bread(sb, cur_block_no);
			if (!bh)
				continue;

			index = (struct ouichefs_file_index_block *)bh->b_data;
			cur_block_no = index->previous_block_number;


		}
		seq_printf(file, "%d %d [%s]\n", ino, count, c + 2);
iput:
		iput(inode);
	}
	return 0;
}


static int debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_seq_show, inode->i_private);
}

static const struct file_operations debug_fops = {
	.owner = THIS_MODULE,
	.open = debugfs_open,
	.read = seq_read,
	.release = single_release,
};


int ouichefs_debug_file(struct super_block *sb)
{
	struct dentry *debugfs_file;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);

	debugfs_file = debugfs_create_file(sb->s_id, 0400, NULL, NULL,
					&debug_fops);
	debugfs_file->d_inode->i_private = sb;
	if (!debugfs_file) {
		pr_err("Debugfs file creation failed\n");
		return -EIO;
	}
	sbi->debug_file = debugfs_file;
	return 0;
}

/*
 * Mount a ouiche_fs partition
 */
struct dentry *ouichefs_mount(struct file_system_type *fs_type, int flags,
			      const char *dev_name, void *data)
{
	struct dentry *dentry = NULL;

	dentry = mount_bdev(fs_type, flags, dev_name, data,
			    ouichefs_fill_super);
	if (IS_ERR(dentry))
		pr_err("'%s' mount failure\n", dev_name);
	else
		pr_info("'%s' mount success\n", dev_name);

	ouichefs_debug_file(dentry->d_inode->i_sb);

	return dentry;
}

/*
 * Unmount a ouiche_fs partition
 */
void ouichefs_kill_sb(struct super_block *sb)
{
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);

	debugfs_remove(sbi->debug_file);
	kill_block_super(sb);

	pr_info("unmounted disk\n");
}

static struct file_system_type ouichefs_file_system_type = {
	.owner = THIS_MODULE,
	.name = "ouichefs",
	.mount = ouichefs_mount,
	.kill_sb = ouichefs_kill_sb,
	.fs_flags = FS_REQUIRES_DEV,
	.next = NULL,
};

static int __init ouichefs_init(void)
{
	int ret;

	ret = ouichefs_init_inode_cache();
	if (ret) {
		pr_err("inode cache creation failed\n");
		goto end;
	}

	ret = register_filesystem(&ouichefs_file_system_type);
	if (ret) {
		pr_err("register_filesystem() failed\n");
		goto free_inode_cache;
	}

	pr_info("module loaded\n");
	return 0;

free_inode_cache:
	ouichefs_destroy_inode_cache();
end:
	return ret;
}

static void __exit ouichefs_exit(void)
{
	int ret;

	ret = unregister_filesystem(&ouichefs_file_system_type);
	if (ret)
		pr_err("unregister_filesystem() failed\n");

	ouichefs_destroy_inode_cache();

	pr_info("module unloaded\n");
}

module_init(ouichefs_init);
module_exit(ouichefs_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Redha Gouicem, <redha.gouicem@lip6.fr>");
MODULE_DESCRIPTION("ouichefs, a simple educational filesystem for Linux");
