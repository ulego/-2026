/*
 * test_tesda.c — Test application for the tesda block device driver
 *
 * Compile: gcc -o test_tesda test_tesda.c
 * Run:     sudo ./test_tesda <device> [test_name]
 *
 * Example: sudo ./test_tesda /dev/tesda0 all
 *          sudo ./test_tesda /dev/tesda1 info
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <linux/limits.h>

/* ============================================================
 * IOCTL definitions (must match tesda.c)
 * ============================================================ */

#define TESDA_IOCTL_MAGIC 0xA0

#define TESDA_IOCTL_RESET        _IO(TESDA_IOCTL_MAGIC, 0x01)
#define TESDA_IOCTL_GETINFO      _IOWR(TESDA_IOCTL_MAGIC, 0x02, struct tesda_info)
#define TESDA_IOCTL_GETSTAT      _IOWR(TESDA_IOCTL_MAGIC, 0x03, struct tesda_stat)
#define TESDA_IOCTL_GETPARTITION _IOWR(TESDA_IOCTL_MAGIC, 0x04, struct tesda_partition_info)

struct tesda_info {
	unsigned int partitions;
	unsigned long long part_size;
	unsigned long long total_size;
	unsigned int sector_size;
	unsigned int reserved;
};

struct tesda_stat {
	unsigned long long reads;
	unsigned long long writes;
	unsigned long long bytes_read;
	unsigned long long bytes_written;
};

struct tesda_partition_info {
	unsigned int id;
	unsigned int reserved;
	unsigned long long start_sector;
	unsigned long long nr_sectors;
	char name[16];
};

#define BUF_SIZE 4096

static int test_fd;
static char write_buf[BUF_SIZE];
static char read_buf[BUF_SIZE];

static void print_usage(const char *prog)
{
	fprintf(stderr, "Usage: %s <device> [test_name]\n", prog);
	fprintf(stderr, "Devices: /dev/tesda0, /dev/tesda1, /dev/tesda2\n");
	fprintf(stderr, "Tests: write, read, info, stat, partition, reset, invalid, proc, sysfs, all\n");
}

static int open_device(const char *path)
{
	int fd = open(path, O_RDWR | O_SYNC);
	if (fd < 0) {
		perror("open");
		return -1;
	}
	return fd;
}

/* ============================================================
 * Test implementations
 * ============================================================ */

static int test_write(void)
{
	printf("  TEST: Write to device\n");

	/* Fill write buffer with a pattern */
	for (int i = 0; i < BUF_SIZE; i++)
		write_buf[i] = (char)(i & 0xFF);

	ssize_t n = write(test_fd, write_buf, BUF_SIZE);
	if (n != BUF_SIZE) {
		printf("    FAIL: write returned %zd, expected %d\n", n, BUF_SIZE);
		return -1;
	}

	printf("    OK: Wrote %zd bytes\n", n);
	return 0;
}

static int test_read(void)
{
	printf("  TEST: Read back data\n");

	memset(read_buf, 0, sizeof(read_buf));
	lseek(test_fd, 0, SEEK_SET);

	ssize_t n = read(test_fd, read_buf, BUF_SIZE);
	if (n != BUF_SIZE) {
		printf("    FAIL: read returned %zd, expected %d\n", n, BUF_SIZE);
		return -1;
	}

	/* Compare patterns */
	int errors = 0;
	for (int i = 0; i < BUF_SIZE && i < n; i++) {
		if (read_buf[i] != (char)(i & 0xFF)) {
			errors++;
			if (errors <= 5)
				printf("    Mismatch at offset %d: got 0x%02x, expected 0x%02x\n",
				       i, (unsigned char)read_buf[i], (unsigned char)(i & 0xFF));
		}
	}

	if (errors == 0) {
		printf("    OK: Read %zd bytes, pattern verified\n", n);
		return 0;
	} else {
		printf("    FAIL: %d mismatches\n", errors);
		return -1;
	}
}

