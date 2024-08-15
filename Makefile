KERNEL_RELEASE  ?= $(shell uname -r)
KERNEL_DIR      ?= /lib/modules/$(KERNEL_RELEASE)/build
obj-m           += nb.o

ccflags-y := -std=gnu11

.PHONY: all clean load unload

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean

load:
	sudo insmod nb.ko

unload:
	sudo rmmod nb


