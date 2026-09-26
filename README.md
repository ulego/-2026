# TESDA — RAM-backed block driver for Linux 6.1.x

Учебный блочный драйвер для Linux 6.1.x. Проект рассчитан на сборку с headers Linux 6.1.130 и использует современный для этой ветки интерфейс `blk-mq`.

## Настройка окружения  
### --------------------------------------------- 1. qemu образ  
####  https://github.com/OTUS-Linux-kernel-dev/Linux-kernel-dev/blob/main/doc/module_1/0x04_QA.md ->  
##### https://drive.google.com/file/d/1PR166GsjU48tAmWT0s_UAM7OcUmrcze-/view?usp=sharing  
```
wget -c  'https://drive.usercontent.google.com/download?id=1PR166GsjU48tAmWT0s_UAM7OcUmrcze-&export=download&authuser=0&confirm=t&uuid=17b39775-6077-48f7-bd96-ee9edb2f614a&at=AMrWOn1CJK0tdnN0hII0qdRdfHft%3A1789811460456' -O debian_12_6.1.130.tar.xz 

tar -xvf debian_12_6.1.130.tar.xz

sudo apt update
sudo apt install qemu-kvm libvirt-daemon-system libvirt-clients virt-manager screen

sudo systemctl start libvirtd
sudo systemctl enable libvirtd


virt-manager
```  
#### Создайте новую виртуальную машину
#### В интерфейсе virt-manager нажмите на кнопку "Создать виртуальную машину".  
#### Выберите "Импорт образа диска" и нажмите "Далее".  
#### Укажите путь к файлу debian_12_6.1.130.qcow2 нажав кнопку «Обзор», а затем «Выбрать файл».  
#### Выберите Debian 11 или другой наиболее близкий к Debian 12 вариант в выпадающем списке.  
#### Нажмите "Далее".  
#### Настройте параметры виртуальной машины, такие как количество ядер процессора и объем оперативной памяти.  
#### Нажмите "Далее", затем "Готово".  

```qemu-system-x86_64 -enable-kvm -m 2048 -vnc :0 -device virtio-blk-device,drive=hd0 -drive if=none,id=hd0,file=debian_12_6.1.130.qcow2,format=qcow2``` 

#### student:1


#### --------------------------------------------- 2.  Модуль ядра  
```git clone https://github.com/ulego/-2026.git```  

## Что создаётся

После загрузки модуля должны появиться:

- `/dev/tesda0` — 100 MiB;
- `/dev/tesda1` — 100 MiB;
- `/dev/tesda2` — 100 MiB;
- `/proc/tesda` — информация и статистика;
- `/sys/block/tesda0/tesda_id`, `tesda_start_sector`, `tesda_stats`;
- аналогичные sysfs-файлы для `tesda1` и `tesda2`.

Общий backing store — 300 MiB, выделяется через `vzalloc()`.

## Почему здесь нет alloc_disk() и blk_cleanup_queue()

В headers Linux 6.1.130 из исходного проекта `alloc_disk()` и `blk_cleanup_queue()` недоступны. Для blk-mq используется:

```c
blk_mq_alloc_disk(&dev->tag_set, part)
```

Она создаёт `gendisk` вместе с его `request_queue`.

При удалении успешно зарегистрированного диска используется:

```c
del_gendisk(part->disk);
put_disk(part->disk);
```

Отдельно освобождать очередь через `blk_cleanup_queue()` нельзя.

## Сборка

Нужны gcc, make и headers текущего ядра.

```bash
uname -r
make clean
make
```

Для указанной в задании системы ожидается примерно:

```text
6.1.130
```

После сборки появятся `tesda.ko` и `test_tesda`.

## Загрузка

```bash
sudo insmod ./tesda.ko
```

Проверка:

```bash
ls -l /dev/tesda*
cat /proc/tesda
make status
```

Также полезно:

```bash
dmesg | tail -50
lsblk
```

Если в минимальной системе нет udev/devtmpfs и файлы `/dev/tesdaN` не появились автоматически, major можно посмотреть в `/proc/devices` и создать nodes вручную. В обычном Debian/Ubuntu это обычно не требуется.

## READ / WRITE

Например:

```bash
echo "hello tesda" | sudo dd of=/dev/tesda0 bs=512 conv=sync
sudo dd if=/dev/tesda0 bs=512 count=1 | hexdump -C
```

Каждое устройство имеет отдельный участок общего backing store, поэтому запись в `tesda0` не должна изменять данные `tesda1` или `tesda2`.

## ioctl

В `tesda_uapi.h` определены команды:

- `TESDA_IOCTL_RESET` — обнулить все 300 MiB и статистику;
- `TESDA_IOCTL_GETINFO` — получить размеры и число устройств;
- `TESDA_IOCTL_GETSTAT` — получить суммарную статистику;
- `TESDA_IOCTL_GETPARTITION` — получить описание выбранного tesdaN.

Готовый пользовательский тест:

```bash
sudo ./test_tesda
```

Он выполняет RESET, GETINFO, GETPARTITION, GETSTAT, затем проверяет запись и чтение 4096 байт на всех трёх устройствах.

## /proc

```bash
cat /proc/tesda
```

Показываются major, размеры, начальные секторы и счётчики операций.

## /sys

```bash
cat /sys/block/tesda0/tesda_id
cat /sys/block/tesda0/tesda_start_sector
cat /sys/block/tesda0/tesda_stats
```

## Выгрузка

Перед выгрузкой не должно быть смонтированных файловых систем на tesdaN.

```bash
sudo rmmod tesda
```

или:

```bash
make unload
```

## Быстрая последовательность проверки

```bash
make clean
make
sudo insmod ./tesda.ko
ls -l /dev/tesda*
cat /proc/tesda
sudo ./test_tesda
make status
sudo rmmod tesda
dmesg | tail -50
```

## Важное замечание по заданию

В старых учебных заданиях может требоваться `make_request_fn`. В Linux 6.1 этот старый путь уже не является нормальным API для нового драйвера; данный вариант использует `blk-mq` и callback `queue_rq`, что соответствует block layer Linux 6.1.x.