static int test_getinfo(void)
{
	printf("  TEST: TESDA_IOCTL_GETINFO\n");

	struct tesda_info info;
	int ret = ioctl(test_fd, TESDA_IOCTL_GETINFO, &info);
	if (ret < 0) {
		perror("ioctl GETINFO");
		return -1;
	}

	printf("    partitions: %u\n", info.partitions);
	printf("    part_size:  %llu\n", info.part_size);
	printf("    total_size: %llu\n", info.total_size);
	printf("    sector_size: %u\n", info.sector_size);

	if (info.partitions != 3 || info.part_size != 104857600 ||
	    info.total_size != 314572800 || info.sector_size != 512) {
		printf("    FAIL: info values mismatch\n");
		return -1;
	}

	printf("    OK\n");
	return 0;
}

static int test_getstat(void)
{
	printf("  TEST: TESDA_IOCTL_GETSTAT\n");

	/* Do some I/O first */
	lseek(test_fd, 0, SEEK_SET);
	write(test_fd, "STAT", 4);

	lseek(test_fd, 0, SEEK_SET);
	read(test_fd, read_buf, 4);

	struct tesda_stat stat;
	int ret = ioctl(test_fd, TESDA_IOCTL_GETSTAT, &stat);
	if (ret < 0) {
		perror("ioctl GETSTAT");
		return -1;
	}

	printf("    reads:   %llu\n", stat.reads);
	printf("    writes:  %llu\n", stat.writes);
	printf("    bytes_read:  %llu\n", stat.bytes_read);
	printf("    bytes_written: %llu\n", stat.bytes_written);

	if (stat.writes < 1 || stat.reads < 1) {
		printf("    FAIL: expected at least 1 read and 1 write\n");
		return -1;
	}

	printf("    OK\n");
	return 0;
}

static int test_getpartition(int part_id)
{
	printf("  TEST: TESDA_IOCTL_GETPARTITION (id=%d)\n", part_id);

	struct tesda_partition_info info;
	info.id = part_id;
	info.reserved = 0;

	int ret = ioctl(test_fd, TESDA_IOCTL_GETPARTITION, &info);
	if (ret < 0) {
		perror("ioctl GETPARTITION");
		return -1;
	}

	printf("    id:      %u\n", info.id);
	printf("    start:   %llu\n", info.start_sector);
	printf("    sectors: %llu\n", info.nr_sectors);
	printf("    name:    %s\n", info.name);

	if (info.id != part_id) {
		printf("    FAIL: id mismatch\n");
		return -1;
	}

	printf("    OK\n");
	return 0;
}

static int test_reset(void)
{
	printf("  TEST: TESDA_IOCTL_RESET\n");

	/* Write some data first */
	lseek(test_fd, 0, SEEK_SET);
	write(test_fd, "BEFORE_RESET", 14);

	/* Reset */
	int ret = ioctl(test_fd, TESDA_IOCTL_RESET);
	if (ret < 0) {
		perror("ioctl RESET");
		return -1;
	}

	/* Read back - should be zeros */
	lseek(test_fd, 0, SEEK_SET);
	memset(read_buf, 0xAA, BUF_SIZE);
	read(test_fd, read_buf, 14);

	for (int i = 0; i < 14; i++) {
		if (read_buf[i] != 0) {
			printf("    FAIL: data not zeroed after reset at offset %d\n", i);
			return -1;
		}
	}

	printf("    OK: data zeroed after reset\n");
	return 0;
}

static int test_invalid_ioctl(void)
{
	printf("  TEST: Invalid partition ID\n");

	struct tesda_partition_info info;
	info.id = 5;   /* invalid */
	info.reserved = 0;

	int ret = ioctl(test_fd, TESDA_IOCTL_GETPARTITION, &info);
	if (ret == 0) {
		printf("    FAIL: expected -EINVAL for invalid partition ID\n");
		return -1;
	}

	if (errno != EINVAL) {
		printf("    FAIL: expected errno=EINVAL, got errno=%d\n", errno);
		return -1;
	}

	printf("    OK: returned -EINVAL for invalid partition ID\n");
	return 0;
}

