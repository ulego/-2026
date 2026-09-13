/*
 * test_tesda.c — пользовательская проверочная программа для драйвера TESDA.
 *
 * Тест открывает блочные устройства обычными POSIX-вызовами, проверяет ioctl,
 * выполняет запись/чтение 4096 байт на каждом /dev/tesdaN и сравнивает данные.
 */

/* Просим libc использовать 64-битные смещения файлов, важно для block device.
 */
#define _FILE_OFFSET_BITS 64

#include <errno.h>     /* errno/perror */
#include <fcntl.h>     /* open, O_RDWR */
#include <inttypes.h>  /* PRIu64 для переносимого printf uint64_t */
#include <stdio.h>     /* printf, fprintf, perror, puts */
#include <stdlib.h>    /* exit, EXIT_SUCCESS/EXIT_FAILURE */
#include <string.h>    /* memset, memcmp */
#include <sys/ioctl.h> /* ioctl */
#include <unistd.h>    /* close, pread, pwrite */

/* Общий с драйвером ABI: структуры и номера ioctl-команд. */
#include "tesda_uapi.h"

/* Размер тестового блока: 4096 байт = 8 секторов по 512 байт. */
#define TEST_SIZE 4096

/*
 * Унифицированное завершение программы при системной ошибке.
 * perror() выводит переданную подпись и текст текущего errno.
 */
static void die(const char *what) {
  perror(what);
  exit(EXIT_FAILURE);
}

/*
 * Получить и вывести метаданные драйвера и текущую суммарную статистику.
 * fd должен быть открытым файловым дескриптором любого /dev/tesdaN.
 */
static void print_info(int fd) {
  struct tesda_info info; /* сюда ядро вернёт общую конфигурацию */
  struct tesda_stat stat; /* сюда ядро вернёт счётчики I/O */
  unsigned int i;

  /* Читаем количество устройств, размеры и размер сектора. */
  if (ioctl(fd, TESDA_IOCTL_GETINFO, &info) < 0)
    die("ioctl(GETINFO)");

  printf("GETINFO:\n");
  printf("  devices       : %u\n", info.partitions);
  printf("  sector size   : %u\n", info.sector_size);
  printf("  device size   : %" PRIu64 " bytes\n",
         (uint64_t)info.part_size_bytes);
  printf("  total backing : %" PRIu64 " bytes\n",
         (uint64_t)info.total_size_bytes);

  /* Отдельно запрашиваем описание каждого логического устройства. */
  for (i = 0; i < info.partitions; i++) {
    struct tesda_partition_info pi;

    /* Обнуляем reserved и буфер name, затем задаём только id. */
    memset(&pi, 0, sizeof(pi));
    pi.id = i;

    /* ioctl является IOWR: id идёт в ядро, описание возвращается назад. */
    if (ioctl(fd, TESDA_IOCTL_GETPARTITION, &pi) < 0)
      die("ioctl(GETPARTITION)");

    printf("  [%u] %s start=%" PRIu64 " sectors=%" PRIu64 "\n", i, pi.name,
           (uint64_t)pi.start_sector, (uint64_t)pi.nr_sectors);
  }

  /* Получаем сумму счётчиков всех tesdaN. */
  if (ioctl(fd, TESDA_IOCTL_GETSTAT, &stat) < 0)
    die("ioctl(GETSTAT)");

  printf("GETSTAT: reads=%" PRIu64 " writes=%" PRIu64 " bytes_read=%" PRIu64
         " bytes_written=%" PRIu64 "\n",
         (uint64_t)stat.reads, (uint64_t)stat.writes, (uint64_t)stat.bytes_read,
         (uint64_t)stat.bytes_written);
}

/*
 * Проверить реальную передачу данных через block layer для одного устройства.
 * path — /dev/tesdaN, id участвует в генерации уникального шаблона данных.
 */
static void test_device(const char *path, unsigned int id) {
  unsigned char write_buf[TEST_SIZE]; /* эталон для записи */
  unsigned char read_buf[TEST_SIZE];  /* сюда читаем данные обратно */
  ssize_t n;                          /* результат pread/pwrite */
  int fd;
  size_t i;

  /* Открываем блочное устройство на чтение и запись. */
  fd = open(path, O_RDWR);
  if (fd < 0)
    die(path);

  /*
   * Формируем детерминированный, но различающийся для tesda0/1/2 шаблон.
   * Маска 0xff оставляет младшие 8 бит для unsigned char.
   */
  for (i = 0; i < sizeof(write_buf); i++)
    write_buf[i] = (unsigned char)((i + id * 37U) & 0xffU);

  /* Заранее очищаем входной буфер, чтобы сравнение не прошло случайно. */
  memset(read_buf, 0, sizeof(read_buf));

  /* Пишем TEST_SIZE байт с абсолютного смещения 0 устройства. */
  n = pwrite(fd, write_buf, sizeof(write_buf), 0);
  if (n != (ssize_t)sizeof(write_buf)) {
    if (n < 0)
      die("pwrite");
    /* Короткая запись тоже считается ошибкой теста. */
    fprintf(stderr, "%s: short pwrite: %zd\n", path, n);
    exit(EXIT_FAILURE);
  }

  /* Читаем тот же диапазон обратно, не меняя текущую позицию fd. */
  n = pread(fd, read_buf, sizeof(read_buf), 0);
  if (n != (ssize_t)sizeof(read_buf)) {
    if (n < 0)
      die("pread");
    fprintf(stderr, "%s: short pread: %zd\n", path, n);
    exit(EXIT_FAILURE);
  }

  /* Побайтово убеждаемся, что RAM-диск вернул ровно записанные данные. */
  if (memcmp(write_buf, read_buf, sizeof(write_buf)) != 0) {
    fprintf(stderr, "%s: READ/WRITE test FAILED\n", path);
    exit(EXIT_FAILURE);
  }

  printf("%s: READ/WRITE test OK (%d bytes)\n", path, TEST_SIZE);
  close(fd);
}

int main(void) {
  /* Имена устройств, которые обязан создать загруженный kernel-модуль. */
  const char *devices[TESDA_PARTITIONS] = {"/dev/tesda0", "/dev/tesda1",
                                           "/dev/tesda2"};
  int fd;
  unsigned int i;

  /* Для управляющих ioctl достаточно открыть первое устройство. */
  fd = open(devices[0], O_RDWR);
  if (fd < 0)
    die(devices[0]);

  /* Начинаем тест с известного состояния RAM и нулевых счётчиков. */
  printf("Resetting backing store and statistics...\n");
  if (ioctl(fd, TESDA_IOCTL_RESET) < 0)
    die("ioctl(RESET)");

  /* Проверяем и показываем UAPI до I/O-теста. */
  print_info(fd);
  close(fd);

  /* Независимо проверяем чтение/запись каждого /dev/tesdaN. */
  for (i = 0; i < TESDA_PARTITIONS; i++)
    test_device(devices[i], i);

  /* Повторно читаем статистику: после теста счётчики должны увеличиться. */
  fd = open(devices[0], O_RDWR);
  if (fd < 0)
    die(devices[0]);

  puts("\nStatistics after tests:");
  print_info(fd);
  close(fd);

  puts("\nAll tests passed.");
  return EXIT_SUCCESS;
}
