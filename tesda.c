// SPDX-License-Identifier: GPL-2.0
/*
 * tesda.c - учебный RAM-backed блочный драйвер для Linux 6.1.x.
 *
 * Драйвер создаёт три независимых блочных устройства:
 *   /dev/tesda0
 *   /dev/tesda1
 *   /dev/tesda2
 *
 * Каждое устройство имеет размер 100 MiB. Общий backing store размером
 * 300 MiB выделяется через vzalloc(). Для I/O используется blk-mq.
 *
 * Дополнительно реализованы:
 *   - ioctl: RESET, GETINFO, GETSTAT, GETPARTITION;
 *   - /proc/tesda со статистикой;
 *   - /sys/block/tesdaN/tesda_id;
 *   - /sys/block/tesdaN/tesda_start_sector;
 *   - /sys/block/tesdaN/tesda_stats.
 */

#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "tesda_uapi.h"

#define TESDA_NAME "tesda"
#define TESDA_PART_SECTORS (TESDA_PART_SIZE_BYTES / TESDA_SECTOR_SIZE)

struct tesda_stats {
  u64 reads;
  u64 writes;
  u64 bytes_read;
  u64 bytes_written;
};

struct tesda_dev;

/* Описание одного логического устройства /dev/tesdaN. */
struct tesda_part {
  struct tesda_dev *dev;
  struct gendisk *disk;
  struct request_queue *queue;
  int id;
  bool added;
  bool sysfs_added;
  struct tesda_stats st;
};

/* Общие данные всего драйвера. */
struct tesda_dev {
  int major;
  struct blk_mq_tag_set tag_set;
  unsigned char *storage;
  struct mutex lock;
  struct tesda_part part[TESDA_PARTITIONS];
  struct proc_dir_entry *proc;
};

static struct tesda_dev *tesda;

/*
 * Обработка одного запроса blk-mq.
 *
 * bi_sector для каждого из трёх gendisk начинается с нуля. Поэтому сначала
 * вычисляется смещение конкретного tesdaN внутри общего backing store, а затем
 * добавляется смещение сектора внутри этого устройства.
 */
static blk_status_t tesda_queue_rq(struct blk_mq_hw_ctx *hctx,
                                   const struct blk_mq_queue_data *bd) {
  struct request *rq = bd->rq;
  struct request_queue *q = rq->q;
  struct tesda_part *part = q->queuedata;
  struct tesda_dev *dev;
  struct bio *bio;
  blk_status_t status = BLK_STS_OK;

  (void)hctx;

  if (unlikely(!part || !part->dev || !part->dev->storage)) {
    blk_mq_start_request(rq);
    blk_mq_end_request(rq, BLK_STS_IOERR);
    return BLK_STS_IOERR;
  }

  dev = part->dev;
  blk_mq_start_request(rq);

  /* Для учебного RAM-диска поддерживаем READ/WRITE и пустой FLUSH. */
  if (req_op(rq) == REQ_OP_FLUSH) {
    blk_mq_end_request(rq, BLK_STS_OK);
    return BLK_STS_OK;
  }

  if (req_op(rq) != REQ_OP_READ && req_op(rq) != REQ_OP_WRITE) {
    blk_mq_end_request(rq, BLK_STS_NOTSUPP);
    return BLK_STS_NOTSUPP;
  }

  mutex_lock(&dev->lock);

  __rq_for_each_bio(bio, rq) {
    sector_t sector = bio->bi_iter.bi_sector;
    unsigned int nsectors = bio_sectors(bio);
    u64 offset;
    u64 remaining;
    struct bio_vec bvec;
    struct bvec_iter iter;
    unsigned char *data_ptr;

    /* Запрос не должен выходить за границы одного tesdaN. */
    if (sector >= TESDA_PART_SECTORS ||
        nsectors > TESDA_PART_SECTORS - sector) {
      status = BLK_STS_IOERR;
      break;
    }

    offset =
        (u64)part->id * TESDA_PART_SIZE_BYTES + (u64)sector * TESDA_SECTOR_SIZE;
    remaining = bio->bi_iter.bi_size;

    if (offset > TESDA_TOTAL_SIZE_BYTES ||
        remaining > TESDA_TOTAL_SIZE_BYTES - offset) {
      status = BLK_STS_IOERR;
      break;
    }

    data_ptr = dev->storage + offset;

    bio_for_each_segment(bvec, bio, iter) {
      void *addr;

      if (unlikely(bvec.bv_len > remaining)) {
        status = BLK_STS_IOERR;
        break;
      }

      addr = kmap_local_page(bvec.bv_page);

      if (req_op(rq) == REQ_OP_READ)
        memcpy((char *)addr + bvec.bv_offset, data_ptr, bvec.bv_len);
      else
        memcpy(data_ptr, (char *)addr + bvec.bv_offset, bvec.bv_len);

      kunmap_local(addr);
      data_ptr += bvec.bv_len;
      remaining -= bvec.bv_len;
    }

    if (status != BLK_STS_OK)
      break;

    if (remaining != 0) {
      status = BLK_STS_IOERR;
      break;
    }

    if (req_op(rq) == REQ_OP_READ) {
      part->st.reads++;
      part->st.bytes_read += bio->bi_iter.bi_size;
    } else {
      part->st.writes++;
      part->st.bytes_written += bio->bi_iter.bi_size;
    }
  }

  mutex_unlock(&dev->lock);
  blk_mq_end_request(rq, status);
  return status;
}

