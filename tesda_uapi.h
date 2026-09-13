/*
 * tesda_uapi.h — общий UAPI-заголовок драйвера TESDA.
 *
 * Этот файл включается одновременно в kernel-модуль и в пользовательскую
 * программу test_tesda. Поэтому здесь находятся только типы и константы,
 * которые составляют внешний ABI: геометрия устройств и номера ioctl.
 */
#ifndef TESDA_UAPI_H
#define TESDA_UAPI_H

/* _IO/_IOR/_IOWR — макросы кодирования ioctl-команд. */
#include <linux/ioctl.h>
/* __u32/__u64 — типы фиксированной разрядности, пригодные для UAPI. */
#include <linux/types.h>

/* Количество отдельных блочных устройств /dev/tesda0..2. */
#define TESDA_PARTITIONS 3U
/* Логический размер сектора блочного устройства: 512 байт. */
#define TESDA_SECTOR_SIZE 512ULL
/* Размер каждого tesdaN: 100 MiB. */
#define TESDA_PART_SIZE_BYTES (100ULL * 1024ULL * 1024ULL)
/* Общий RAM backing store: 3 * 100 MiB = 300 MiB. */
#define TESDA_TOTAL_SIZE_BYTES (TESDA_PARTITIONS * TESDA_PART_SIZE_BYTES)

/* Максимальная длина имени устройства, возвращаемого через ioctl. */
#define TESDA_NAME_LEN 32

/*
 * Общая информация о драйвере. Структура заполняется ядром при
 * TESDA_IOCTL_GETINFO и копируется в адресное пространство процесса.
 */
struct tesda_info {
  __u32 partitions;       /* количество устройств tesdaN */
  __u32 sector_size;      /* размер логического сектора в байтах */
  __u64 part_size_bytes;  /* размер одного устройства в байтах */
  __u64 total_size_bytes; /* полный размер backing store */
};

/* Суммарная статистика операций по всем трём устройствам. */
struct tesda_stat {
  __u64 reads;         /* число обработанных BIO чтения */
  __u64 writes;        /* число обработанных BIO записи */
  __u64 bytes_read;    /* суммарно прочитано байт */
  __u64 bytes_written; /* суммарно записано байт */
};

/*
 * Информация об одном tesdaN. Поле id пользователь задаёт перед ioctl,
 * остальные поля драйвер заполняет и возвращает обратно.
 */
struct tesda_partition_info {
  __u32 id;                  /* индекс устройства: 0, 1 или 2 */
  __u32 reserved;            /* зарезервировано для совместимости ABI */
  __u64 start_sector;        /* начало участка в общем 300-MiB хранилище */
  __u64 nr_sectors;          /* количество секторов в устройстве */
  char name[TESDA_NAME_LEN]; /* строковое имя: tesda0/tesda1/tesda2 */
};

/* Уникальная «магическая» буква семейства ioctl-команд TESDA. */
#define TESDA_IOCTL_MAGIC 'T'
/* Команда без аргумента: обнулить backing store и статистику. */
#define TESDA_IOCTL_RESET _IO(TESDA_IOCTL_MAGIC, 0x01)
/* Команда чтения структуры tesda_info из ядра в user space. */
#define TESDA_IOCTL_GETINFO _IOR(TESDA_IOCTL_MAGIC, 0x02, struct tesda_info)
/* Команда чтения суммарной статистики. */
#define TESDA_IOCTL_GETSTAT _IOR(TESDA_IOCTL_MAGIC, 0x03, struct tesda_stat)
/* Двунаправленная команда: передать id и получить описание tesdaN. */
#define TESDA_IOCTL_GETPARTITION                                               \
  _IOWR(TESDA_IOCTL_MAGIC, 0x04, struct tesda_partition_info)

#endif /* TESDA_UAPI_H */
