#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "tesda_uapi.h"

#define TEST_SIZE 4096

static void die(const char *what) {
  perror(what);
  exit(EXIT_FAILURE);
}

static void print_info(int fd) {
  struct tesda_info info;
  struct tesda_stat stat;
  unsigned int i;

  if (ioctl(fd, TESDA_IOCTL_GETINFO, &info) < 0)
    die("ioctl(GETINFO)");

  printf("GETINFO:\n");
  printf("  devices       : %u\n", info.partitions);
  printf("  sector size   : %u\n", info.sector_size);
  printf("  device size   : %" PRIu64 " bytes\n",
         (uint64_t)info.part_size_bytes);
  printf("  total backing : %" PRIu64 " bytes\n",
         (uint64_t)info.total_size_bytes);

  for (i = 0; i < info.partitions; i++) {
    struct tesda_partition_info pi;

    memset(&pi, 0, sizeof(pi));
    pi.id = i;

    if (ioctl(fd, TESDA_IOCTL_GETPARTITION, &pi) < 0)
      die("ioctl(GETPARTITION)");

    printf("  [%u] %s start=%" PRIu64 " sectors=%" PRIu64 "\n", i, pi.name,
           (uint64_t)pi.start_sector, (uint64_t)pi.nr_sectors);
  }

  if (ioctl(fd, TESDA_IOCTL_GETSTAT, &stat) < 0)
    die("ioctl(GETSTAT)");

  printf("GETSTAT: reads=%" PRIu64 " writes=%" PRIu64 " bytes_read=%" PRIu64
         " bytes_written=%" PRIu64 "\n",
         (uint64_t)stat.reads, (uint64_t)stat.writes, (uint64_t)stat.bytes_read,
         (uint64_t)stat.bytes_written);
}

static void test_device(const char *path, unsigned int id) {
  unsigned char write_buf[TEST_SIZE];
  unsigned char read_buf[TEST_SIZE];
  ssize_t n;
  int fd;
  size_t i;

  fd = open(path, O_RDWR);
  if (fd < 0)
    die(path);

  for (i = 0; i < sizeof(write_buf); i++)
    write_buf[i] = (unsigned char)((i + id * 37U) & 0xffU);

  memset(read_buf, 0, sizeof(read_buf));

  n = pwrite(fd, write_buf, sizeof(write_buf), 0);
  if (n != (ssize_t)sizeof(write_buf)) {
    if (n < 0)
      die("pwrite");
    fprintf(stderr, "%s: short pwrite: %zd\n", path, n);
    exit(EXIT_FAILURE);
  }

  n = pread(fd, read_buf, sizeof(read_buf), 0);
  if (n != (ssize_t)sizeof(read_buf)) {
    if (n < 0)
      die("pread");
    fprintf(stderr, "%s: short pread: %zd\n", path, n);
    exit(EXIT_FAILURE);
  }

  if (memcmp(write_buf, read_buf, sizeof(write_buf)) != 0) {
    fprintf(stderr, "%s: READ/WRITE test FAILED\n", path);
    exit(EXIT_FAILURE);
  }

  printf("%s: READ/WRITE test OK (%d bytes)\n", path, TEST_SIZE);
  close(fd);
}

int main(void) {
  const char *devices[TESDA_PARTITIONS] = {"/dev/tesda0", "/dev/tesda1",
                                           "/dev/tesda2"};
  int fd;
  unsigned int i;

  fd = open(devices[0], O_RDWR);
  if (fd < 0)
    die(devices[0]);

  printf("Resetting backing store and statistics...\n");
  if (ioctl(fd, TESDA_IOCTL_RESET) < 0)
    die("ioctl(RESET)");

  print_info(fd);
  close(fd);

  for (i = 0; i < TESDA_PARTITIONS; i++)
    test_device(devices[i], i);

  fd = open(devices[0], O_RDWR);
  if (fd < 0)
    die(devices[0]);

  puts("\nStatistics after tests:");
  print_info(fd);
  close(fd);

  puts("\nAll tests passed.");
  return EXIT_SUCCESS;
}