/* Таблица операций blk-mq должна иметь статическое время жизни. */
static const struct blk_mq_ops tesda_mq_ops = {
    .queue_rq = tesda_queue_rq,
};

static int tesda_open(struct block_device *bdev, fmode_t mode) {
  (void)bdev;
  (void)mode;
  return 0;
}

static void tesda_release(struct gendisk *disk, fmode_t mode) {
  (void)disk;
  (void)mode;
}

/* ioctl-интерфейс пользовательского приложения. */
static int tesda_ioctl(struct block_device *bdev, fmode_t mode,
                       unsigned int cmd, unsigned long arg) {
  struct tesda_part *part;
  struct tesda_dev *dev;
  struct tesda_info info;
  struct tesda_stat stat = {0};
  struct tesda_partition_info pi;
  int i;

  (void)mode;

  if (unlikely(!bdev || !bdev->bd_disk))
    return -ENODEV;

  part = bdev->bd_disk->private_data;
  if (unlikely(!part || !part->dev))
    return -ENODEV;

  dev = part->dev;

  switch (cmd) {
  case TESDA_IOCTL_RESET:
    mutex_lock(&dev->lock);
    memset(dev->storage, 0, TESDA_TOTAL_SIZE_BYTES);
    for (i = 0; i < TESDA_PARTITIONS; i++)
      memset(&dev->part[i].st, 0, sizeof(dev->part[i].st));
    mutex_unlock(&dev->lock);
    return 0;

  case TESDA_IOCTL_GETINFO:
    info.partitions = TESDA_PARTITIONS;
    info.sector_size = TESDA_SECTOR_SIZE;
    info.part_size_bytes = TESDA_PART_SIZE_BYTES;
    info.total_size_bytes = TESDA_TOTAL_SIZE_BYTES;

    return copy_to_user((void __user *)arg, &info, sizeof(info)) ? -EFAULT : 0;

  case TESDA_IOCTL_GETSTAT:
    mutex_lock(&dev->lock);
    for (i = 0; i < TESDA_PARTITIONS; i++) {
      stat.reads += dev->part[i].st.reads;
      stat.writes += dev->part[i].st.writes;
      stat.bytes_read += dev->part[i].st.bytes_read;
      stat.bytes_written += dev->part[i].st.bytes_written;
    }
    mutex_unlock(&dev->lock);

    return copy_to_user((void __user *)arg, &stat, sizeof(stat)) ? -EFAULT : 0;

  case TESDA_IOCTL_GETPARTITION:
    if (copy_from_user(&pi, (void __user *)arg, sizeof(pi)))
      return -EFAULT;

    if (pi.id >= TESDA_PARTITIONS)
      return -EINVAL;

    pi.reserved = 0;
    pi.start_sector = (__u64)pi.id * TESDA_PART_SECTORS;
    pi.nr_sectors = TESDA_PART_SECTORS;
    snprintf(pi.name, sizeof(pi.name), "tesda%u", pi.id);

    return copy_to_user((void __user *)arg, &pi, sizeof(pi)) ? -EFAULT : 0;

  default:
    return -ENOTTY;
  }
}

static const struct block_device_operations tesda_fops = {
    .owner = THIS_MODULE,
    .open = tesda_open,
    .release = tesda_release,
    .ioctl = tesda_ioctl,
};

/* ------------------------- /proc/tesda ------------------------- */

