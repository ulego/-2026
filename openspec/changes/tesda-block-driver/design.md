## Context

Реализуется модуль ядра Linux (6.1.130) для блочного устройства с тремя разделами. Проектная работа — разработка и регистрация драйвера устройства. Код пишется с нуля, существующих компонентов нет.

## Goals / Non-Goals

**Goals:**
- Один модуль ядра `tesda.c` — блочное устройство с тремя разделами
- Backing store — 300 MiB в vmalloc-памяти
- Обработка I/O через `blk_mq_init_sq_queue()` (совместимо с ядром 6.1, без deprecated API)
- ioctl-интерфейс: RESET, GETINFO, GETSTAT, GETPARTITION
- `/proc/tesda` — текстовая статистика по каждому разделу
- `/sys/class/tesda` — sysfs-класс устройства
- `/dev/tesda0/1/2` — символьные ссылки на kernel block devices
- `Makefile` для kbuild
- Тестовое приложение `test_tesda.c` — полная проверка
- Документация `README.md`

**Non-Goals:**
- Персистентное хранилище (данные живут только в памяти ядра)
- Шифрование или сжатие данных
- Поддержка hot-plug / hot-remove разделов
- Мультипоточный blk-mq (достаточно single-queue)
- UDEV правила для автоматического создания /dev — делаем вручную в init/exit

## Decisions

### D1: Single gendisk with 3 partitions (not three separate gendisks)
**Решение:** Один `gendisk` с `alloc_disk(3)`, три partition-а.
**Почему:** Ядро автоматически обрабатывает offsets/limits для каждого раздела. Меньше boilerplate, один major number.
**Альтернатива:** Три отдельных `gendisk` — три разных major number, сложнее управление.

### D2: `blk_mq_init_sq_queue()` вместо `blk_init_queue()`
**Решение:** Использовать `blk_mq_init_sq_queue()` с `blk_mq_ops`.
**Почему:** `blk_init_queue()` + `make_request_fn` — deprecated в ядре 6.1, вызывают предупреждения в dmesg. `blk_mq_init_sq_queue()` — тот же single-queue behaviour, но без deprecated-нотификаций.
**Альтернатива:** `blk_init_queue()` — просто, но deprecated. Полный blk-mq — избыточно для этого задания.

### D3: vmalloc для backing store
**Решение:** Единый `vmalloc(300 MiB)` буфер, разделённый на три partition offset.
**Почему:** 300 MiB почти гарантированно найдутся в linear virtual address space. `alloc_pages()` потребовал бы contiguous physical memory, что для 300 MiB почти невозможно.
**Альтернатива:** `alloc_pages()` — невозможно для такого размера.

### D4: Symlink /dev/tesdaN → /dev/sdaN
**Решение:** `add_disk()` создаёт `/dev/sdaN` (через udev), мы создаём symlink `/dev/tesdaN → /dev/sdaN` через `symlink()` после `add_disk()`.
**Почему:** Задание требует имя `tesda`. `add_disk()` не позволяет менять имя partition-устройства.
**Альтернатива:** `device_create()` с именем `tesdaN` — дублирование устройств (и sdaN, и tesdaN).

### D5: Dynamic major number
**Решение:** `register_blkdev()` для получения динамического major.
**Почему:** Модуль загружается/выгружается, статический major может конфликтовать. Динамический — всегда свободный.

### D6: ioctl через `block_device_operations`
**Решение:** `.unlocked_ioctl` в `block_device_operations` для `/dev/tesdaN`.
**Почему:** Стандартный путь для блочных ioctl. Ядро уже обрабатывает стандартные BLK* ioctl, мы добавляем свои.
**ABI:** ioctl commands через `_IOWR`/`_IOW` macros с magic number `0xA0`.

## Risks / Trade-offs

| Risk | Mitigation |
|------|-----------|
| `vmalloc(300 MiB)` может занять много contiguous virtual address space | В 64-bit Linux linear vmalloc region обычно > 1 GiB. Проверка на NULL/vfree в error path. |
| `blk_mq_init_sq_queue()` — API может меняться в новых ядрах | Для 6.1.x стабильный. Задание 6.1.130. |
| Символьные ссылки в /dev — требуют root | Модуль загружается с root, `sysmlink()` в kernel space. |
| `/proc` — legacy API в новых ядрах | `proc_ops` структура вместо устаревшего `file_operations` для proc. |
| Нет реального блочного диска — только RAM backing | Ядро может ждать таймаута при монтировании. Документировать в README. |
| Race conditions при параллельном доступе к одному разделу | `spinlock_t` в структуре устройства для защиты stats и backing store. |

## Migration Plan

### Deploy
1. Собрать модуль: `make -C /lib/modules/$(uname -r)/build M=$(pwd) modules`
2. Загрузить: `sudo insmod tesda.ko`
3. Проверить: `dmesg | tail`, `ls -la /dev/tesda*`, `cat /proc/tesda`
4. Запустить тест: `gcc test_tesda.c -o test_tesda && sudo ./test_tesda`

### Rollback
1. Остановить тестовое приложение (если запущено)
2. `sudo rmmod tesda`
3. Очистить сборку: `make clean`

### Uninstall
1. `rmmod tesda` — освобождает все ресурсы
2. `make clean` — удаляет .ko, модули сборки

## Open Questions

- **Нет** — все ключевые решения приняты в ходе explore-сессии.
