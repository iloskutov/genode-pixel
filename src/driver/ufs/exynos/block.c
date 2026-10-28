/**
 * \brief  Genode <=> Linux kernel interaction - Linux side
 * \author Stefan Kalkowski
 * \author Ivan Loskutov
 * \date   2021-07-10
 *
 * Derived from the following file of Genode 25.11, with the code for the
 * 6.1 kernel only, with the block size of the device instead of 512 bytes
 * (UFS storage uses 4 KiB blocks), and without 'genhd_device_init', which
 * the kernel's block/genhd.c provides:
 *
 *   repos/dde_linux/src/lib/lx_emul/block.c
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include <linux/part_stat.h>
#include <../block/blk.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <uapi/linux/hdreg.h>

#include <genode_c_api/block.h>
#include <lx_emul/debug.h>
#include <lx_user/init.h>
#include <lx_user/io.h>


/* defined in genode_block.cc */
void genode_block_announce_device_with_block_size(const char *          name,
                                                  genode_block_sector_t blocks,
                                                  unsigned              block_size_log2);


struct block_device *bdev_alloc(struct gendisk *disk, u8 partno)
{
	struct block_device * bdev  = kzalloc(sizeof(struct block_device), GFP_KERNEL);
	struct inode        * inode = kzalloc(sizeof(struct inode), GFP_KERNEL);

	inode->i_mode = S_IFBLK;
	inode->i_rdev = 0;

	mutex_init(&bdev->bd_fsfreeze_mutex);
	spin_lock_init(&bdev->bd_size_lock);
	bdev->bd_disk = disk;

	bdev->bd_partno = partno;
	bdev->bd_inode = inode;

	bdev->bd_queue = disk->queue;

	bdev->bd_stats = alloc_percpu(struct disk_stats);
	return bdev;
}


enum { MAX_BDEV = 256 };
static struct block_device * bdevs[MAX_BDEV];

void bdev_add(struct block_device * bdev, dev_t dev)
{
	unsigned idx = MAJOR(dev);
	bdev->bd_dev = dev;
	if (idx < MAX_BDEV) {
		bdevs[idx] = bdev;
		return;
	}

	printk("Error: bdev_add invalid major=%u minor=%u\n",
	       MAJOR(dev), MINOR(dev));
	lx_emul_trace_and_stop(__func__);
}


extern void bdev_set_nr_sectors(struct block_device * bdev,sector_t sectors);
void bdev_set_nr_sectors(struct block_device * bdev,sector_t sectors)
{
	spin_lock(&bdev->bd_size_lock);
	i_size_write(bdev->bd_inode, (loff_t)sectors << SECTOR_SHIFT);
	bdev->bd_nr_sectors = sectors;
	spin_unlock(&bdev->bd_size_lock);
}


struct block_device *blkdev_get_by_dev(dev_t dev, fmode_t mode, void *holder)
{
	unsigned idx = MAJOR(dev);
	if (idx < MAX_BDEV)
		return bdevs[idx];

	printk("Error: blkdev_get_by_dev invalid major=%u minor=%u\n",
	       MAJOR(dev), MINOR(dev));
	return NULL;
}


enum { MAX_GEN_DISKS = 4 };

struct gendisk * gendisks[MAX_GEN_DISKS];

int blk_register_queue(struct gendisk * disk)
{
	unsigned idx;
	unsigned block_size_log2;

	for (idx = 0; idx < MAX_GEN_DISKS; idx++)
		if (!gendisks[idx])
			break;

	if (idx >= MAX_GEN_DISKS)
		return -1;

	block_size_log2 = ilog2(queue_logical_block_size(disk->queue));

	/* the capacity is given in 512-byte sectors */
	genode_block_announce_device_with_block_size(disk->disk_name,
		get_capacity(disk) >> (block_size_log2 - SECTOR_SHIFT),
		block_size_log2);
	gendisks[idx] = disk;
	return 0;
}


void blk_unregister_queue(struct gendisk * disk)
{
	unsigned idx;
	for (idx = 0; idx < MAX_GEN_DISKS; idx++)
		if (gendisks[idx] == disk)
			gendisks[idx] = NULL;

	genode_block_discontinue_device(disk->disk_name);
}


struct genode_block_session_context
{
	struct gendisk              *disk;
	struct genode_block_request *req;
};


