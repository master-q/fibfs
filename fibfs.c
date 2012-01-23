/*
 * Fib Filesystem - JOKE filesystem.
 *
 * Copyright (C) 2012 Kiwamu Okabe <kiwamu@debian.or.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fsnotify.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/mount.h>
#include <linux/ramfs.h>
#include <linux/parser.h>
#include <linux/sched.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>

#include "internal.h"

#define	FIBFS_NAMELEN	64

static DEFINE_SPINLOCK(allfibfs_lock);
static LIST_HEAD(allfibfs);

struct fibfs_private {
	struct list_head list;
	struct fibfs_info *psi;
	enum fibfs_type_id type;
	u64	id;
	ssize_t	size;
	char	data[];
};

static int fibfs_file_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private; // 必要？
	return 0;
}

static ssize_t copy_hoge_to_userbuf(void __user *to, size_t count, loff_t *ppos)
{
	static const char *from = "hoge";
	size_t available = strlen(from);
	loff_t pos = *ppos;
	size_t ret;

	if (pos < 0)
		return -EINVAL;
	if (pos >= available || !count)
		return 0;
	if (count > available - pos)
		count = available - pos;
	ret = copy_to_user(to, from + pos, count);
	if (ret == count)
		return -EFAULT;
	count -= ret;
	*ppos = pos + count;
	return count;
}

static ssize_t fibfs_file_read(struct file *file, char __user *userbuf,
						size_t count, loff_t *ppos)
{
//	struct fibfs_private *ps = file->private_data;

	return copy_hoge_to_userbuf(userbuf, count, ppos);
}

static const struct file_operations fibfs_file_operations = {
	.open	= fibfs_file_open,
	.read	= fibfs_file_read,
	.llseek	= default_llseek,
};

/*
 * When a file is unlinked from our file system we call the
 * platform driver to erase the record from persistent store.
 */
static int fibfs_unlink(struct inode *dir, struct dentry *dentry)
{
//	struct fibfs_private *p = dentry->d_inode->i_private;

//	p->psi->erase(p->type, p->id, p->psi);
	// persistent storeなるものを使わなければ削れるんだと思う

	return simple_unlink(dir, dentry);
}

static void fibfs_evict_inode(struct inode *inode)
{
	struct fibfs_private	*p = inode->i_private;
	unsigned long		flags;

	end_writeback(inode);
	if (p) {
		spin_lock_irqsave(&allfibfs_lock, flags);
		list_del(&p->list); // ここになにも繋いでないなら解放不要？
		spin_unlock_irqrestore(&allfibfs_lock, flags);
		kfree(p);
	}
}

static const struct inode_operations fibfs_dir_inode_operations = {
	.lookup		= simple_lookup,
	.unlink		= fibfs_unlink,
};

static struct inode *fibfs_get_inode(struct super_block *sb,
					const struct inode *dir, int mode, dev_t dev)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
		inode->i_ino = get_next_ino();
		inode->i_uid = inode->i_gid = 0;
		inode->i_mode = mode;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		case S_IFREG:
			inode->i_fop = &fibfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &fibfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;
			inc_nlink(inode);
			break;
		}
	}
	return inode;
}

enum {
	Opt_kmsg_bytes, Opt_err
};

static const match_table_t tokens = {
	{Opt_kmsg_bytes, "kmsg_bytes=%u"},
	{Opt_err, NULL}
};

static int fibfs_remount(struct super_block *sb, int *flags, char *data)
{
	return 0;
}

static const struct super_operations fibfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.evict_inode	= fibfs_evict_inode,
	.remount_fs	= fibfs_remount,
	.show_options	= generic_show_options,
};

static struct super_block *fibfs_sb;

int fibfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode = NULL;
	struct dentry *root;
	int err;

	save_mount_options(sb, data);

	fibfs_sb = sb;

	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_CACHE_SIZE;
	sb->s_blocksize_bits	= PAGE_CACHE_SHIFT;
	sb->s_magic		= FIBFSFS_MAGIC;
	sb->s_op		= &fibfs_ops;
	sb->s_time_gran		= 1;

	inode = fibfs_get_inode(sb, NULL, S_IFDIR | 0755, 0);
	if (!inode) {
		err = -ENOMEM;
		goto fail;
	}
	/* override ramfs "dir" options so we catch unlink(2) */
	inode->i_op = &fibfs_dir_inode_operations;

	root = d_alloc_root(inode);
	sb->s_root = root;
	if (!root) {
		err = -ENOMEM;
		goto fail;
	}

	return 0;
fail:
	iput(inode);
	return err;
}

static struct dentry *fibfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_single(fs_type, flags, data, fibfs_fill_super);
}

static void fibfs_kill_sb(struct super_block *sb)
{
	kill_litter_super(sb);
	fibfs_sb = NULL;
}

static struct file_system_type fibfs_fs_type = {
	.name		= "fibfs",
	.mount		= fibfs_mount,
	.kill_sb	= fibfs_kill_sb,
};

static int __init init_fibfs_fs(void)
{
	return register_filesystem(&fibfs_fs_type);
}
module_init(init_fibfs_fs)

MODULE_AUTHOR("Kiwamu Okabe <kiwamu@debian.or.jp>");
MODULE_LICENSE("GPL");
