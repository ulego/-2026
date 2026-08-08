// SPDX-License-Identifier: GPL-2.0
/*
 * tesda.c — Block device driver with 3 partitions (100 MiB each)
 *
 * Kernel: 6.1.x
 * Device: /dev/sda{0,1,2} + symlinks /dev/tesda{0,1,2}
 * Backing: vmalloc(300 MiB)
 * Queue: blk_mq_init_queue (single-queue blk-mq)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/backing-dev.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/ioctl.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/moduleparam.h>

#define DRIVER_NAME "tesda"
#define PARTITIONS 3
#define PART_SIZE_SECTORS (100 * 1024 * 1024 / 512) /* 209715200 sectors = 100 MiB */
#define PART_SIZE_BYTES (100 * 1024 * 1024)          /* 104857600 bytes */
#define TOTAL_SIZE_BYTES (PART_SIZE_BYTES * PARTITIONS) /* 314572800 bytes = 300 MiB */
#define TOTAL_SIZE_SECTORS (PART_SIZE_SECTORS * PARTITIONS)
#define PROC_NAME "tesda"

/* ============================================================
 * IOCTL definitions
 * ============================================================ */

#define TESDA_IOCTL_MAGIC 0xA0

#define TESDA_IOCTL_RESET        _IO(TESDA_IOCTL_MAGIC, 0x01)
#define TESDA_IOCTL_GETINFO      _IOWR(TESDA_IOCTL_MAGIC, 0x02, struct tesda_info)
#define TESDA_IOCTL_GETSTAT      _IOWR(TESDA_IOCTL_MAGIC, 0x03, struct tesda_stat)
#define TESDA_IOCTL_GETPARTITION _IOWR(TESDA_IOCTL_MAGIC, 0x04, struct tesda_partition_info)

/* ============================================================
 * Data structures
 * ============================================================ */

struct tesda_info {
	__u32 partitions;
	__u64 part_size;
	__u64 total_size;
	__u32 sector_size;
	__u32 reserved;
};

struct tesda_stat {
	__u64 reads;
	__u64 writes;
	__u64 bytes_read;
	__u64 bytes_written;
};

struct tesda_partition_info {
	__u32 id;
	__u32 reserved;
	__u64 start_sector;
	__u64 nr_sectors;
	char name[16];
};

struct tesda_part_stats {
	__u64 reads;
	__u64 writes;
	__u64 bytes_read;
	__u64 bytes_written;
};

/* Per-instance device state */
struct tesda_dev {
	int major;
	struct gendisk *gd;
	struct request_queue *queue;
	struct blk_mq_tag_set tq_set;
	unsigned char *storage;
	struct class *cls;
	struct device *devices[PARTITIONS];
	struct proc_dir_entry *proc_entry;
	struct tesda_part_stats stats[PARTITIONS];
	spinlock_t lock;
	int open_count;
};

static struct tesda_dev *tesda_dev_ptr;

/* ============================================================
 * Helpers
 * ============================================================ */

static int get_partition_from_sector(sector_t sector, int *part_id, sector_t *local_sector)
{
	if (sector >= TOTAL_SIZE_SECTORS)
		return -EINVAL;

	*part_id = sector / PART_SIZE_SECTORS;
	*local_sector = sector - (*part_id * PART_SIZE_SECTORS);

	/* Validate within partition bounds */
	if (*local_sector >= PART_SIZE_SECTORS)
		return -EINVAL;

	return 0;
}

static sector_t get_partition_size_sectors(void)
{
	return PART_SIZE_SECTORS;
}

/* ============================================================
 * blk-mq queue_rq callback
 * ============================================================ */