static int tesda_proc_show(struct seq_file *m, void *v) {
  struct tesda_dev *dev = m->private;
  int i;

  (void)v;

  if (!dev)
    return -ENODEV;

  mutex_lock(&dev->lock);

  seq_printf(m, "Device: %s\nMajor: %d\nDevices: %u\n\n", TESDA_NAME,
             dev->major, TESDA_PARTITIONS);

  for (i = 0; i < TESDA_PARTITIONS; i++) {
    const struct tesda_stats *st = &dev->part[i].st;

    seq_printf(m, "Device %d:\n", i);
    seq_printf(m, "  Name:       tesda%d\n", i);
    seq_printf(m, "  Size:       100 MiB (%llu sectors)\n",
               (unsigned long long)TESDA_PART_SECTORS);
    seq_printf(m, "  Start:      %llu\n",
               (unsigned long long)i * TESDA_PART_SECTORS);
    seq_printf(m, "  Reads:      %llu\n", (unsigned long long)st->reads);
    seq_printf(m, "  Writes:     %llu\n", (unsigned long long)st->writes);
    seq_printf(m, "  Bytes read: %llu\n", (unsigned long long)st->bytes_read);
    seq_printf(m, "  Bytes writ: %llu\n\n",
               (unsigned long long)st->bytes_written);
  }

  mutex_unlock(&dev->lock);
  return 0;
}

static int tesda_proc_open(struct inode *inode, struct file *file) {
  return single_open(file, tesda_proc_show, pde_data(inode));
}

