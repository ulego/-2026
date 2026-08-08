obj-m := tesda.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

load:
	sudo insmod tesda.ko
	@echo "Module loaded."

unload:
	sudo rmmod tesda
	@echo "Module unloaded."

.PHONY: all clean load unload