static blk_status_t tesda_queue_rq(struct blk_mq_hw_ctx *hctx,
				   const struct blk_mq_queue_data *qd)
{
	struct request *req = qd->rq;
	struct bio *bio;
	int ret;
	unsigned long flags;

	blk_mq_start_request(req);

	/* Protect storage access with spinlock */
	spin_lock_irqsave(&tesda_dev_ptr->lock, flags);

	/* Iterate over each bio in the request */
	__rq_for_each_bio(bio, req) {
		sector_t sector = bio->bi_iter.bi_sector;
		int part_id;
		sector_t local_sector;
		unsigned int nr_sectors = bio_sectors(bio);
		struct bio_vec bvec;
		struct bvec_iter iter;

		/* Determine partition and local offset */
		ret = get_partition_from_sector(sector, &part_id, &local_sector);
		if (ret) {
			/* Out of bounds */
			spin_unlock_irqrestore(&tesda_dev_ptr->lock, flags);
			blk_mq_end_request(req, BLK_STS_IOERR);
			return BLK_STS_IOERR;
		}

		/* Validate range within partition */
		if (local_sector + nr_sectors > PART_SIZE_SECTORS) {
			spin_unlock_irqrestore(&tesda_dev_ptr->lock, flags);
			blk_mq_end_request(req, BLK_STS_IOERR);
			return BLK_STS_IOERR;
		}

		/* Calculate byte offset into backing store */
		unsigned long long byte_offset = (unsigned long long)(local_sector * 512);
		unsigned char *storage_ptr = tesda_dev_ptr->storage + byte_offset;

		if (bio_data_dir(bio) == READ) {
			bio_for_each_segment(bvec, bio, iter) {
				void *dst = kmap_local_page(bvec.bv_page);
				memcpy(dst + bvec.bv_offset, storage_ptr, bvec.bv_len);
				kunmap_local(dst);
				storage_ptr += bvec.bv_len;
			}
			/* Update stats */
			tesda_dev_ptr->stats[part_id].reads++;
			tesda_dev_ptr->stats[part_id].bytes_read += bio->bi_iter.bi_size;
		} else if (bio_data_dir(bio) == WRITE) {
			bio_for_each_segment(bvec, bio, iter) {
				void *src = kmap_local_page(bvec.bv_page);
				memcpy(storage_ptr, src + bvec.bv_offset, bvec.bv_len);
				kunmap_local(src);
				storage_ptr += bvec.bv_len;
			}
			/* Update stats */
			tesda_dev_ptr->stats[part_id].writes++;
			tesda_dev_ptr->stats[part_id].bytes_written += bio->bi_iter.bi_size;
		}
	}

	spin_unlock_irqrestore(&tesda_dev_ptr->lock, flags);
	blk_mq_end_request(req, BLK_STS_OK);
	return BLK_STS_OK;
}

/* ============================================================
 * block_device_operations
 * ============================================================ */

static int tesda_open(struct block_device *bdev, fmode_t mode)
{
	struct tesda_dev *dev = bdev->bd_disk->private_data;

	spin_lock(&dev->lock);
	dev->open_count++;
	spin_unlock(&dev->lock);
	return 0;
}

static void tesda_release(struct gendisk *gd, fmode_t mode)
{
	struct tesda_dev *dev = gd->private_data;

	spin_lock(&dev->lock);
	if (dev->open_count > 0)
		dev->open_count--;
	spin_unlock(&dev->lock);
}

static int tesda_ioctl(struct block_device *bdev, fmode_t mode,
		       unsigned int cmd, unsigned long arg)
{
	struct tesda_dev *dev = bdev->bd_disk->private_data;
	struct tesda_partition_info part_info;
	struct tesda_info info;
	struct tesda_stat stat;
	int i;
	unsigned long flags;

	/* Handle standard BLK* ioctl commands first */
	if (_IOC_TYPE(cmd) != TESDA_IOCTL_MAGIC) {
		return blk_ioctl(bdev, mode, cmd, arg);
	}

	switch (cmd) {
	case TESDA_IOCTL_RESET: {
		spin_lock_irqsave(&dev->lock, flags);

		/* Free old storage, allocate new, zero it */
		if (dev->storage)
			vfree(dev->storage);

		dev->storage = vmalloc(TOTAL_SIZE_BYTES);
		if (!dev->storage) {
			spin_unlock_irqrestore(&dev->lock, flags);
			return -ENOMEM;
		}
		memset(dev->storage, 0, TOTAL_SIZE_BYTES);

		/* Reset all stats */
		for (i = 0; i < PARTITIONS; i++) {
			dev->stats[i].reads = 0;
			dev->stats[i].writes = 0;
			dev->stats[i].bytes_read = 0;
			dev->stats[i].bytes_written = 0;
		}

		spin_unlock_irqrestore(&dev->lock, flags);
		break;
	}

	case TESDA_IOCTL_GETINFO: {
		info.partitions = PARTITIONS;
		info.part_size = PART_SIZE_BYTES;
		info.total_size = TOTAL_SIZE_BYTES;
		info.sector_size = 512;
		info.reserved = 0;

		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			return -EFAULT;
		break;
	}

	case TESDA_IOCTL_GETSTAT: {
		spin_lock_irqsave(&dev->lock, flags);

		/* Aggregate stats across all partitions */
		memset(&stat, 0, sizeof(stat));
		for (i = 0; i < PARTITIONS; i++) {
			stat.reads += dev->stats[i].reads;
			stat.writes += dev->stats[i].writes;
			stat.bytes_read += dev->stats[i].bytes_read;
			stat.bytes_written += dev->stats[i].bytes_written;
		}

		spin_unlock_irqrestore(&dev->lock, flags);

		if (copy_to_user((void __user *)arg, &stat, sizeof(stat)))
			return -EFAULT;
		break;
	}

	case TESDA_IOCTL_GETPARTITION: {
		/* User passes pointer to struct tesda_partition_info with id set */
		if (copy_from_user(&part_info, (void __user *)arg, sizeof(part_info)))
			return -EFAULT;

		if (part_info.id >= PARTITIONS)
			return -EINVAL;

		part_info.start_sector = (__u64)part_info.id * PART_SIZE_SECTORS;
		part_info.nr_sectors = PART_SIZE_SECTORS;
		snprintf(part_info.name, sizeof(part_info.name), "tesda%d", part_info.id);

		if (copy_to_user((void __user *)arg, &part_info, sizeof(part_info)))
			return -EFAULT;
		break;
	}

	default:
		return -ENOTTY;
	}

	return 0;
}