static inline int block_handle_end_io(struct genode_block_session_context *ctx,
                               struct genode_block_session *session)
{
	struct genode_block_request * const req = ctx->req;

	if (genode_block_ack_request(session, req, true))
		lx_user_handle_io();

	return 1;
}


static void bio_end_io(struct bio *bio)
{
	struct genode_block_request * const req =
		(struct genode_block_request*) bio->bi_private;

	struct genode_block_session_context ctx = { .req = req };

	/*
	 * A failed request is acknowledged as successful (see
	 * 'block_handle_end_io'), and the kernel's own messages about the error
	 * are rate-limited away. This message leaves a trace of it.
	 */
	if (bio->bi_status)
		printk("Error: %s of %llu blocks at block %llu failed (status %u)\n",
		       op_is_write(bio_op(bio)) ? "write" : "read",
		       (unsigned long long)req->blk_cnt,
		       (unsigned long long)req->blk_nr, (unsigned)bio->bi_status);

	genode_block_session_ack_by_name(bio->bi_bdev->bd_disk->disk_name,
	                                 req->id, &ctx, block_handle_end_io);

	bio_put(bio);
}


static inline int block_sync(struct block_device * const bdev)
{
	if (blkdev_issue_flush(bdev)) {
		printk("blkdev_issue_flush failed!\n");
		return 0;
	}
	return 1;
}


static inline void block_request(struct block_device         * const bdev,
                                 struct genode_block_request * const request,
                                 bool                          write)
{
	unsigned const block_size = bdev_logical_block_size(bdev);

	struct bio  * bio  = bio_alloc(bdev, 1, write ? REQ_OP_WRITE
	                                              : REQ_OP_READ, GFP_KERNEL);
	struct page * page = virt_to_page(request->addr);

	bio_set_dev(bio, bdev);

	/* the bio counts 512-byte sectors */
	bio->bi_iter.bi_sector = request->blk_nr * (block_size >> SECTOR_SHIFT);
	bio->bi_end_io         = bio_end_io;
	bio->bi_opf            = write ? REQ_OP_WRITE : REQ_OP_READ;
	bio->bi_private        = request;

	if (bio_add_page(bio, page, request->blk_cnt * block_size,
	                 (unsigned long)request->addr & (PAGE_SIZE-1)))
		submit_bio(bio);
}


static inline int
block_handle_session(struct genode_block_session_context * const ctx,
                     struct genode_block_session * const session)
{
	if (!session)
		return 1;

	int resume = 0;

	unsigned count = 0;
	for (;;) {
		struct genode_block_request * const req =
			genode_block_request_by_session(session);
		struct block_device * const bdev = ctx->disk->part0;

		if (!req) {
			break;
		}

		switch (req->op) {
		case GENODE_BLOCK_READ:
			block_request(bdev, req, false);
			break;
		case GENODE_BLOCK_WRITE:
			block_request(bdev, req, true);
			break;
		case GENODE_BLOCK_SYNC:
			genode_block_ack_request(session, req, block_sync(bdev));
			genode_block_notify_peers();
			break;
		default:
			break;
		};

		/*
		 * Interrupt processing after some requests were handled
		 * to give other sessions a chance to submit their requests
		 * as well.
		 */

		if (++count >= 8) {
			resume = 1;
			break;
		}
	}
	return resume;
}


static int block_poll_sessions(void * data)
{
	for (;;) {
		unsigned idx;

		for (idx = 0; idx < MAX_GEN_DISKS; idx++) {

			struct gendisk * const disk = gendisks[idx];

			if (!disk)
				continue;

			{
				struct genode_block_session_context ctx = { .disk = disk };

				/*
				 * Loop as long as at least on session has not finished
				 * submitting its requests. This is mostly the case due
				 * to being interrupted forcefully in 'block_handle_session()'
				 * to allow other sessions to submit requests as well.
				 */
				if (!genode_block_session_for_each_by_name(disk->disk_name, &ctx,
				                                           block_handle_session))
					continue;
			}
		}

		lx_emul_task_schedule(true);
	}

	return 0;
}


static struct task_struct * lx_user_task = NULL;


void lx_user_handle_io(void)
{
	if (lx_user_task)
		lx_emul_task_unblock(lx_user_task);
}


void lx_user_init(void)
{
	int pid = kernel_thread(block_poll_sessions, NULL,
	                        CLONE_FS | CLONE_FILES);
	lx_user_task = find_task_by_pid_ns(pid, NULL);;
}
