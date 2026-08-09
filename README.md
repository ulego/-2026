# tesda — Блочное устройство ядра Linux

Драйвер блочного устройства `tesda` с тремя разделами по 100 MiB каждый.
tesda - test sda

## Обзор

- **Тип:** блочное устройство (block device)
- **Разделы:** 3 × 100 MiB (`/dev/tesda0`, `/dev/tesda1`, `/dev/tesda2`)
- **Backing store:** 300 MiB в памяти ядра (`vmalloc`)
- **Операции:** read, write, ioctl
- **Интерфейсы:** `/dev`, `/proc`, `/sys`
- **Очередь:** blk-mq single-queue
- **Ядро:** Linux 6.1.x (и совместимые)

## Требования

- Ядро Linux 6.1.x с заголовочными файлами
- `make`, `gcc`
- Права root для загрузки модуля и доступа к устройствам

## Сборка

``` bash
make
```
## Результат: tesda.ko

## Очистка
``` bash
make clean
```
## Загрузка и выгрузка
### Загрузка модуля
``` bash
sudo insmod tesda.ko
```
## После загрузки:
Блочные устройства: **/dev/sda0**, **/dev/sda1**, **/dev/sda2**
Символьные ссылки: **/dev/tesda0**, **/dev/tesda1**, **/dev/tesda2**
Proc-файл: **/proc/tesda**
Sysfs-класс: **/sys/class/tesda/**

## Проверка:
``` bash
dmesg | tail
cat /proc/tesda
ls -la /dev/tesda*
```
### Выгрузка модуля
sudo rmmod tesda
## Удобные цели в Makefile
make load   # загружает модуль
make unload # выгружает модуль
## Использование
Устройства tesda ведут себя как обычные блочные устройства. Данные записываются и читаются через стандартные системные вызовы read() и write().

##Пример на C:
``` C
int fd = open("/dev/tesda0", O_RDWR);
char buf[4096] = "Hello, tesda!";
write(fd, buf, sizeof(buf));

memset(buf, 0, sizeof(buf));
lseek(fd, 0, SEEK_SET);
read(fd, buf, sizeof(buf));
// buf содержит "Hello, tesda!"
close(fd);
IOCTL команды
TESDA_IOCTL_RESET
Очищает все данные и сбрасывает статистику на всех разделах.

c
ioctl(fd, TESDA_IOCTL_RESET);
TESDA_IOCTL_GETINFO
Возвращает информацию об устройстве.

c
struct tesda_info info;
ioctl(fd, TESDA_IOCTL_GETINFO, &info);
// info.partitions     = 3
// info.part_size      = 104857600  (100 MiB)
// info.total_size     = 314572800  (300 MiB)
// info.sector_size    = 512
Структура:

c
struct tesda_info {
    unsigned int partitions;
    unsigned long long part_size;
    unsigned long long total_size;
    unsigned int sector_size;
    unsigned int reserved;
};
TESDA_IOCTL_GETSTAT
Возвращает агрегированную статистику по всем разделам.

c
struct tesda_stat stat;
ioctl(fd, TESDA_IOCTL_GETSTAT, &stat);
// stat.reads, stat.writes, stat.bytes_read, stat.bytes_written
TESDA_IOCTL_GETPARTITION
Возвращает информацию о разделе. Передаётся структура с заполненным id.

c
struct tesda_partition_info info;
info.id = 1;
ioctl(fd, TESDA_IOCTL_GETPARTITION, &info);
Интерфейс /proc/tesda
Текстовый файл со статистикой по каждому разделу.

Интерфейс /sys
Устройство появляется в /sys/class/tesda/.

##Тестирование
Компиляция тестового приложения
bash
gcc -o test_tesda test_tesda.c
Запуск
bash
sudo ./test_tesda /dev/tesda0 all
sudo ./test_tesda /dev/tesda1 info
Архитектура
(см. схему в коде README)

##Известные ограничения
Память ядра: данные хранятся только в ОЗУ. При выгрузке модуля данные теряются.

Без файловой системы: сырой блочный интерфейс.

Права root: требуются для загрузки и управления.

Single-queue: только одна очередь.

##Лицензия
GPL-2.0

