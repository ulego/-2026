## 1. Module skeleton and build system

- [x] 1.1 Create Makefile with kbuild targets (module compilation, load/unload, clean)
- [x] 1.2 Create `tesda.c` with module init/exit skeleton (module_init, module_exit, LICENSE, MODULE_ macros)
- [x] 1.3 Register character device via `register_blkdev()` to obtain dynamic major number
- [x] 1.4 Implement `blk_ioctl()` fallback in block_device_operations for standard BLK* commands
- [ ] 1.5 Verify module compiles with `make` (no warnings)

## 2. Block device initialization (blk-mq)

- [x] 2.1 Define `tesda_mq_ops` struct with `.queue_rq` callback (`tesda_queue_rq`)
- [x] 2.2 Define and populate `blk_mq_tag_set` struct with ops, queue_depth=1
- [x] 2.3 Allocate tag set via `blk_mq_alloc_tag_set()`
- [x] 2.4 Allocate disk structure via `alloc_disk(3)` for 3 partitions
- [x] 2.5 Initialize single-queue via `blk_mq_init_sq_queue(&tag_set, driver_data, 1)`
- [x] 2.6 Set disk capacity: `set_capacity(disk, 3 * 209715200)` (sectors)
- [x] 2.7 Assign `disk->fops = &tesda_blk_fops`
- [x] 2.8 Register disk via `add_disk(disk)`
- [ ] 2.9 Verify block devices appear (check `/sys/block/` for sda)

## 3. Storage backend (vmalloc backing store)

- [x] 3.1 Allocate backing store: `storage = vmalloc(314572800)` (300 MiB)
- [x] 3.2 Zero-initialize storage with `memset`
- [x] 3.3 Implement partition offset calculation: `offset = part_id * PART_SIZE`
- [x] 3.4 Implement error handling: on vmalloc failure, return -ENOMEM and free all prior allocations in exit
- [x] 3.5 Free storage in module exit: `vfree(storage)`

## 4. Block request handler (queue_rq)

- [x] 4.1 Implement `tesda_queue_rq()` callback function
- [x] 4.2 Parse bio: extract sector from `rq->bio->bi_iter.bi_sector`
- [x] 4.3 Determine partition ID: `part_id = sector / 209715200`
- [x] 4.4 Validate partition ID: reject if `part_id >= 3` (return error)
- [x] 4.5 Calculate local offset: `local_sector = sector - (part_id * 209715200)`
- [x] 4.6 Validate range: `local_sector + bio_sectors <= 209715200`
- [x] 4.7 For REQ_OP_READ: `memcpy(to bio->bi_io_vec, storage + offset, size)`
- [x] 4.8 For REQ_OP_WRITE: `memcpy(storage + offset, from bio->bi_io_vec, size)`
- [x] 4.9 Update per-partition stats (reads, writes, bytes_read, bytes_written) under spinlock
- [x] 4.10 Call `bio_endio(bio)` on completion
- [x] 4.11 Return `BLK_MQ_OK` on success

## 5. Partition symlinks in /dev

- [x] 5.1 Create sysfs class: `class_create(THIS_MODULE, "tesda")`
- [x] 5.2 Create device: `device_create(cls, NULL, MKDEV(major, 0), NULL, "tesda0")`
- [x] 5.3 Create symlinks: `sysfs_create_link()` → `/sys/class/tesda/tesda0 -> ../sda0`
- [x] 5.4 Repeat for tesda1 and tesda2
- [x] 5.5 Cleanup in exit: `device_destroy()`, `class_destroy()`

## 6. ioctl commands

- [x] 6.1 Define ioctl magic number: `#define TESDA_IOCTL_MAGIC 0xA0`
- [x] 6.2 Define ioctl commands: `_IOWR(TESDA_IOCTL_MAGIC, 1, ...)` etc.
- [x] 6.3 Define `struct tesda_info` (partitions, part_size, total_size, sector_size)
- [x] 6.4 Define `struct tesda_stat` (reads, writes, bytes_read, bytes_written)
- [x] 6.5 Define `struct tesda_partition_info` (id, start_sector, nr_sectors, name)
- [x] 6.6 Implement `TESDA_IOCTL_RESET`: vfree old storage, vmalloc new, memset 0, reset stats
- [x] 6.7 Implement `TESDA_IOCTL_GETINFO`: copy `tesda_info` to userland
- [x] 6.8 Implement `TESDA_IOCTL_GETSTAT`: copy per-partition stats to userland
- [x] 6.9 Implement `TESDA_IOCTL_GETPARTITION`: validate id < 3, fill partition info
- [x] 6.10 Handle `-ENOTTY` for unknown ioctl commands
- [x] 6.11 Handle `-EFAULT` for copy_to_user failures

## 7. /proc interface

- [x] 7.1 Define `struct tesda_proc_data` to hold formatted output buffer
- [x] 7.2 Implement proc read callback (`tesda_proc_read`)
- [x] 7.3 Format output: device name, major, partition count, per-partition stats
- [x] 7.4 Create proc entry: `proc_create("tesda", 0444, NULL, &tesda_proc_ops)`
- [x] 7.5 Use `proc_ops` (not `file_operations`) for /proc in kernel 6.1
- [x] 7.6 Remove proc entry in module exit: `proc_remove()`

## 8. Error handling and cleanup

- [x] 8.1 Implement proper exit function: reverse order of init
- [x] 8.2 Free proc entry before destroying device class
- [x] 8.3 Destroy devices before freeing disk
- [x] 8.4 Free disk, cleanup queue, free tag set in correct order
- [x] 8.5 Unregister char device: `unregister_chrdev(major, "tesda")`
- [x] 8.6 Prevent module unload when devices are open: implement `.open` return -EBUSY check or use `THIS_MODULE`
- [ ] 8.7 Test module unload with no open devices (should succeed)
- [ ] 8.8 Test module unload with open device (should fail with -EBUSY)

## 9. Test application

- [x] 9.1 Create `test_tesda.c` with helper functions (open_device, check_result, etc.)
- [x] 9.2 Test write to `/dev/tesda0`: write known pattern, verify with read
- [x] 9.3 Test read from `/dev/tesda0`: read back written data, compare byte-by-byte
- [x] 9.4 Test write to `/dev/tesda1`: verify it doesn't affect `/dev/tesda0` data
- [x] 9.5 Test `TESDA_IOCTL_GETINFO`: verify returned structure values
- [x] 9.6 Test `TESDA_IOCTL_GETSTAT`: write 100 bytes, read 50 bytes, verify counters
- [x] 9.7 Test `TESDA_IOCTL_GETPARTITION`: query each partition (0, 1, 2), verify info
- [x] 9.8 Test `TESDA_IOCTL_RESET`: write data, reset, read back (should be zeros)
- [x] 9.9 Test invalid ioctl: pass invalid partition ID (should return -EINVAL)
- [x] 9.10 Test /proc/tesda: verify output contains expected fields after operations
- [x] 9.11 Test /sys/class/tesda: verify class and attributes exist

## 10. Documentation

- [x] 10.1 Update README.md: module build instructions (`make`, `insmod`, `rmmod`)
- [x] 10.2 Document ioctl commands with struct definitions and usage examples
- [x] 10.3 Document /proc/tesda output format with example
- [x] 10.4 Document /sys/class/tesda attributes
- [x] 10.5 Add test_tesda.c compilation and usage instructions
- [x] 10.6 Add known limitations section
