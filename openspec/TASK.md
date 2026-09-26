# tesda — Блочное устройство ядра Linux

Драйвер блочного устройства `tesda` с тремя разделами по 100 MiB каждый.

## Обзор

- **Тип:** блочное устройство (block device)
- **Разделы:** 3 × 100 MiB (`/dev/tesda0`, `/dev/tesda1`, `/dev/tesda2`)
- **Backing store:** 300 MiB в памяти ядра (`vmalloc`)
- **Операции:** read, write, ioctl
- **Интерфейсы:** `/dev`, `/proc`, `/sys`
- **Ядро:** Linux 6.1.x

## Сборка и загрузка

### Требования

- Ядро Linux 6.1.x с заголовочными файлами
- make
- gcc

### Сборка модуля

```bash
make
```

Результат: `tesda.ko`

### Загрузка модуля

```bash
sudo insmod tesda.ko
```

После загрузки:
- Блочные устройства: `/dev/sda0`, `/dev/sda1`, `/dev/sda2`
- Символьные ссылки: `/dev/tesda0`, `/dev/tesda1`, `/dev/tesda2`
- Proc-файл: `/proc/tesda`
- Sysfs-класс: `/sys/class/tesda/`

Проверка:

```bash
dmesg | tail
cat /proc/tesda
ls -la /dev/tesda*
```

### Выгрузка модуля

```bash
sudo rmmod tesda
```

### Очистка

```bash
make clean
```

## Использование

### Базовая запись/чтение

Устройства `tesda` ведут себя как обычные блочные устройства. Данные записываются и читаются через стандартные системные вызовы `read()` и `write()`.

```c
int fd = open("/dev/tesda0", O_RDWR);
char buf[4096] = "Hello, tesda!";
write(fd, buf, sizeof(buf));

memset(buf, 0, sizeof(buf));
lseek(fd, 0, SEEK_SET);
read(fd, buf, sizeof(buf));
// buf содержит "Hello, tesda!"
close(fd);
```

### IOCTL команды

#### TESDA_IOCTL_RESET

Очищает все данные и сбрасывает статистику на всех разделах.

```c
ioctl(fd, TESDA_IOCTL_RESET);
```

#### TESDA_IOCTL_GETINFO

Возвращает информацию о устройстве:

```c
struct tesda_info info;
ioctl(fd, TESDA_IOCTL_GETINFO, &info);
// info.partitions     = 3
// info.part_size      = 104857600  (100 MiB)
// info.total_size     = 314572800  (300 MiB)
// info.sector_size    = 512
```

Структура:

```c
struct tesda_info {
    unsigned int partitions;     // 3
    unsigned long long part_size;  // 104857600 (100 MiB)
    unsigned long long total_size; // 314572800 (300 MiB)
    unsigned int sector_size;      // 512
    unsigned int reserved;
};
```

#### TESDA_IOCTL_GETSTAT

Возвращает агрегированную статистику по всем разделам:

```c
struct tesda_stat stat;
ioctl(fd, TESDA_IOCTL_GETSTAT, &stat);
// stat.reads            // общее число операций чтения
// stat.writes           // общее число операций записи
// stat.bytes_read       // общее число прочитанных байт
// stat.bytes_written    // общее число записанных байт
```

Структура:

```c
struct tesda_stat {
    unsigned long long reads;
    unsigned long long writes;
    unsigned long long bytes_read;
    unsigned long long bytes_written;
};
```

#### TESDA_IOCTL_GETPARTITION

Возвращает информацию о конкретном разделе. На вход подаётся ID раздела (0-2):

```c
int part_id = 1;
ioctl(fd, TESDA_IOCTL_GETPARTITION, &part_id);
// part_id == 1 → info для /dev/tesda1
```

Возвращаемая структура:

```c
struct tesda_partition_info {
    unsigned int id;              // ID раздела (0, 1 или 2)
    unsigned int reserved;
    unsigned long long start_sector; // Начальный сектор
    unsigned long long nr_sectors;   // Количество секторов (209715200)
    char name[16];               // Имя ("tesda0", "tesda1", "tesda2")
};
```

