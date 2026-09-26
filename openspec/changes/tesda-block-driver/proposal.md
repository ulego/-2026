## Why

Требуется реализовать модуль ядра Linux (6.1.130) для драйвера блочного устройства `tesda` с тремя разделами по 100 MiB каждый, поддержкой операций read/write/ioctl и интеграцией в `/dev`, `/proc`, `/sys`. Драйвер необходим для проектной работы по разработке и регистрации драйвера устройства.

## What Changes

- Создать модуль ядра (`tesda.c`) — блочное устройство с тремя разделами (`tesda0`, `tesda1`, `tesda2`)
- Реализовать операции `block_device_operations`: open, release, ioctl
- Каждый раздел — 100 MiB (209715200 sectors по 512 байт), backing store — 300 MiB в vmalloc-памяти
- Обработка запросов через `blk_mq_init_sq_queue()` и `blk_mq_ops` (совместимо с ядром 6.1)
- ioctl-команды: `TESDA_IOCTL_RESET`, `TESDA_IOCTL_GETINFO`, `TESDA_IOCTL_GETSTAT`, `TESDA_IOCTL_GETPARTITION`
- `/proc/tesda` — текстовый интерфейс со статистикой по каждому разделу
- `/sys/class/tesda` — автоматически через `class_create` + `device_create`
- `/dev/tesda0/1/2` — символьные ссылки на `sda0/1/2`, созданные ядром через `add_disk`
- `Makefile` для сборки модуля и целей `load`/`unload`
- Тестовое приложение на C (`test_tesda.c`) — полная проверка read/write/ioctl/proc/sysfs
- Документация в `README.md`

## Capabilities

### New Capabilities
- `tesda-driver`: блочное устройство ядра с тремя разделами, операциями read/write/ioctl, интеграцией в /proc, /sys, /dev

### Modified Capabilities
<!-- None — this is a new capability -->

## Impact

- **Код модуля:** `tesda.c` (один модуль), `Makefile` (kbuild), `test_tesda.c` (пользовательское приложение)
- **API ядра:** `block_device_operations`, `blk_mq_ops`, `gendisk`, `class_device`, `proc_ops`
- **Зависимости:** только стандартное ядро Linux 6.1.x (заголовочные файлы `/lib/modules/$(uname -r)/build`)
- **Пользовательский интерфейс:** блочные устройства `/dev/tesda0..2`, ioctl-структуры `tesda_info`, `tesda_partition_info`
- **Без BREAKING изменений:** новый модуль не влияет на существующий код