static int test_proc(void)
{
	printf("  TEST: /proc/tesda\n");

	FILE *f = fopen("/proc/tesda", "r");
	if (!f) {
		perror("fopen /proc/tesda");
		return -1;
	}

	char buf[256];
	int found_device = 0, found_partitions = 0, found_stats = 0;

	while (fgets(buf, sizeof(buf), f)) {
		if (strstr(buf, "Device:")) found_device = 1;
		if (strstr(buf, "Partitions:")) found_partitions = 1;
		if (strstr(buf, "Reads:")) found_stats = 1;
	}

	fclose(f);

	if (!found_device || !found_partitions || !found_stats) {
		printf("    FAIL: /proc/tesda missing expected fields\n");
		return -1;
	}

	printf("    OK: /proc/tesda contains Device, Partitions, Stats\n");
	return 0;
}

static int test_sysfs(void)
{
	printf("  TEST: /sys/class/tesda\n");

	struct stat st;
	int ret = stat("/sys/class/tesda", &st);
	if (ret < 0) {
		printf("    FAIL: /sys/class/tesda not found (%s)\n", strerror(errno));
		return -1;
	}

	if (!S_ISDIR(st.st_mode)) {
		printf("    FAIL: /sys/class/tesda is not a directory\n");
		return -1;
	}

	/* Check symlink exists */
	ret = stat("/sys/class/tesda/tesda0", &st);
	if (ret < 0) {
		printf("    WARN: /sys/class/tesda/tesda0 not found (%s)\n", strerror(errno));
		/* Non-fatal */
	} else {
		printf("    OK: /sys/class/tesda exists with device entries\n");
	}

	return 0;
}

/* ============================================================
 * main()
 * ============================================================ */

int main(int argc, char *argv[])
{
	const char *device;
	const char *test_name = NULL;
	int all_tests = 1;
	int failed = 0, total = 0;

	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	device = argv[1];

	if (argc >= 3) {
		test_name = argv[2];
		all_tests = 0;
	}

	/* Open the device */
	test_fd = open_device(device);
	if (test_fd < 0) {
		fprintf(stderr, "Failed to open %s\n", device);
		return 1;
	}

	printf("=== TESDA Driver Test Suite ===\n");
	printf("Device: %s\n\n", device);

	/* Run tests */
	if (all_tests || strcmp(test_name, "write") == 0) {
		total++;
		if (test_write()) failed++;
	}
	if (all_tests || strcmp(test_name, "read") == 0) {
		total++;
		if (test_read()) failed++;
	}
	if (all_tests || strcmp(test_name, "info") == 0) {
		total++;
		if (test_getinfo()) failed++;
	}
	if (all_tests || strcmp(test_name, "stat") == 0) {
		total++;
		if (test_getstat()) failed++;
	}

	/* Test partitions: need to open each partition separately */
	if (all_tests || strcmp(test_name, "partition") == 0) {
		for (int i = 0; i < 3; i++) {
			char path[64];
			snprintf(path, sizeof(path), "/dev/tesda%d", i);
			int fd = open_device(path);
			if (fd < 0) {
				printf("  TEST: TESDA_IOCTL_GETPARTITION (id=%d) - SKIP (can't open %s)\n", i, path);
				continue;
			}
			/* Use this fd for the test, then close it */
			int old_fd = test_fd;
			test_fd = fd;
			if (test_getpartition(i)) failed++;
			test_fd = old_fd;
			close(fd);
			total++;
		}
	}

	if (all_tests || strcmp(test_name, "reset") == 0) {
		total++;
		if (test_reset()) failed++;
	}
	if (all_tests || strcmp(test_name, "invalid") == 0) {
		total++;
		if (test_invalid_ioctl()) failed++;
	}
	if (all_tests || strcmp(test_name, "proc") == 0) {
		total++;
		if (test_proc()) failed++;
	}
	if (all_tests || strcmp(test_name, "sysfs") == 0) {
		total++;
		if (test_sysfs()) failed++;
	}

	close(test_fd);

	printf("\n=== Results ===\n");
	printf("Total: %d, Failed: %d, Passed: %d\n", total, failed, total - failed);

	return (failed == 0) ? 0 : 1;
}