## Интерфейс /proc/tesda

Текстовый файл со статистикой по каждому разделу:

```bash
$ cat /proc/tesda
Device: tesda
Major: 240
Partitions: 3

Partition 0:
  Name:     tesda0
  Size:     100 MiB (209715200 sectors)
  Start:    0
  Reads:    5
  Writes:   3
  Bytes RD: 2560
  Bytes WR: 1536

Partition 1:
  Name:     tesda1
  ...
```

## Интерфейс /sys

Устройство появляется в `/sys/class/tesda/`:

```bash
$ ls -la /sys/class/tesda/
total 0
drwxr-xr-x  2 root root 0 Aug  7 12:00 .
drwxr-xr-x 25 root root 0 Aug  7 12:00 ..
lrwxrwxrw-  1 root root 0 Aug  7 12:00 tesda0 -> ../sda0
lrwxrwxrw-  1 root root 0 Aug  7 12:00 tesda1 -> ../sda1
lrwxrwxrw-  1 root root 0 Aug  7 12:00 tesda2 -> ../sda2
```

## Тестирование

### Компиляция тестового приложения

```bash
gcc -o test_tesda test_tesda.c
```

### Запуск

```bash
sudo ./test_tesda /dev/tesda0
sudo ./test_tesda /dev/tesda1
sudo ./test_tesda /dev/tesda2
```

Тест проверяет:
- Запись данных в устройство
- Чтение данных обратно с проверкой паттерна
- IOCTL команды (GETINFO, GETSTAT, RESET)
- Обработку невалидных ioctl-запросов
- Интерфейс `/proc/tesda`
- Интерфейс `/sys/class/tesda`

## Архитектура

```
┌─────────────────────────────────────────┐
│  User Space                             │
│  ┌──────────────────────────────────┐   │
│  │ test_tesda / any app             │   │
│  │ open/read/write/ioctl(/dev/tesdaN)│  │
│  └──────────┬───────────────────────┘   │
│             │ VFS                       │
│             ▼                           │
│  Kernel Space                           │
│  ┌──────────────────────────────────┐   │
│  │ block_device_operations          │   │
│  │ - open/release                   │   │
│  │ - ioctl (RESET/GETINFO/...)      │   │
│  │ - blk-mq queue_rq → tesda_queue_rq│  │
│  └──────────┬───────────────────────┘   │
│             │                           │
│             ▼                           │
│  ┌──────────────────────────────────┐   │
│  │ gendisk "sda" + 3 partitions     │   │
│  │ part[0] → /dev/sda0 (tesda0)     │   │
│  │ part[1] → /dev/sda1 (tesda1)     │   │
│  │ part[2] → /dev/sda2 (tesda2)     │   │
│  └──────────┬───────────────────────┘   │
│             │                           │
│             ▼                           │
│  ┌──────────────────────────────────┐   │
│  │ vmalloc(300 MiB) backing store   │   │
│  │ ┌──────────┬──────────┬────────┐ │   │
│  │ │part0 100MiB│part1 100MiB│part2 100MiB│ │   │
│  │ └──────────┴──────────┴────────┘ │   │
│  └──────────────────────────────────┘   │
└─────────────────────────────────────────┘
```

## Известные ограничения

- **Память ядра:** данные хранятся только в оперативной памяти ядра. При выгрузке модуля (`rmmod`) или перезагрузке все данные теряются.
- **Без монтирования ФС:** устройство предоставляет "сырой" блочный интерфейс. Для использования с файловой системой потребуется `mkfs` и `mount`, но это может быть нестабильно из-за отсутствия реального блочного носителя.
- **Только root:** загрузка модуля (`insmod`), управление устройством и запуск тестов требуют прав root.
- **single-queue:** очередь blk-mq single-queue, без мультипотока.

## Структура проекта

```
.
├── Makefile          # kbuild: make, make load, make unload, make clean
├── tesda.c           # Драйвер блочного устройства (модуль ядра)
├── test_tesda.c      # Тестовое приложение на C
├── README.md         # Этот файл
└── openspec/         # OpenSpec change artifacts
```

## Лицензия

GPL-2.0