static const struct block_device_operations tesda_blk_fops = {
	.owner   = THIS_MODULE,
	.open    = tesda_open,
	.release = tesda_release,
	.ioctl   = tesda_ioctl,
};

/* ============================================================
 * /proc interface
 * ============================================================ */

static int tesda_proc_show(struct seq_file *m, void *v)
{
	struct tesda_dev *dev = m->private;
	int i;

	seq_printf(m, "Device: %s\n", DRIVER_NAME);
	seq_printf(m, "Major: %d\n", dev->major);
	seq_printf(m, "Partitions: %d\n\n", PARTITIONS);

	for (i = 0; i < PARTITIONS; i++) {
		seq_printf(m, "Partition %d:\n", i);
		seq_printf(m, "  Name:     tesda%d\n", i);
		seq_printf(m, "  Size:     %u MiB (%u sectors)\n",
			   PART_SIZE_BYTES / (1024 * 1024), PART_SIZE_SECTORS);
		seq_printf(m, "  Start:    %llu\n",
			   (unsigned long long)i * PART_SIZE_SECTORS);
		seq_printf(m, "  Reads:    %llu\n",
			   (unsigned long long)dev->stats[i].reads);
		seq_printf(m, "  Writes:   %llu\n",
			   (unsigned long long)dev->stats[i].writes);
		seq_printf(m, "  Bytes RD: %llu\n",
			   (unsigned long long)dev->stats[i].bytes_read);
		seq_printf(m, "  Bytes WR: %llu\n",
			   (unsigned long long)dev->stats[i].bytes_written);
		seq_puts(m, "\n");
	}

	return 0;
}

static int tesda_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, tesda_proc_show, PDE_DATA(inode));
}

