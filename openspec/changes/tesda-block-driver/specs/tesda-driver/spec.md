## Purpose

Определяет поведение блочного устройства ядра Linux `tesda` с тремя разделами по 100 MiB, операциями read/write/ioctl и интеграцией в /dev, /proc, /sys.

## ADDED Requirements

### Requirement: Module registration and device creation
The module MUST register as a dynamic major number block device named `tesda` and create three partitions accessible as block devices.

#### Scenario: Module loads successfully
- **WHEN** `insmod tesda.ko` is executed
- **THEN** a dynamic major number is assigned and three block devices appear (accessible via kernel as sda0/sda1/sda2)

#### Scenario: Symlinks created in /dev
- **WHEN** module is loaded
- **THEN** `/dev/tesda0`, `/dev/tesda1`, `/dev/tesda2` symlinks exist pointing to the kernel block devices

### Requirement: Partition sizing
Each of the three partitions MUST be exactly 100 MiB (100 × 1024 × 1024 = 104 857 600 bytes = 209 715 200 sectors of 512 bytes).

#### Scenario: Partition 0 size is 100 MiB
- **WHEN** the device is queried via BLKGETSIZE64
- **THEN** the size of `/dev/sda0` (or its symlink) returns 104 857 600 bytes

#### Scenario: Partition 1 size is 100 MiB
- **WHEN** the device is queried via BLKGETSIZE64
- **THEN** the size of `/dev/sda1` returns 104 857 600 bytes

#### Scenario: Partition 2 size is 100 MiB
- **WHEN** the device is queried via BLKGETSIZE64
- **THEN** the size of `/dev/sda2` returns 104 857 600 bytes

### Requirement: Read and write operations
Each partition MUST support read and write operations via standard block device I/O path.

#### Scenario: Write to partition 0
- **WHEN** user writes data to `/dev/sda0` (or its symlink)
- **THEN** the data is stored in the partition's backing store at the correct offset

#### Scenario: Read back written data from partition 0
- **WHEN** user reads from `/dev/sda0` after a write
- **THEN** the returned data matches what was written

#### Scenario: Write to partition 1 does not affect partition 0
- **WHEN** user writes data to `/dev/sda1`
- **THEN** reading from `/dev/sda0` returns data unaffected by the write to `/dev/sda1`

#### Scenario: Write exceeding partition bounds
- **WHEN** user attempts to write beyond the end of a partition
- **THEN** the operation fails with -EIO

### Requirement: ioctl commands
The device MUST support four custom ioctl commands: RESET, GETINFO, GETSTAT, GETPARTITION.

#### Scenario: TESDA_IOCTL_RESET clears all data
- **WHEN** `ioctl(fd, TESDA_IOCTL_RESET)` is called
- **THEN** all data across all three partitions is cleared (backing store zeroed)

#### Scenario: TESDA_IOCTL_GETINFO returns partition info
- **WHEN** `ioctl(fd, TESDA_IOCTL_GETINFO, &info)` is called
- **THEN** the returned `tesda_info` structure contains: partitions=3, part_size=104857600, total_size=314572800, sector_size=512

#### Scenario: TESDA_IOCTL_GETSTAT returns statistics
- **WHEN** `ioctl(fd, TESDA_IOCTL_GETSTAT, &stat)` is called after read/write operations
- **THEN** the returned `tesda_stat` structure contains accurate counts of reads, writes, bytes read, and bytes written

#### Scenario: TESDA_IOCTL_GETPARTITION returns partition details
- **WHEN** `ioctl(fd, TESDA_IOCTL_GETPARTITION, &part_info)` is called with partition ID 0
- **THEN** the returned structure contains id=0, start_sector=0, nr_sectors=209715200, name="tesda0"

#### Scenario: ioctl with invalid partition ID
- **WHEN** `ioctl(fd, TESDA_IOCTL_GETPARTITION, &part_info)` is called with id=3 (invalid)
- **THEN** the ioctl returns -EINVAL

### Requirement: /proc interface
A `/proc/tesda` file MUST provide human-readable statistics and partition information.

#### Scenario: /proc/tesda exists after module load
- **WHEN** module is loaded
- **THEN** `/proc/tesda` file exists and is readable

#### Scenario: /proc/tesda shows device info
- **WHEN** `cat /proc/tesda` is executed
- **THEN** the output includes the device name, major number, and total partition count

#### Scenario: /proc/tesda shows per-partition stats
- **WHEN** `cat /proc/tesda` is executed after read/write operations
- **THEN** the output includes per-partition statistics: read count, write count, bytes read, bytes written

### Requirement: /sys interface
The device MUST appear under `/sys/class/tesda/` with standard and custom attributes.

#### Scenario: /sys/class/tesda class is created
- **WHEN** module is loaded
- **THEN** a `tesda` class appears under `/sys/class/`

#### Scenario: /sys/class/tesda shows device size
- **WHEN** the device class is created
- **THEN** a `size` attribute is available under `/sys/class/tesda/`

### Requirement: Error handling
The driver MUST properly handle all error conditions.

#### Scenario: Module unload without open devices
- **WHEN** `rmmod tesda` is executed and no devices are open
- **THEN** the module unloads successfully, all resources are freed

#### Scenario: Module unload with open devices
- **WHEN** `rmmod tesda` is executed while a device is open
- **THEN** the module refuses to unload (returns -EBUSY)

#### Scenario: Module init fails on allocation
- **WHEN** `vmalloc(300 MiB)` fails during initialization
- **THEN** the init function returns -ENOMEM and frees all previously allocated resources
