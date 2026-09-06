#ifndef TESDA_UAPI_H
#define TESDA_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define TESDA_PARTITIONS 3U
#define TESDA_SECTOR_SIZE 512ULL
#define TESDA_PART_SIZE_BYTES (100ULL * 1024ULL * 1024ULL)
#define TESDA_TOTAL_SIZE_BYTES (TESDA_PARTITIONS * TESDA_PART_SIZE_BYTES)

#define TESDA_NAME_LEN 32

struct tesda_info {
  __u32 partitions;
  __u32 sector_size;
  __u64 part_size_bytes;
  __u64 total_size_bytes;
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
  char name[TESDA_NAME_LEN];
};

#define TESDA_IOCTL_MAGIC 'T'
#define TESDA_IOCTL_RESET _IO(TESDA_IOCTL_MAGIC, 0x01)
#define TESDA_IOCTL_GETINFO _IOR(TESDA_IOCTL_MAGIC, 0x02, struct tesda_info)
#define TESDA_IOCTL_GETSTAT _IOR(TESDA_IOCTL_MAGIC, 0x03, struct tesda_stat)
#define TESDA_IOCTL_GETPARTITION                                               \
  _IOWR(TESDA_IOCTL_MAGIC, 0x04, struct tesda_partition_info)

#endif /* TESDA_UAPI_H */