static const struct proc_ops tesda_proc_ops = {
	.proc_open    = tesda_proc_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/* ============================================================
 * Module init / exit
 * ============================================================ */

static int __init tesda_init(void)
{
	struct tesda_dev *dev;
	int ret, i;

	pr_info(DRIVER_NAME ": Loading module...\n");

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		pr_err(DRIVER_NAME ": Failed to alloc device struct\n");
		return ret;
	}

	tesda_dev_ptr = dev;
	spin_lock_init(&dev->lock);
	dev->open_count = 0;

	/* 1. Register blkdev to get dynamic major */
	dev->major = register_blkdev(0, DRIVER_NAME);
	if (dev->major <= 0) {
		ret = -ENODEV;
		pr_err(DRIVER_NAME ": Failed to register blkdev\n");
		goto err_kfree;
	}
	pr_info(DRIVER_NAME ": Registered with major %d\n", dev->major);

	/* 2. Setup blk-mq tag set */
	dev->tq_set.ops = &(struct blk_mq_ops){
		.queue_rq = tesda_queue_rq,
	};
	dev->tq_set.nr_hw_queues = 1;
	dev->tq_set.queue_depth = 128;  /* reasonable depth */
	dev->tq_set.numa_node = NUMA_NO_NODE;
	dev->tq_set.flags = BLK_MQ_F_SINGLE_QUEUE;

	ret = blk_mq_alloc_tag_set(&dev->tq_set);
	if (ret) {
		pr_err(DRIVER_NAME ": Failed to alloc tag set: %d\n", ret);
		goto err_unregister_blkdev;
	}

	/* 3. Allocate disk structure */
	dev->gd = alloc_disk(PARTITIONS);
	if (!dev->gd) {
		ret = -ENOMEM;
		pr_err(DRIVER_NAME ": Failed to alloc disk\n");
		goto err_free_tag_set;
	}

	dev->gd->private_data = dev;
	dev->gd->major = dev->major;
	dev->gd->first_minor = 0;
	dev->gd->fops = &tesda_blk_fops;
	dev->gd->queue = NULL; /* set below */
	sprintf(dev->gd->disk_name, "sda");
	dev->gd->flags |= GENHD_FL_EXT_PAR;

	/* 4. Initialize queue via blk-mq */
	dev->queue = blk_mq_init_queue(&dev->tq_set);
	if (IS_ERR(dev->queue)) {
		ret = PTR_ERR(dev->queue);
		pr_err(DRIVER_NAME ": Failed to init queue: %d\n", ret);
		goto err_put_disk;
	}
	dev->gd->queue = dev->queue;

	/* 5. Set capacity */
	set_capacity(dev->gd, TOTAL_SIZE_SECTORS);

	/* 6. Allocate backing store */
	dev->storage = vmalloc(TOTAL_SIZE_BYTES);
	if (!dev->storage) {
		ret = -ENOMEM;
		pr_err(DRIVER_NAME ": Failed to vmalloc backing store\n");
		goto err_cleanup_queue;
	}
	memset(dev->storage, 0, TOTAL_SIZE_BYTES);

	/* 7. Register disk */
	add_disk(dev->gd);
	pr_info(DRIVER_NAME ": Disk registered, capacity %u sectors\n",
		(unsigned int)TOTAL_SIZE_SECTORS);

	/* 8. Create sysfs class and devices */
	dev->cls = class_create(DRIVER_NAME);
	if (IS_ERR(dev->cls)) {
		ret = PTR_ERR(dev->cls);
		pr_err(DRIVER_NAME ": Failed to create class: %d\n", ret);
		goto err_vfree;
	}

	for (i = 0; i < PARTITIONS; i++) {
		dev->devices[i] = device_create(dev->cls, NULL,
						MKDEV(dev->major, i),
						NULL, "tesda%d", i);
		if (IS_ERR(dev->devices[i])) {
			ret = PTR_ERR(dev->devices[i]);
			pr_err(DRIVER_NAME ": Failed to create device tesda%d: %d\n",
			       i, ret);
			goto err_destroy_devices;
		}
	}

	/* 9. Create /proc entry */
	dev->proc_entry = proc_create(PROC_NAME, 0444, NULL, &tesda_proc_ops);
	if (dev->proc_entry) {
		dev->proc_entry->private_data = dev;
	} else {
		pr_warn(DRIVER_NAME ": Failed to create /proc/%s\n", PROC_NAME);
	}

	pr_info(DRIVER_NAME ": Loaded successfully (major=%d)\n", dev->major);
	return 0;

err_destroy_devices:
	for (i = i - 1; i >= 0; i--)
		device_destroy(dev->cls, MKDEV(dev->major, i));
	class_destroy(dev->cls);

err_vfree:
	vfree(dev->storage);

err_cleanup_queue:
	blk_cleanup_queue(dev->queue);

err_put_disk:
	put_disk(dev->gd);

err_free_tag_set:
	blk_mq_free_tag_set(&dev->tq_set);

err_unregister_blkdev:
	unregister_blkdev(dev->major, DRIVER_NAME);

err_kfree:
	kfree(dev);
	tesda_dev_ptr = NULL;

	return ret;
}

static void __exit tesda_exit(void)
{
	struct tesda_dev *dev = tesda_dev_ptr;
	int i;

	if (!dev)
		return;

	pr_info(DRIVER_NAME ": Unloading module...\n");

	/* Reverse order of init */

	/* 1. Remove proc entry */
	if (dev->proc_entry)
		remove_proc_entry(PROC_NAME, NULL);

	/* 2. Destroy devices */
	for (i = 0; i < PARTITIONS; i++) {
		if (dev->devices[i])
			device_destroy(dev->cls, MKDEV(dev->major, i));
	}

	/* 3. Destroy class */
	if (dev->cls)
		class_destroy(dev->cls);

	/* 4. Cleanup queue first */
	if (dev->queue)
		blk_cleanup_queue(dev->queue);

	/* 5. Remove disk */
	if (dev->gd) {
		del_gendisk(dev->gd);
		put_disk(dev->gd);
	}

	/* 6. Free tag set */
	blk_mq_free_tag_set(&dev->tq_set);

	/* 7. Free backing store */
	if (dev->storage)
		vfree(dev->storage);

	/* 8. Unregister char device */
	unregister_blkdev(dev->major, DRIVER_NAME);

	/* 9. Free device struct */
	kfree(dev);
	tesda_dev_ptr = NULL;

	pr_info(DRIVER_NAME ": Unloaded\n");
}

module_init(tesda_init);
module_exit(tesda_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Project");
MODULE_DESCRIPTION("Block device driver with 3 partitions of 100 MiB each");
MODULE_VERSION("1.1");