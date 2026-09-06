obj-m := tesda.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(CURDIR)
CC   ?= gcc

.PHONY: all module test clean load unload reload status

all: module test

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

test:
	$(CC) -Wall -Wextra -Wpedantic -O2 -o test_tesda test_tesda.c

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f test_tesda

load: module
	sudo insmod ./tesda.ko

unload:
	-sudo rmmod tesda

reload: unload load

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
