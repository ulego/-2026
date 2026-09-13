# Makefile для сборки учебного блочного драйвера TESDA и пользовательского теста.
#
# Внешние модули ядра собираются не обычным вызовом gcc, а через систему
# сборки самого ядра (Kbuild). Поэтому цель module передаёт управление в
# каталог headers текущего ядра и указывает M=$(PWD), то есть каталог проекта.

# Сообщаем Kbuild, что из исходника tesda.c нужно собрать внешний модуль tesda.ko.
obj-m := tesda.o

# Каталог дерева сборки/headers работающего ядра.
# Переменную KDIR можно переопределить при вызове make, например:
#   make KDIR=/usr/src/linux-headers-6.1.130
KDIR ?= /lib/modules/$(shell uname -r)/build

# Абсолютный путь к текущему проекту; передаётся Kbuild как M=...
PWD  := $(CURDIR)

# Компилятор для пользовательской программы test_tesda.c.
# Для самого kernel-модуля компилятор выбирает Kbuild.
CC   ?= gcc

# Эти цели не соответствуют реальным файлам, поэтому объявляем их phony.
.PHONY: all module test clean load unload reload status

# Цель по умолчанию: собрать и модуль ядра, и пользовательский тест.
all: module test

# Сборка tesda.ko через штатную систему сборки ядра Linux.
module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Сборка пользовательской тестовой программы.
# -Wall/-Wextra/-Wpedantic включают расширенный набор предупреждений,
# -O2 включает обычную оптимизацию без агрессивных нестандартных приёмов.
test:
	$(CC) -Wall -Wextra -Wpedantic -O2 -o test_tesda test_tesda.c

# Удаление результатов Kbuild и отдельно созданного бинарника test_tesda.
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f test_tesda

# Сначала гарантированно собираем модуль, затем загружаем его в ядро.
load: module
	sudo insmod ./tesda.ko

# Выгрузка модуля. Начальный '-' говорит make не считать ошибку rmmod
# фатальной, например если модуль сейчас не загружен.
unload:
	-sudo rmmod tesda

# Удобная цель для пересборки/перезагрузки во время разработки.
reload: unload load

# Диагностическая цель: показывает созданные /dev-устройства,
# содержимое /proc/tesda и пользовательские атрибуты каждого gendisk в sysfs.
status:
	@echo "--- /dev ---"
	@ls -l /dev/tesda* 2>/dev/null || true
	@echo "--- /proc/tesda ---"
	@cat /proc/tesda 2>/dev/null || true
	@echo "--- /sys ---"
	@for d in /sys/block/tesda*; do \
		[ -d "$$d" ] || continue; \
		echo "$$d"; \
		cat "$$d/tesda_id" 2>/dev/null || true; \
		cat "$$d/tesda_start_sector" 2>/dev/null || true; \
		cat "$$d/tesda_stats" 2>/dev/null || true; \
	done