static const struct proc_ops tesda_proc_ops = {
    .proc_open = tesda_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

/* ------------------------- /sys/block/tesdaN ------------------------- */

static ssize_t tesda_id_show(struct device *device,
                             struct device_attribute *attr, char *buf) {
  struct gendisk *disk = dev_to_disk(device);
  struct tesda_part *part = disk->private_data;

  (void)attr;

  if (!part)
    return -ENODEV;

  return sysfs_emit(buf, "%d\n", part->id);
}
static DEVICE_ATTR_RO(tesda_id);

static ssize_t tesda_start_sector_show(struct device *device,
                                       struct device_attribute *attr,
                                       char *buf) {
  struct gendisk *disk = dev_to_disk(device);
  struct tesda_part *part = disk->private_data;

  (void)attr;

  if (!part)
    return -ENODEV;

  return sysfs_emit(buf, "%llu\n",
                    (unsigned long long)part->id * TESDA_PART_SECTORS);
}
static DEVICE_ATTR_RO(tesda_start_sector);

static ssize_t tesda_stats_show(struct device *device,
                                struct device_attribute *attr, char *buf) {
  struct gendisk *disk = dev_to_disk(device);
  struct tesda_part *part = disk->private_data;
  struct tesda_stats st;

  (void)attr;

  if (!part || !part->dev)
    return -ENODEV;

  mutex_lock(&part->dev->lock);
  st = part->st;
  mutex_unlock(&part->dev->lock);

  return sysfs_emit(
      buf, "reads=%llu writes=%llu bytes_read=%llu bytes_written=%llu\n",
      (unsigned long long)st.reads, (unsigned long long)st.writes,
      (unsigned long long)st.bytes_read, (unsigned long long)st.bytes_written);
}
static DEVICE_ATTR_RO(tesda_stats);

static struct attribute *tesda_attrs[] = {
    &dev_attr_tesda_id.attr,
    &dev_attr_tesda_start_sector.attr,
    &dev_attr_tesda_stats.attr,
    NULL,
};

static const struct attribute_group tesda_attr_group = {
    .attrs = tesda_attrs,
};

/*
 * Освобить один gendisk.
 *
 * В Linux 6.1 blk_mq_alloc_disk() помечает диск GD_OWNS_QUEUE. После успешного
 * add_disk() нужно вызвать del_gendisk(), затем put_disk(). Отдельный
 * blk_cleanup_queue() здесь НЕ нужен и в целевых headers 6.1.130 недоступен.
 */
static void tesda_destroy_part(struct tesda_part *part) {
  if (!part || !part->disk)
    return;

  if (part->sysfs_added) {
    sysfs_remove_group(&disk_to_dev(part->disk)->kobj, &tesda_attr_group);
    part->sysfs_added = false;
  }

  if (part->added) {
    del_gendisk(part->disk);
    part->added = false;
  }

  put_disk(part->disk);
  part->disk = NULL;
  part->queue = NULL;
}

static int __init tesda_init(void) {
  struct tesda_dev *dev;
  int i;
  int ret;

  dev = kzalloc(sizeof(*dev), GFP_KERNEL);
  if (!dev)
    return -ENOMEM;

  mutex_init(&dev->lock);

  /* vzalloc одновременно выделяет и обнуляет 300 MiB backing store. */
  dev->storage = vzalloc(TESDA_TOTAL_SIZE_BYTES);
  if (!dev->storage) {
    ret = -ENOMEM;
    goto err_dev;
  }

  dev->major = register_blkdev(0, TESDA_NAME);
  if (dev->major < 0) {
    ret = dev->major;
    goto err_storage;
  }

  memset(&dev->tag_set, 0, sizeof(dev->tag_set));
  dev->tag_set.ops = &tesda_mq_ops;
  dev->tag_set.nr_hw_queues = 1;
  dev->tag_set.queue_depth = 128;
  dev->tag_set.numa_node = NUMA_NO_NODE;
  dev->tag_set.cmd_size = 0;
  dev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
  dev->tag_set.driver_data = dev;

  ret = blk_mq_alloc_tag_set(&dev->tag_set);
  if (ret)
    goto err_major;

  for (i = 0; i < TESDA_PARTITIONS; i++) {
    struct tesda_part *part = &dev->part[i];

    part->dev = dev;
    part->id = i;

    /*
     * Linux 6.1: blk_mq_alloc_disk(tag_set, queuedata).
     * Функция сама создаёт request_queue, поэтому alloc_disk() и
     * blk_mq_init_queue() отдельно не вызываются.
     */
    part->disk = blk_mq_alloc_disk(&dev->tag_set, part);
    if (IS_ERR(part->disk)) {
      ret = PTR_ERR(part->disk);
      part->disk = NULL;
      goto err_parts;
    }

    part->queue = part->disk->queue;
    if (unlikely(!part->queue)) {
      ret = -ENODEV;
      goto err_parts;
    }

    blk_queue_logical_block_size(part->queue, TESDA_SECTOR_SIZE);
    blk_queue_physical_block_size(part->queue, TESDA_SECTOR_SIZE);

    part->disk->major = dev->major;
    part->disk->first_minor = i;
    part->disk->minors = 1;
    part->disk->fops = &tesda_fops;
    part->disk->private_data = part;

    /* Каждый tesdaN сам является конечным блочным устройством. */
    part->disk->flags |= GENHD_FL_NO_PART;

    snprintf(part->disk->disk_name, DISK_NAME_LEN, "tesda%d", i);
    set_capacity(part->disk, TESDA_PART_SECTORS);

    ret = add_disk(part->disk);
    if (ret)
      goto err_parts;
    part->added = true;

    ret = sysfs_create_group(&disk_to_dev(part->disk)->kobj, &tesda_attr_group);
    if (ret)
      goto err_parts;
    part->sysfs_added = true;
  }

  dev->proc = proc_create_data(TESDA_NAME, 0444, NULL, &tesda_proc_ops, dev);
  if (!dev->proc) {
    ret = -ENOMEM;
    goto err_parts;
  }

  tesda = dev;
  pr_info(TESDA_NAME ": loaded, major=%d, devices=/dev/tesda0..2\n",
          dev->major);
  return 0;

err_parts:
  /* Включая текущий элемент i: он мог быть создан, но не добавлен. */
  while (i >= 0) {
    tesda_destroy_part(&dev->part[i]);
    i--;
  }
  blk_mq_free_tag_set(&dev->tag_set);
err_major:
  unregister_blkdev(dev->major, TESDA_NAME);
err_storage:
  vfree(dev->storage);
err_dev:
  kfree(dev);
  return ret;
}

static void __exit tesda_exit(void) {
  int i;
  struct tesda_dev *dev = tesda;

  if (!dev)
    return;

  /* Сначала запрещаем новые обращения через /proc. */
  if (dev->proc) {
    proc_remove(dev->proc);
    dev->proc = NULL;
  }

  /* Удаляем диски и их очереди до освобождения общего tag_set. */
  for (i = 0; i < TESDA_PARTITIONS; i++)
    tesda_destroy_part(&dev->part[i]);

  blk_mq_free_tag_set(&dev->tag_set);
  unregister_blkdev(dev->major, TESDA_NAME);
  vfree(dev->storage);
  kfree(dev);
  tesda = NULL;

  pr_info(TESDA_NAME ": unloaded\n");
}

module_init(tesda_init);
module_exit(tesda_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Oleg Ulanov");
MODULE_DESCRIPTION("RAM-backed tesda block devices for Linux 6.1.x");
MODULE_VERSION("1.0");
